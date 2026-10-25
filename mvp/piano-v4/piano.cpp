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
        if (piano_mvp_v2::compilePresetString(preset, fs, v.modes) != pme::Status::Ok)
            return false;
        if (pme::compileBridgeTerms(v.modes, pme::BridgeEnd::AtL, v.terms) != pme::Status::Ok)
            return false;
        pme::exciters::StruckNoteDesc d;
        d.strings = {&v.modes};
        d.hammer = preset.hammer;
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
    pedal_ = false; clock_ = 0; numericalFaults_ = 0; peakBridgeMotion_ = 0;
}
int Piano::activeVoices() const noexcept {
    int n = 0; for (const auto& v : voices_) n += v.active; return n;
}
void Piano::noteOn(int key, double velocity) noexcept {
    if (!ready_ || key < 21 || key > 108 || !std::isfinite(velocity)) return;
    if (velocity <= 0) { noteOff(key); return; }
    auto& v = voices_[key - 21];
    if (!v.active && activeVoices() >= 16) {
        Voice* oldest = nullptr;
        for (auto& x : voices_) if (x.active && (!oldest || x.age < oldest->age)) oldest = &x;
        if (oldest) { oldest->active = oldest->held = false; oldest->note.reset(); oldest->bridge.reset(); }
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
            for (std::size_t n = 0; n < v.note.modeCount(0); ++n) {
                s.q()[n] *= releaseStep_; s.u()[n] *= releaseStep_;
            }
        }
        const double w = feedback_ ? v.bridge.wV() : 0;
        peakBridgeMotion_ = std::max(peakBridgeMotion_,std::abs(w));
        double force = pme::bridge::bridgeForce<double>(v.terms, s.q(), w);
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
