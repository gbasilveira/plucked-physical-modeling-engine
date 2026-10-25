// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "preset.hpp"
#include "bridge_model.hpp"
#include <algorithm>
#include <cmath>

namespace piano_mvp_v4 {
bool Piano::prepare(double fs) {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000) return false;
    fs_ = fs;
    releaseStep_ = std::exp(-35.0 / fs);
    for (int i = 0; i < 88; ++i) {
        auto& v = voices_[i];
        v.pan = 0.2 + 0.6 * i / 87.0;
        const auto preset = piano_mvp_v2::parametersForKey(i + 21);
        pme::exciters::StruckNoteDesc d;
        const int count = unisons_ ? stringsForKey(i + 21) : 1;
        for (int j = 0; j < count; ++j) {
            auto string = preset;
            // Small symmetric unison spread: an explicit tuning assumption, not
            // measured data. Compile each string, including its own bridge terms.
            const double cents = count == 1 ? 0 : (j - (count - 1) * 0.5) * 0.8;
            string.string.f1Target *= std::exp2(cents / 1200.0);
            if (piano_mvp_v2::compilePresetString(string, fs, v.modes[j]) != pme::Status::Ok)
                return false;
            if (feedback_ && residualLoss_ && applyResidualBridgeLoss(v.modes[j],v.pan)!=pme::Status::Ok)
                return false;
            if (pme::compileBridgeTerms(v.modes[j], pme::BridgeEnd::AtL, v.terms[j]) != pme::Status::Ok)
                return false;
            d.strings.push_back(&v.modes[j]);
        }
        d.hammer = preset.hammer;
        // Treat the tabulated felt stiffness as the whole contact patch and
        // distribute it across strings. Keep one full-mass shared hammer.
        d.hammer.K /= count;
        if (feedback_ && v.bridge.configure({},localBridgeModel(v.pan),fs) != pme::Status::Ok)
            return false;
        if (v.note.configure(d,fs,feedback_ ? &v.bridge : nullptr) != pme::Status::Ok) return false;
    }
    if (!soundboard_.prepare(fs)) return false;
    ready_ = true; reset(); return true;
}
void Piano::reset() noexcept {
    for (auto& v : voices_) {
        v.note.reset(); v.bridge.reset(); v.active = v.held = false; v.age = v.quiet = 0; v.release = 1;
    }
    soundboard_.reset();
    pedal_ = false; clock_ = 0; numericalFaults_ = 0; voiceSteals_ = 0; peakBridgeMotion_ = 0;
}
int Piano::activeVoices() const noexcept {
    int n = 0; for (const auto& v : voices_) n += v.active; return n;
}
void Piano::noteOn(int key, double velocity) noexcept {
    if (!ready_ || key < 21 || key > 108 || !std::isfinite(velocity)) return;
    if (velocity <= 0) { noteOff(key); return; }
    auto& v = voices_[key - 21];
    if (!v.active && activeVoices() >= maxVoices) {
        Voice* oldest = nullptr;
        for (auto& x : voices_) if (x.active && (!oldest || x.age < oldest->age)) oldest = &x;
        if (oldest) {
            ++voiceSteals_;
            oldest->active = oldest->held = false; oldest->note.reset(); oldest->bridge.reset();
        }
    }
    if (!v.active) { v.note.reset(); v.bridge.reset(); }
    if (v.note.launch(std::clamp(velocity, 0.001, 1.0), 0) != pme::Status::Ok) return;
    v.active = v.held = true; v.release = 1; v.age = ++clock_; v.quiet = 0;
}
void Piano::noteOff(int key) noexcept {
    if (key >= 21 && key <= 108) voices_[key - 21].held = false;
}
void Piano::sustain(bool down) noexcept { pedal_ = down; }
void Piano::sample(float& left, float& right) noexcept {
    double l = 0, r = 0;
    if (!ready_) { left = right = 0; return; }
    for (auto& v : voices_) {
        if (!v.active) continue;
        v.note.step();
        auto& s = v.note.segment();
        if (!v.held && !pedal_) {
            v.release *= releaseStep_;
            // MVP diagonal damper: remove energy from string coordinates only.
            for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                const auto off = v.note.modeOffset(j);
                for (std::size_t n = 0; n < v.note.modeCount(j); ++n) {
                    s.q()[off+n] *= releaseStep_; s.u()[off+n] *= releaseStep_;
                }
            }
        }
        const double w = feedback_ ? v.bridge.wV() : 0;
        peakBridgeMotion_ = std::max(peakBridgeMotion_,std::abs(w));
        double force = 0;
        for (std::size_t j = 0; j < v.note.stringCount(); ++j)
            force += pme::bridge::bridgeForce<double>(v.terms[j], s.q() + v.note.modeOffset(j), w);
        if (!std::isfinite(force)) {
            ++numericalFaults_; v.note.reset(); v.bridge.reset(); v.active = false; continue;
        }
        l += force * (1 - v.pan); r += force * v.pan;
        if (std::abs(force) < 1e-7 && !v.note.inFlight()) ++v.quiet; else v.quiet = 0;
        if (v.release < 1e-6 || v.quiet > static_cast<std::uint64_t>(fs_)) {
            v.active = false; v.note.reset(); v.bridge.reset();
        }
    }
    double pressureL, pressureR;
    soundboard_.sample(l, r, pressureL, pressureR);
    // Fixed linear gain; pressure is never mixed with raw string force.
    left = static_cast<float>(0.05 * pressureL);
    right = static_cast<float>(0.05 * pressureR);
    if (!std::isfinite(left) || !std::isfinite(right)) {
        ++numericalFaults_; left = right = 0; soundboard_.reset();
    }
}
}
