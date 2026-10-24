// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace piano_mvp {
bool Piano::prepare(double fs) {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000) return false;
    fs_ = fs;
    releaseStep_ = std::exp(-35.0 / fs);
    for (int i = 0; i < 88; ++i) {
        auto& v = voices_[i];
        const double f = 440.0 * std::exp2((i + 21 - 69) / 12.0);
        pme::StringParams p;
        p.L = std::clamp(0.65 * std::pow(261.625565 / f, 0.75), 0.075, 2.0);
        p.dCore = p.dOuter = 0.00085;
        p.rhoCore = 7850; p.ECore = 200e9; p.f1Target = f;
        if (f < 130) {
            p.dOuter = 0.00085 + 0.003 * (1.0 - f / 130.0);
            p.rhoWind = 8900; p.packing = 0.85; p.epsWind = 0.05;
        }
        p.b1 = 0.25 + 0.0003 * f; p.b2 = 0.004; p.b3 = 3e-9;
        if (pme::compileString(p, fs, std::min(12000.0, fs * 0.42), {}, v.modes) != pme::Status::Ok)
            return false;
        if (pme::compileBridgeTerms(v.modes, pme::BridgeEnd::AtL, v.terms) != pme::Status::Ok)
            return false;
        pme::exciters::StruckNoteDesc d;
        d.strings = {&v.modes};
        d.hammer.mass = std::clamp(0.008 * std::sqrt(261.625565 / f), 0.003, 0.012);
        d.hammer.K = 4.5e9; d.hammer.p = 2.5; d.hammer.muHC = 0.5;
        d.hammer.width = std::min(0.01, p.L * 0.04); d.hammer.strikeRatio = 0.125;
        if (v.note.configure(d, fs, nullptr) != pme::Status::Ok) return false;
        v.pan = 0.2 + 0.6 * i / 87.0;
    }
    // A deliberately simple, one-way resonant body, not a measured piano soundboard.
    constexpr double freq[] = {110, 227, 487, 1103};
    for (int i = 0; i < 4; ++i) {
        auto& b = body_[i];
        const double r = std::exp(-(18.0 + 8.0 * i) / fs);
        b.a = 2 * r * std::cos(2 * std::numbers::pi * freq[i] / fs);
        b.r2 = r * r; b.gain = (1 - r) * 0.12;
    }
    ready_ = true; reset(); return true;
}
void Piano::reset() noexcept {
    for (auto& v : voices_) {
        v.note.reset(); v.active = v.held = false; v.age = v.quiet = 0; v.release = 1;
    }
    for (auto& b : body_) b.y1 = b.y2 = 0;
    pedal_ = false; clock_ = 0; dcX_ = dcY_ = 0;
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
        if (oldest) { oldest->active = oldest->held = false; oldest->note.reset(); }
    }
    if (!v.active) v.note.reset();
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
        double force = pme::bridge::bridgeForce<double>(v.terms, s.q(), 0.0);
        if (!std::isfinite(force)) { v.note.reset(); v.active = false; continue; }
        l += force * (1 - v.pan); r += force * v.pan;
        if (std::abs(force) < 1e-7 && !v.note.inFlight()) ++v.quiet; else v.quiet = 0;
        if (v.release < 1e-6 || v.quiet > static_cast<std::uint64_t>(fs_)) {
            v.active = false; v.note.reset();
        }
    }
    const double input = (l + r) * 0.5;
    double wet = 0;
    for (auto& b : body_) {
        const double y = b.gain * input + b.a * b.y1 - b.r2 * b.y2;
        b.y2 = b.y1; b.y1 = y; wet += y;
    }
    // DC removal on the body path, then a fixed, bounded output transfer.
    const double dc = wet - dcX_ + std::exp(-2 * std::numbers::pi * 15 / fs_) * dcY_;
    dcX_ = wet; dcY_ = dc;
    left = static_cast<float>(std::tanh(0.04 * (l + dc)));
    right = static_cast<float>(std::tanh(0.04 * (r + dc)));
}
}
