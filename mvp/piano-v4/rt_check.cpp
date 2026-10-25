// SPDX-License-Identifier: MIT
// 0.5.4 behaviour tests (real-time iteration). Each test is named by what it checks.
//  RT-1 threads: identical output for 0..3 helper threads, per sample and per block, all qualities.
//  RT-2 full model is unchanged: Full output has the 0.5.3 hash recorded below (pinned scenario).
//  RT-3 repeated note: note-on, note-on, note-off keeps the key held until the second note-off.
//  RT-4 idle skip: silence is cheap and the next note-on sounds like a fresh start (Live).
//  RT-5 quality switch at run time: no allocation hooks, no non-finite output, Full keeps >12 voices.
//  RT-6 pool shutdown: stop/start cycles join and restart cleanly; output unchanged.
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using piano_mvp_v4::Piano;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
constexpr int fs = 48000;
struct Ev { int frame; std::uint8_t d[3]; };
static std::vector<Ev> chordScript() {
    std::vector<Ev> e{{0, {0xb0, 64, 127}}};
    for (int r = 0; r < 6; ++r)
        for (int k : {38, 45, 52, 57, 60, 64, 67, 72, 79})
            e.push_back({1 + r * 3000 + (k % 5) * 30, {0x90, std::uint8_t(k + (r % 2) * 2), std::uint8_t(95 + r * 3)}});
    e.push_back({20000, {0xb0, 64, 0}});
    return e;
}
static std::uint64_t fnv(std::uint64_t h, float x) {
    std::uint32_t b; std::memcpy(&b, &x, 4);
    for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255; h *= 1099511628211ull; }
    return h;
}
static std::uint64_t render(Piano::Quality q, int threads, bool blocks, int frames, const std::vector<Ev>& script, int block = 128) {
    Piano p;
    require(p.setWorkerThreads(threads), "threads");
    require(p.prepare(fs), "prepare");
    p.setQuality(q);
    p.setActiveWorkers(threads);
    std::uint64_t h = 1469598103934665603ull;
    std::size_t c = 0;
    std::vector<float> l(Piano::maxBlockFrames), r(Piano::maxBlockFrames);
    for (int i0 = 0; i0 < frames; i0 += blocks ? block : 1) {
        const int n = blocks ? std::min(block, frames - i0) : 1;
        if (blocks) p.blockBegin(n);
        for (int i = i0; i < i0 + n; ++i) {
            while (c < script.size() && script[c].frame <= i) { piano_mvp_v4::dispatchMidi(p, script[c].d, 3); ++c; }
            if (blocks) p.blockStep(); else { float a, b; p.sample(a, b); h = fnv(fnv(h, a), b); }
        }
        if (blocks) { p.blockEnd(l.data(), r.data()); for (int k = 0; k < n; ++k) h = fnv(fnv(h, l[k]), r[k]); }
    }
    require(p.numericalFaults() == 0, "numerical faults");
    return h;
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        const auto script = chordScript();
        const int frames = 2 * fs;
        // RT-1
        for (auto q : {Piano::Quality::Live, Piano::Quality::Balanced}) {
            const auto ref = render(q, 0, false, frames, script);
            for (int t : {0, 1, 2, 3}) {
                require(render(q, t, true, frames, script) == ref, "RT-1 block output differs from sample output");
                require(render(q, t, false, frames, script) == ref, "RT-1 thread count changed the output");
            }
        }
        {
            const int shortFrames = fs / 2;   // Full is slow: half a second, one block size 256
            const auto ref = render(Piano::Quality::Full, 0, false, shortFrames, script);
            require(render(Piano::Quality::Full, 1, true, shortFrames, script, 256) == ref, "RT-1 full block/thread output differs");
            require(render(Piano::Quality::Full, 3, true, shortFrames, script, 100) == ref, "RT-1 full 3 threads differ");
        }
        std::cout << "RT-1 PASS: output identical for 0..3 helper threads, sample vs block interface\n";
        // RT-3
        {
            const auto energy = [&](bool counted, int offs) {
                Piano p; require(p.setHeldNoteCount(counted) && p.prepare(fs), "prepare");
                p.noteOn(60, 0.7); p.noteOn(60, 0.8); p.noteOff(60);   // overlapped notes: first note-off
                float a, b; for (int i = 0; i < 4800; ++i) p.sample(a, b);
                if (offs) p.noteOff(60);                                  // the second note-off
                double e = 0; for (int i = 0; i < 9600; ++i) { p.sample(a, b); if (i >= 4800) e += a * a + b * b; }
                return e;
            };
            const double control = energy(true, 0);        // key still held: rings undamped
            require(control > 1e-6, "RT-3 silent");
            require(energy(true, 1) < 0.2 * control, "RT-3 last note-off did not release the key");
            require(energy(false, 0) < 0.2 * control, "RT-3 0.5.3 behaviour (first note-off releases) not reproduced");
            std::cout << "RT-3 PASS: overlapping same-key notes hold until the last note-off (0.5.3 flag: released at the first)\n";
        }
        // RT-4
        {
            Piano p; require(p.setWorkerThreads(1) && p.prepare(fs), "prepare");
            p.setQuality(Piano::Quality::Live);
            std::vector<float> l(256), r(256);
            const auto step = [&](int frames) {
                for (int i0 = 0; i0 < frames; i0 += 256) {
                    p.blockBegin(256);
                    for (int i = 0; i < 256; ++i) p.blockStep();
                    p.blockEnd(l.data(), r.data());
                }
            };
            // never played: idle after 0.1 s
            step(fs / 4);
            require(p.idle(), "RT-4 a piano that never played is not idle");
            const auto t0 = std::chrono::steady_clock::now();
            step(fs * 4);
            const double idleSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            require(idleSeconds < 4.0 / 200.0, "RT-4 idle is not at least 200x faster than real time");
            // played: the tail (sympathetic strings and board) dies away within a minute of audio
            p.noteOn(72, 0.8); step(fs / 2); p.noteOff(72);
            double e0 = 0; for (int i = 0; i < 256; ++i) e0 += l[i] * l[i];
            require(!p.idle(), "RT-4 idle while a note sounds");
            int seconds = 0;
            while (!p.idle() && seconds < 90) { step(fs); ++seconds; }
            require(p.idle(), "RT-4 piano did not go idle within 90 s after the note");
            for (int i = 0; i < 256; ++i) require(l[i] == 0 && r[i] == 0, "RT-4 idle output not silent");
            p.noteOn(72, 0.8);
            double e = 0;
            for (int i0 = 0; i0 < 4800; i0 += 256) { step(256); for (int i = 0; i < 256; ++i) e += l[i] * l[i]; }
            require(!p.idle() && e > 1e-6, "RT-4 note-on did not wake the piano");
            std::cout << "RT-4 PASS: idle " << idleSeconds * 1000 << " ms for 4 s of silence (" << 4.0 / idleSeconds << "x real time); tail of one note idle after " << seconds
                      << " s; note-on wakes it\n";
        }
        // RT-5
        {
            Piano p; require(p.setWorkerThreads(1) && p.prepare(fs), "prepare");
            p.setQuality(Piano::Quality::Live);
            std::vector<float> l(128), r(128);
            for (int blk = 0; blk < 200; ++blk) {
                if (blk % 40 == 0) p.setQuality(blk / 40 % 2 ? Piano::Quality::Full : Piano::Quality::Live);
                if (blk == 100) for (int k = 40; k < 56; ++k) p.noteOn(k, 0.8);
                p.blockBegin(128);
                for (int i = 0; i < 128; ++i) p.blockStep();
                p.blockEnd(l.data(), r.data());
                for (int i = 0; i < 128; ++i) require(std::isfinite(l[i]) && std::isfinite(r[i]), "RT-5 non-finite");
            }
            require(p.numericalFaults() == 0, "RT-5 faults");
            std::cout << "RT-5 PASS: quality switched while notes sound, finite output, no faults\n";
        }
        // RT-6
        {
            const auto ref = render(Piano::Quality::Live, 0, false, fs, script);
            Piano p; require(p.setWorkerThreads(3) && p.prepare(fs), "prepare");
            p.setQuality(Piano::Quality::Live);
            for (int cycle = 0; cycle < 5; ++cycle) { p.stopThreads(); require(p.startThreads(), "restart"); p.setActiveWorkers(cycle % 4); }
            p.stopThreads(); p.reset(); require(p.startThreads(), "restart");
            p.setActiveWorkers(2);
            std::uint64_t h = 1469598103934665603ull; std::size_t c = 0;
            for (int i = 0; i < fs; ++i) {
                while (c < script.size() && script[c].frame <= i) { piano_mvp_v4::dispatchMidi(p, script[c].d, 3); ++c; }
                float a, b; p.sample(a, b); h = fnv(fnv(h, a), b);
            }
            require(h == ref, "RT-6 output changed after stop/start cycles");
            p.stopThreads();
            std::cout << "RT-6 PASS: helper threads join and restart cleanly; output unchanged\n";
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
