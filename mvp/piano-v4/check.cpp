// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "preset.hpp"
#include "pme/simd/denormals.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <numbers>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static double energy(piano_mvp_v4::Piano& p, int frames) {
    double e = 0;
    for (int i = 0; i < frames; ++i) {
        float l, r; p.sample(l, r);
        require(std::isfinite(l) && std::isfinite(r) && std::abs(l) <= 1 && std::abs(r) <= 1,
                "invalid audio");
        e += l * l + r * r;
    }
    require(p.numericalFaults() == 0, "a non-finite voice was silently retired");
    return e;
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        // Independent anchors transcribed from Appendix A of the revised report.
        const auto c4 = piano_mvp_v2::parametersForKey(60);
        require(c4.string.L == 0.657 && c4.string.dCore == 0.001006 &&
                c4.string.rhoCore == 7850 && c4.string.ECore == 2.02e11,
                "middle C geometry differs from report v2");
        require(std::abs(c4.hammer.mass - 0.00871) < 1e-12 &&
                c4.hammer.K == 5.840e9 && c4.hammer.p == 2.418 &&
                std::abs(c4.hammer.strikeRatio * c4.string.L - 0.079) < 1e-12,
                "middle C hammer differs from report");
        require(!c4.extrapolated, "middle C marked extrapolated");
        const auto c1 = piano_mvp_v2::parametersForKey(24);
        const auto c7 = piano_mvp_v2::parametersForKey(96);
        require(c1.string.L == 2.007 && c1.string.rhoCore == 57787 &&
                c1.source.tension_n == 1722, "wrapped bass table mapping failed");
        require(c7.string.L == 0.095 && c7.source.tension_n == 670,
                "treble table mapping failed");
        for (int key = 21; key <= 108; ++key) {
            const auto parameters = piano_mvp_v2::parametersForKey(key);
            require(parameters.extrapolated == (key < 24 || key > 107), "incorrect extrapolation flag");
            pme::StringModes modes;
            require(piano_mvp_v2::compilePresetString(parameters, 48000, modes) == pme::Status::Ok,
                    "report parameters did not compile");
            require(std::abs(modes.tension / parameters.source.tension_n - 1) < 1e-12,
                    "published tension was not preserved");
            require(modes.sigma.back() >= modes.sigma.front() && modes.sigma.front() >= 0,
                    "invalid modal losses");
        }
        pme::StringModes middle;
        require(piano_mvp_v2::compilePresetString(c4, 48000, middle) == pme::Status::Ok,
                "middle C compilation failed");
        require(std::abs(middle.omega.front() / (2 * std::numbers::pi) - 262.3034645048) < 1e-7,
                "middle C frequency wrong for published tension and geometry");
        // From the projected source damping equation, not the MVP's old b1/b2/b3 curve.
        const double sigma1 = 0.18 + 2.6e-9 * 741 / middle.rhoA * std::pow(std::numbers::pi / 0.657, 2);
        require(std::abs(middle.sigma.front() - sigma1) < 1e-12, "source damping mapping failed");
        piano_mvp_v4::Piano p;
        require(piano_mvp_v4::Piano::stringsForKey(21) == 1 &&
                piano_mvp_v4::Piano::stringsForKey(33) == 2 &&
                piano_mvp_v4::Piano::stringsForKey(60) == 3,
                "unison register mapping failed");
        require(!p.prepare(0), "invalid sample rate accepted");
        for (double fs : {44100., 48000., 96000.}) {
            require(p.prepare(fs), "prepare failed");
            require(energy(p, 256) == 0, "idle is not silent");
            for (int key = 21; key <= 108; ++key) {
                p.reset(); p.noteOn(key, 0.8);
                require(energy(p, static_cast<int>(fs * 0.25)) > 1e-9, "silent key");
            }
        }
        require(p.prepare(48000), "prepare failed");
        require(!p.setBridgeFeedback(false), "feedback changed after preparation");
        require(!p.setUnisonStrings(false), "string group changed after preparation");
        require(!p.setResidualBridgeLoss(false), "bridge loss changed after preparation");
        p.noteOn(60, 0.2); const double soft = energy(p, 12000);
        require(p.peakBridgeMotion() > 1e-12, "enabled bridge did not move");
        p.reset(); p.noteOn(60, 0.9); const double loud = energy(p, 12000);
        require(loud > soft * 2, "velocity has insufficient effect");
        p.reset(); p.sustain(true); p.noteOn(60, 0.7); energy(p, 4800); p.noteOff(60);
        require(energy(p, 24000) > 1e-5 && p.activeVoices() == 1, "sustain did not hold note");
        p.sustain(false); energy(p, 48000);
        require(p.activeVoices() == 0, "released note did not retire");
        p.reset();
        for (int key = 21; key <= 108; ++key) p.noteOn(key, 0.8);
        require(p.activeVoices() == piano_mvp_v4::Piano::maxVoices, "polyphony limit failed");
        require(energy(p, 24000) > 1e-5, "chord silent");
        for (int first : {21, 48, 81}) {
            p.reset();
            for (int key = first; key < first+16; ++key) p.noteOn(key, 1.0);
            require(energy(p, 24000) > 1e-5, "full-velocity chord silent");
        }
        p.reset(); require(energy(p, 1024) == 0, "reset left an audio tail");
        piano_mvp_v4::Piano fixed;
        require(fixed.setBridgeFeedback(false) && fixed.prepare(48000), "fixed control failed");
        fixed.noteOn(60,0.75); require(energy(fixed,12000) > 1e-5,"fixed control is silent");
        require(fixed.peakBridgeMotion() == 0,"fixed control bridge moved");
        piano_mvp_v4::Piano single;
        require(single.setUnisonStrings(false) && single.prepare(48000), "single control failed");
        single.noteOn(60,0.9);
        const double singleEnergy = energy(single,12000);
        require(std::abs(singleEnergy/loud-1) > 0.01, "unisons have no audible-path effect");
        std::cout << "PASS: source anchors, tension, loss projection, extrapolation; 88 keys at 3 sample rates, unisons, velocity, sustain, release, voice limit, reset\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
