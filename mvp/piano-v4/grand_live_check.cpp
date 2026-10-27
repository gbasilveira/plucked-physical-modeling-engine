// SPDX-License-Identifier: MIT
// Piano 0.7c tests (Body Grand). Named by what they check.
//  G7-2 no allocation in process across Body / Mic / Lid / quality switches (global operator new counted inside the audio calls).
//  G7-3 determinism: sample() and blocks of 37/128/256, 0..3 helper threads, give identical bits (Grand, with Mic/Lid switches).
//  G7-4 Body Grand drives the board exactly as piano-grand-render does: bridge-force records of the pair-less Grand piano equal those of a
//       Classic piano with the 0.7b board table installed through the hook (bit-exact), Full quality.
//  G7-5 Body Classic is untouched: the pair's Classic output equals a plain Piano (bit-exact) and a body switch leaves a sounding note ringing out.
#include "piano.hpp"
#include "piano_pair.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

static std::atomic<long> g_allocs{0};
static std::atomic<bool> g_count{false};
void* operator new(std::size_t n) { if (g_count.load(std::memory_order_relaxed)) g_allocs.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (g_count.load(std::memory_order_relaxed)) g_allocs.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace piano_mvp_v4;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
constexpr int fs = 48000;
struct Ev { int frame; std::uint8_t d[3]; };
static std::vector<Ev> script() {
    std::vector<Ev> e{{0, {0xb0, 64, 127}}};
    for (int r = 0; r < 4; ++r) for (int k : {36, 45, 52, 60, 64, 72, 84})
        e.push_back({1 + r * 2500 + (k % 5) * 40, {0x90, std::uint8_t(k + (r % 2) * 2), std::uint8_t(90 + r * 4)}});
    e.push_back({14000, {0xb0, 64, 0}});
    return e;
}
static std::uint64_t fnv(std::uint64_t h, float x) { std::uint32_t b; std::memcpy(&b, &x, 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255; h *= 1099511628211ull; } return h; }
static std::uint64_t renderGrand(Piano::Quality q, int threads, int block, int frames, bool switches) {
    Piano p; require(p.setBody(Piano::Body::Grand) && p.setWorkerThreads(threads) && p.prepare(fs), "grand prepare (embedded table?)");
    p.setQuality(q); p.setActiveWorkers(threads);
    const auto s = script(); std::size_t c = 0; std::uint64_t h = 1469598103934665603ull;
    std::vector<float> l(Piano::maxBlockFrames), r(Piano::maxBlockFrames);
    for (int i0 = 0; i0 < frames; i0 += block) {
        const int n = std::min(block, frames - i0);
        if (switches && i0 % 2048 == 0) { const int b = i0 / 2048; p.setGrandMic(b % 3); p.setGrandLid((b / 2) % 3); }   // applied at the next block begin / sample
        if (block != 1) p.blockBegin(n);
        for (int i = i0; i < i0 + n; ++i) {
            while (c < s.size() && s[c].frame <= i) { dispatchMidi(p, s[c].d, 3); ++c; }
            if (block == 1) { float a, b; p.sample(a, b); h = fnv(fnv(h, a), b); } else p.blockStep();
        }
        if (block != 1) { p.blockEnd(l.data(), r.data()); for (int k = 0; k < n; ++k) h = fnv(fnv(h, l[k]), r[k]); }
    }
    require(p.numericalFaults() == 0, "faults");
    return h;
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        // G7-2
        {
            PianoPair pair; require(pair.setWorkerThreads(1) && pair.prepare(fs) && pair.grandAvailable(), "pair prepare");
            pair.setQuality(Piano::Quality::Live); pair.warmUp();
            std::vector<float> l(128), r(128); long total = 0;
            for (int blk = 0; blk < 400; ++blk) {
                g_allocs = 0; g_count = true;
                if (blk % 20 == 0) pair.setBody((blk / 20) % 2);
                pair.setGrandMic((blk / 7) % 3); pair.setGrandLid((blk / 11) % 3);
                if (blk % 25 == 0) pair.setQuality(blk / 25 % 3 == 0 ? Piano::Quality::Full : (blk / 25 % 3 == 1 ? Piano::Quality::Balanced : Piano::Quality::Live));
                if (blk % 10 == 3) for (int k = 40; k < 52; k += 4) pair.noteOn(k, 0.8);
                if (blk % 30 == 29) { pair.sustain(true); pair.allNotesOff(); pair.sustain(false); }
                pair.blockBegin(128); for (int i = 0; i < 128; ++i) pair.blockStep(); pair.blockEnd(l.data(), r.data());
                g_count = false; total += g_allocs.load();
                for (int i = 0; i < 128; ++i) require(std::isfinite(l[i]) && std::isfinite(r[i]), "G7-2 non-finite");
            }
            require(total == 0, "G7-2 allocation in the audio calls");
            std::cout << "G7-2 PASS: 400 blocks with Body/Mic/Lid/quality switches and note events, 0 allocations\n";
        }
        // G7-3
        {
            const int frames = fs / 2;
            for (auto q : {Piano::Quality::Live, Piano::Quality::Balanced}) {
                const auto ref = renderGrand(q, 0, 1, frames, true);
                for (int t : {0, 1, 3}) for (int b : {32, 128, 256})
                    if (renderGrand(q, t, b, frames, true) != ref) { std::cerr << "q " << int(q) << " threads " << t << " block " << b << " noswitch-ok? " << (renderGrand(q, t, b, frames, false) == renderGrand(q, 0, 1, frames, false)) << "\n"; throw std::runtime_error("G7-3 block/thread output differs from sample()"); }
            }
            const auto ref = renderGrand(Piano::Quality::Full, 0, 1, fs / 4, true);
            require(renderGrand(Piano::Quality::Full, 3, 128, fs / 4, true) == ref, "G7-3 full differs");
            std::cout << "G7-3 PASS: sample() = blocks (32/128/256), 0..3 threads, Live/Balanced/Full, with Mic/Lid switches\n";
        }
        // G7-4
        {
            std::size_t size = 0; const std::uint8_t* blob = embeddedGrandTable(size); GrandTable t; require(blob && t.parse(blob, size), "table");
            Piano a; require(a.setBody(Piano::Body::Grand) && a.setWorkerThreads(0) && a.prepare(fs), "prepare a");
            setBoardOverride(&t.board);
            Piano b; require(b.setWorkerThreads(0) && b.prepare(fs), "prepare b"); setBoardOverride(nullptr);
            for (Piano* p : {&a, &b}) { p->setQuality(Piano::Quality::Full); p->setVoiceLimit(88); }
            const auto s = script(); std::size_t c = 0; double worst = 0; long compared = 0;
            for (int i = 0; i < fs / 2; ++i) {
                while (c < s.size() && s[c].frame <= i) { dispatchMidi(a, s[c].d, 3); dispatchMidi(b, s[c].d, 3); ++c; }
                float x, y; a.sample(x, y); b.sample(x, y);
                require(a.radiationPortCount() == b.radiationPortCount(), "G7-4 port count");
                for (int k = 0; k < a.radiationPortCount(); ++k) {
                    require(a.radiationPort(k) == b.radiationPort(k), "G7-4 port");
                    worst = std::max(worst, std::fabs(a.radiationDrive(k) - b.radiationDrive(k))); ++compared;
                }
            }
            require(compared > 1000 && worst == 0.0, "G7-4 bridge-force records differ from the offline configuration");
            std::cout << "G7-4 PASS: " << compared << " bridge-force samples bit-identical to the piano-grand-render configuration\n";
        }
        // G7-5
        {
            const auto s = script(); const int frames = fs / 2;
            Piano plain; require(plain.setWorkerThreads(0) && plain.prepare(fs), "prepare plain"); plain.setQuality(Piano::Quality::Live);
            PianoPair pair; require(pair.setWorkerThreads(0) && pair.prepare(fs), "prepare pair"); pair.setQuality(Piano::Quality::Live); pair.setBody(0);
            std::size_t c = 0, d = 0; std::uint64_t h1 = 0, h2 = 0;
            for (int i = 0; i < frames; ++i) {
                while (c < s.size() && s[c].frame <= i) { dispatchMidi(plain, s[c].d, 3); ++c; }
                while (d < s.size() && s[d].frame <= i) { dispatchMidi(pair, s[d].d, 3); ++d; }
                float a, b, x, y; plain.sample(a, b); pair.sample(x, y); h1 = fnv(fnv(h1, a), b); h2 = fnv(fnv(h2, x), y);
            }
            require(h1 == h2, "G7-5 Classic pair output differs from a plain piano");
            // body switch while a note sounds: the left piano keeps ringing, nothing is cut
            pair.reset(); pair.setBody(1); pair.noteOn(60, 0.8);
            double e1 = 0, e2 = 0; float l, r;
            for (int i = 0; i < 4800; ++i) { pair.sample(l, r); e1 += l * l + r * r; }
            pair.setBody(0);
            for (int i = 0; i < 4800; ++i) { pair.sample(l, r); e2 += l * l + r * r; }
            require(pair.leaving() && e2 > 0.05 * e1, "G7-5 sounding note was cut by the body switch");
            std::cout << "G7-5 PASS: Classic pair = plain piano bit-exact; a body switch lets the sounding note ring out\n";
        }
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    return 0;
}
