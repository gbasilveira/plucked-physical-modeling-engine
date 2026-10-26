// SPDX-License-Identifier: MIT
// 0.6 tool helpers: event lists, scenarios, rendering through Piano, WAV output.
#pragma once
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pb {
using piano_mvp_v4::Piano;
constexpr int kFs = 48000;
struct Ev { int frame; std::array<std::uint8_t, 3> d; };
struct Stereo { std::vector<float> l, r; };

inline std::vector<Ev> readEvents(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<Ev> e; int f, s, a, b;
    while (in >> f >> s >> a >> b) e.push_back({f, {std::uint8_t(s), std::uint8_t(a), std::uint8_t(b)}});
    return e;
}
// Events of [from, to) seconds of a file, shifted to start at 0; the controller state at `from` is replayed at frame 0.
inline std::vector<Ev> excerpt(const std::vector<Ev>& all, double from, double to) {
    const int f0 = int(from * kFs), f1 = int(to * kFs);
    std::vector<Ev> out; std::array<int, 128> ccLast; ccLast.fill(-1);
    for (const auto& e : all) if (e.frame < f0 && (e.d[0] & 0xf0) == 0xb0) { ccLast[e.d[1]] = e.d[2]; }
    for (int cc : {64, 67}) if (ccLast[cc] >= 0) out.push_back({0, {176, std::uint8_t(cc), std::uint8_t(ccLast[cc])}});
    for (const auto& e : all) if (e.frame >= f0 && e.frame < f1) out.push_back({e.frame - f0, e.d});
    return out;
}
// Chromatic sweep: pedalled, every key 21..108 once, velocities cycling 0.3 / 0.6 / 0.9, 0.4 s apart, held 1.2 s.
inline std::vector<Ev> sweepEvents() {
    std::vector<Ev> e; e.push_back({0, {176, 64, 127}});
    for (int k = 21; k <= 108; ++k) {
        const int on = 100 + (k - 21) * int(0.4 * kFs), vel = std::array<int, 3>{38, 76, 114}[(k - 21) % 3];
        e.push_back({on, {144, std::uint8_t(k), std::uint8_t(vel)}});
        e.push_back({on + int(1.2 * kFs), {128, std::uint8_t(k), 0}});
    }
    std::stable_sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
    return e;
}
// Single note, no pedal.
inline std::vector<Ev> noteEvents(int key, double velocity, double hold) {
    return {{10, {144, std::uint8_t(key), std::uint8_t(std::lround(velocity * 127))}},
            {10 + int(hold * kFs), {128, std::uint8_t(key), 0}}};
}
inline void configure(Piano& p, Piano::Quality q, Piano::Radiation r, int threads = 0, double tol = piano_mvp_v4::kBakedDefaultTolerance) {
    if (!p.setWorkerThreads(threads) || !p.setBakedTolerance(tol) || !p.prepare(kFs)) throw std::runtime_error("prepare failed");
    p.setQuality(q); p.setRadiation(r); p.setImperfection(1.0);
}
// Render `frames` frames through the block interface (bit-identical to sample()); optional hook(frame) after each sample
// when `perSample` (uses sample()).
inline Stereo render(Piano& p, const std::vector<Ev>& events, int frames, const std::function<void(int)>& hook = {}, int block = 128) {
    pme::simd::ScopedFlushDenormals denormals;
    Stereo out; out.l.assign(frames, 0.f); out.r.assign(frames, 0.f);
    std::size_t cur = 0;
    if (hook) {
        for (int i = 0; i < frames; ++i) {
            while (cur < events.size() && events[cur].frame <= i) { piano_mvp_v4::dispatchMidi(p, events[cur].d.data(), 3); ++cur; }
            p.sample(out.l[i], out.r[i]); hook(i);
        }
        return out;
    }
    for (int i0 = 0; i0 < frames; i0 += block) {
        const int n = std::min(block, frames - i0);
        p.blockBegin(n);
        for (int i = i0; i < i0 + n; ++i) {
            while (cur < events.size() && events[cur].frame <= i) { piano_mvp_v4::dispatchMidi(p, events[cur].d.data(), 3); ++cur; }
            p.blockStep();
        }
        p.blockEnd(out.l.data() + i0, out.r.data() + i0);
    }
    return out;
}
inline void put(std::ostream& f, std::uint32_t x, int bytes) { for (int i = 0; i < bytes; ++i) f.put(char((x >> (8 * i)) & 255)); }
// 16-bit PCM (listening) or 32-bit float (analysis) stereo WAV.
inline void writeWav(const std::string& path, const Stereo& s, bool pcm16 = true) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    const std::uint32_t n = std::uint32_t(s.l.size()), bps = pcm16 ? 2 : 4;
    f.write("RIFF", 4); put(f, 36 + n * 2 * bps, 4); f.write("WAVEfmt ", 8); put(f, 16, 4); put(f, pcm16 ? 1 : 3, 2); put(f, 2, 2);
    put(f, kFs, 4); put(f, kFs * 2 * bps, 4); put(f, 2 * bps, 2); put(f, bps * 8, 2); f.write("data", 4); put(f, n * 2 * bps, 4);
    for (std::uint32_t i = 0; i < n; ++i)
        for (float x : {s.l[i], s.r[i]}) {
            if (pcm16) put(f, std::uint16_t(std::int16_t(std::lround(std::clamp(x, -1.f, 1.f) * 32767))), 2);
            else f.write(reinterpret_cast<const char*>(&x), 4);
        }
}
inline double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
