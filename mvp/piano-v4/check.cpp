// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "preset.hpp"
#include "string_loss.hpp"
#include "bridge_mass.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <numbers>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
// Fourth-difference energy over total energy: a monotone high-frequency weighting (~omega^4).
static double brightness(piano_mvp_v4::Piano& p, int frames) {
    double x[5]{}, high = 0, total = 0;
    for (int i = 0; i < frames; ++i) {
        float l, r; p.sample(l, r);
        for (int k = 4; k > 0; --k) x[k] = x[k-1];
        x[0] = 0.5 * (l + r);
        const double d4 = x[0] - 4*x[1] + 6*x[2] - 4*x[3] + x[4];
        high += d4 * d4; total += x[0] * x[0];
    }
    return total > 0 ? high / total : 0;
}
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
        require(!p.setSoftFelt(false), "felt voicing changed after preparation");
        require(piano_mvp_v4::Piano::feltStiffnessScale(60)==0.35 &&
                piano_mvp_v4::Piano::feltStiffnessScale(84)==1.0,
                "felt voicing register anchors changed");
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
        {   // 0.4.9: measured excess of 2-8 kHz attack energy and over-fast partial decay above 2 kHz.
            const auto bass = piano_mvp_v2::parametersForKey(45);
            pme::StringModes m;
            require(piano_mvp_v2::compilePresetString(bass, 48000, m) == pme::Status::Ok, "bass compile failed");
            const double eta = bass.source.transverse_viscosity_s, corner = piano_mvp_v4::kStringLossCornerHz;
            for (std::size_t n = 0; n < m.omega.size(); ++n) {
                const double plain = eta * m.tension / m.rhoA * m.k[n] * m.k[n];
                const double sat = piano_mvp_v4::saturatedStringViscosity(eta, m.tension, m.rhoA, m.k[n], m.omega[n], corner);
                const double hz = m.omega[n] / (2 * std::numbers::pi);
                if (hz <= corner) require(std::abs(sat - plain) <= 1e-12 * plain, "viscosity changed below the corner");
                else require(std::abs(sat / m.omega[n] / (eta * 2 * std::numbers::pi * corner) - 1) < 1e-9,
                             "viscosity above the corner is not a constant loss factor");
            }
            pme::StringModes sm = m;
            require(piano_mvp_v4::applySaturatingStringLoss(sm, eta, corner) == pme::Status::Ok, "saturating loss failed");
            for (std::size_t n = 1; n < sm.sigma.size(); ++n)
                {
                require(sm.sigma[n] >= sm.sigma[n-1], "saturated loss not monotone");
                if (m.omega[n] > 1.5 * 2 * std::numbers::pi * corner)
                    require(sm.sigma[n] <= m.sigma[n], "saturated loss exceeds the report law");
            }
            require(sm.sigma.back() < 0.7 * m.sigma.back(), "high partials were not less damped");
            require(piano_mvp_v4::kBridgeMassCornerHz > 1900 && piano_mvp_v4::kBridgeMassCornerHz < 2050,
                    "bridge-mass corner moved");
            piano_mvp_v4::BridgeMassTransmission lp; lp.prepare(48000);
            double l = 1, r = 1; for (int i = 0; i < 48000; ++i) { l = r = 1; lp.process(l, r); }
            require(std::abs(l - 1) < 1e-9, "bridge-mass path has DC gain other than one");
            piano_mvp_v4::Piano before, after;
            require(before.setBridgeMassLoading(false) && before.setSaturatingStringLoss(false) && before.prepare(48000), "0.4.8 control failed");
            require(after.prepare(48000), "0.4.9 prepare failed");
            for (int key : {45, 60}) {
                before.reset(); before.noteOn(key, 0.47); after.reset(); after.noteOn(key, 0.47);
                const double b = brightness(before, 12000), a = brightness(after, 12000);
                require(a < 0.75 * b, "0.4.9 attack is not darker above 2 kHz than 0.4.8");
            }
            require(!after.setBridgeMassLoading(false) && !after.setSaturatingStringLoss(false), "0.4.9 flags changed after preparation");
        }
        {   // 0.5.0-T1 polyphony: runtime voice limit, 88 never steals.
            piano_mvp_v4::Piano many;
            require(!many.setVoiceLimit(0) && !many.setVoiceLimit(89), "voice limit range not enforced");
            require(many.setVoiceLimit(88) && many.prepare(48000), "88-voice prepare failed");
            for (int key = 21; key <= 108; ++key) many.noteOn(key, 0.3);
            require(many.activeVoices() == 88 && many.voiceSteals() == 0, "88-voice limit stole voices");
            require(energy(many, 2400) > 0, "88-voice chord silent");
            require(!many.setVoiceLimit(12), "voice limit changed after preparation");
            piano_mvp_v4::Piano few;
            require(few.setVoiceLimit(3) && few.prepare(48000), "3-voice prepare failed");
            for (int key = 60; key < 66; ++key) few.noteOn(key, 0.5);
            require(few.activeVoices() == 3 && few.voiceSteals() == 3, "3-voice limit did not steal");
            piano_mvp_v4::Piano dflt; require(dflt.prepare(48000), "default prepare failed");
            for (int key = 40; key < 60; ++key) dflt.noteOn(key, 0.5);
            require(dflt.activeVoices() == piano_mvp_v4::Piano::maxVoices, "default limit is not 12");
        }
        {   // 0.5.0-T2 una corda (CC67): fewer strings struck and softer felt, set at launch only.
            const auto run = [](int key, bool pedal, bool late) {
                piano_mvp_v4::Piano q; require(q.prepare(48000), "una prepare failed");
                std::uint8_t on[3]{0xb0, 67, 127}, off[3]{0xb0, 67, 0};
                if (pedal && !late) piano_mvp_v4::dispatchMidi(q, on, 3);
                q.noteOn(key, 0.7);
                double e = 0;
                if (pedal && late) { e += energy(q, 20); piano_mvp_v4::dispatchMidi(q, on, 3); }
                e += energy(q, 12000 - (pedal && late ? 20 : 0));
                require(q.softPedalDown() == pedal, "soft pedal state wrong");
                piano_mvp_v4::dispatchMidi(q, off, 3);
                require(!q.softPedalDown(), "CC67 release ignored");
                return e;
            };
            for (int key : {24, 40, 60}) {   // 1-, 2- and 3-string keys
                const double plain = run(key, false, false), una = run(key, true, false);
                require(una > 1e-12 && std::isfinite(una), "una corda silenced the key");
                require(std::abs(una / plain - 1) > 0.02, "una corda has no measurable effect");
                if (key >= 40) require(una < plain, "una corda is not softer for multi-string keys");
            }
            // A pedal pressed after the strike must not touch a hammer already in flight.
            require(std::abs(run(60, true, true) / run(60, false, false) - 1) < 1e-12, "una corda moved a hammer in flight");
            piano_mvp_v4::Piano off; require(off.setUnaCorda(false) && off.prepare(48000), "una-off prepare failed");
            off.softPedal(true); require(!off.softPedalDown(), "disabled una corda engaged");
        }
        {   // 0.5.0-T3 undamped treble: keys >= F#6 (90) ring after note-off, with or without sustain.
            // The board keeps radiating after a damped release, so compare the first 0.1-0.2 s after
            // note-off (string energy still dominant) and whether the string voice survives 1 s.
            const auto tail = [](int key, bool undamped, int& active) {
                piano_mvp_v4::Piano q; require(q.setUndampedTreble(undamped) && q.prepare(48000), "treble prepare failed");
                q.noteOn(key, 0.7); energy(q, 4800); q.noteOff(key); energy(q, 4800);
                const double e = energy(q, 4800);
                energy(q, 38400); active = q.activeVoices();
                return e;
            };
            int a1, a2, a3, a4;
            require(piano_mvp_v4::Piano::firstUndampedKey == 90, "undamped boundary moved");
            const double und = tail(96, true, a1), dmp = tail(96, false, a2);
            require(und > 1.5 * dmp, "undamped key still damped on note-off");
            require(a1 == 1 && a2 == 0, "undamped key did not outlive the damper release");
            require(tail(89, true, a3) == tail(89, false, a4) && a3 == a4, "damped key behaviour changed");
            piano_mvp_v4::Piano q; require(q.prepare(48000), "prepare failed");
            q.sustain(true); q.sustain(false);
            q.noteOn(100, 0.7); q.noteOff(100); energy(q, 4800);
            require(q.activeVoices() == 1, "undamped key retired at note-off");
        }
        std::cout << "PASS: source anchors, tension, loss projection, extrapolation; 88 keys at 3 sample rates, unisons, velocity, sustain, release, voice limit, reset, 0.5.0 polyphony/una corda/undamped treble\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
