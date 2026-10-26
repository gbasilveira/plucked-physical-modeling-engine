// SPDX-License-Identifier: MIT
// 0.6 tests of the baked radiation (README "Update 0.6"). Each test is named by what it checks.
//  B1 error bound: Full with the baked bank against Full with the modal bank: rms error <= -80 dB re signal on the Chopin
//     excerpt and on single notes C2/C4/C6 at three velocities; octave-band errors are reported.
//  B2 determinism: baked output identical for sample() and blocks of 64/100/256 and for 0..3 helper threads.
//  B3 real-time safety: no heap allocation (operator new) during blockBegin/blockStep/blockEnd, including radiation switches.
//  B4 switching: Modal <-> Baked <-> Auto while a chord sounds stays finite without faults; Auto is Baked in Live and Modal in Full.
//  B5 bank structure: 246 zero-weight modes dropped (exact), light ports drive a prefix, padding to whole groups of 32.
//  B6 partitioned convolution (study tool): matches a direct convolution; independent of how the stream is split.
#include "common.hpp"
#include "part_conv.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <random>

static std::atomic<bool> gArmed{false};
static std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) { if (gArmed.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gArmed.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using pb::Piano;
using Rad = Piano::Radiation;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

static std::uint64_t fnv(std::uint64_t h, float x) {
    std::uint32_t b; std::memcpy(&b, &x, 4);
    for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255; h *= 1099511628211ull; }
    return h;
}
static std::uint64_t hashOf(const pb::Stereo& s) { std::uint64_t h = 1469598103934665603ull; for (std::size_t i = 0; i < s.l.size(); ++i) h = fnv(fnv(h, s.l[i]), s.r[i]); return h; }
static double relRms(const pb::Stereo& a, const pb::Stereo& b, double& peak) {
    double e = 0, s = 0; peak = 0;
    for (std::size_t i = 0; i < a.l.size(); ++i)
        for (auto [x, y] : {std::pair{a.l[i], b.l[i]}, std::pair{a.r[i], b.r[i]}}) { const double d = double(x) - y; e += d * d; s += double(x) * x; peak = std::max(peak, std::abs(d)); }
    return s > 0 ? std::sqrt(e / s) : 0;
}
// Octave-band error: band energy of (a-b) over band energy of a, in dB, for bands centred 63 Hz .. 8 kHz (mono mix).
static void octaveErrors(const pb::Stereo& a, const pb::Stereo& b, std::vector<double>& centre, std::vector<double>& errDb) {
    std::size_t n = 1; while (n < a.l.size()) n <<= 1;
    pb::Fft fft; fft.prepare(int(n));
    std::vector<std::complex<float>> x(n), d(n);
    for (std::size_t i = 0; i < a.l.size(); ++i) { x[i] = 0.5f * (a.l[i] + a.r[i]); d[i] = 0.5f * ((a.l[i] - b.l[i]) + (a.r[i] - b.r[i])); }
    fft.run(x.data(), false); fft.run(d.data(), false);
    centre.clear(); errDb.clear();
    for (double c = 63; c <= 8000; c *= 2) {
        double ex = 0, ed = 0;
        for (std::size_t k = 1; k < n / 2; ++k) { const double f = double(k) * pb::kFs / double(n); if (f >= c / std::sqrt(2.0) && f < c * std::sqrt(2.0)) { ex += std::norm(x[k]); ed += std::norm(d[k]); } }
        centre.push_back(c); errDb.push_back(ex > 0 ? 10 * std::log10(ed / ex + 1e-40) : -999);
    }
}
static pb::Stereo run(Piano::Quality q, Rad r, int threads, const std::vector<pb::Ev>& ev, int frames, int block = 128, bool perSample = false) {
    Piano p; pb::configure(p, q, r, threads);
    if (threads > 0) p.setActiveWorkers(threads);
    return pb::render(p, ev, frames, perSample ? std::function<void(int)>([](int) {}) : std::function<void(int)>{}, block);
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        // ---- B1 ----
        {
            const auto chopin = pb::excerpt(pb::readEvents(std::string(PIANO_SOURCE_DIR) + "/listening/chopin-op28-no4.events"), 40, 46);
            const int frames = 8 * pb::kFs;
            const auto m = run(Piano::Quality::Full, Rad::Modal, 1, chopin, frames), b = run(Piano::Quality::Full, Rad::Baked, 1, chopin, frames);
            double pk; const double e = relRms(m, b, pk);
            std::vector<double> c, d; octaveErrors(m, b, c, d);
            std::cout << "B1 Chopin 6 s excerpt (Full): baked vs modal rms error " << 20 * std::log10(e + 1e-30) << " dB re signal, max abs diff " << pk << "; octave bands (dB):";
            for (std::size_t i = 0; i < c.size(); ++i) std::cout << " " << c[i] << ":" << d[i];
            std::cout << "\n";
            require(e < 1e-4, "B1 Chopin: baked error above -80 dB");
            for (double x : d) require(x < -60, "B1 Chopin: an octave band error above -60 dB");
            for (int key : {36, 60, 84})
                for (double vel : {0.3, 0.6, 0.9}) {
                    const auto ev = pb::noteEvents(key, vel, 3.0);
                    const int nf = 5 * pb::kFs;
                    const auto mm = run(Piano::Quality::Full, Rad::Modal, 1, ev, nf), bb = run(Piano::Quality::Full, Rad::Baked, 1, ev, nf);
                    double p2; const double e2 = relRms(mm, bb, p2);
                    std::cout << "B1 note " << key << " v" << vel << ": " << 20 * std::log10(e2 + 1e-30) << " dB\n";
                    require(e2 < 1e-4, "B1 note: baked error above -80 dB");
                }
        }
        // ---- B2 ----
        {
            std::vector<pb::Ev> script{{0, {176, 64, 127}}};
            for (int r = 0; r < 4; ++r) for (int k : {38, 45, 52, 57, 60, 64, 67, 72, 79}) script.push_back({1 + r * 3000 + (k % 5) * 30, {144, std::uint8_t(k + (r % 2) * 2), std::uint8_t(95 + r * 3)}});
            const int frames = 2 * pb::kFs;
            const auto ref = hashOf(run(Piano::Quality::Live, Rad::Baked, 0, script, frames, 128, true));
            for (int t : {0, 1, 2, 3})
                for (int blk : {64, 100, 256}) require(hashOf(run(Piano::Quality::Live, Rad::Baked, t, script, frames, blk)) == ref, "B2 baked output depends on block size or threads");
            const int shortFrames = pb::kFs / 2;
            const auto refFull = hashOf(run(Piano::Quality::Full, Rad::Baked, 0, script, shortFrames, 128, true));
            for (int t : {1, 3}) require(hashOf(run(Piano::Quality::Full, Rad::Baked, t, script, shortFrames, 100)) == refFull, "B2 Full+Baked differs");
            std::cout << "B2 PASS: baked output identical for sample(), blocks 64/100/256 and 0..3 helper threads (Live 2 s, Full 0.5 s)\n";
            // Auto = Baked in Live, Modal in Full
            require(hashOf(run(Piano::Quality::Live, Rad::Auto, 1, script, frames, 128)) == ref, "B4 Live Auto is not Baked");
            require(hashOf(run(Piano::Quality::Full, Rad::Auto, 1, script, shortFrames, 128)) == hashOf(run(Piano::Quality::Full, Rad::Modal, 0, script, shortFrames, 128, true)), "B4 Full Auto is not Modal");
            require(ref != hashOf(run(Piano::Quality::Live, Rad::Modal, 1, script, frames, 128)), "B4 Modal and Baked should differ in the last bits");
        }
        // ---- B3 + B4 ----
        {
            Piano p; pb::configure(p, Piano::Quality::Live, Rad::Auto, 1);
            std::vector<float> l(128), r(128);
            for (int k = 40; k < 60; k += 3) p.noteOn(k, 0.8);
            p.sustain(true);
            long allocs = 0; double peak = 0;
            const Rad seq[] = {Rad::Auto, Rad::Modal, Rad::Baked, Rad::Auto, Rad::Baked, Rad::Modal, Rad::Auto};
            for (int blk = 0; blk < 7 * 20; ++blk) {
                p.setRadiation(seq[blk / 20]);
                if (blk == 60) p.setQuality(Piano::Quality::Full);
                if (blk == 100) p.setQuality(Piano::Quality::Live);
                gAllocs = 0; gArmed = true;
                p.blockBegin(128);
                for (int i = 0; i < 128; ++i) p.blockStep();
                p.blockEnd(l.data(), r.data());
                gArmed = false; allocs += gAllocs.load();
                for (int i = 0; i < 128; ++i) { require(std::isfinite(l[i]) && std::isfinite(r[i]), "B4 non-finite after a radiation switch"); peak = std::max(peak, double(std::abs(l[i]))); }
            }
            require(p.numericalFaults() == 0, "B4 numerical faults after switching");
            require(allocs == 0, "B3 heap allocation during blockBegin/blockStep/blockEnd");
            std::cout << "B3 PASS: 0 heap allocations in 140 blocks with radiation and quality switches (operator new hook)\n";
            std::cout << "B4 PASS: Modal/Baked/Auto switched while 7 notes sound, finite, no faults, peak " << peak << "\n";
        }
        // ---- B5 ----
        {
            Piano p; pb::configure(p, Piano::Quality::Live, Rad::Baked, 0);
            const auto& b = p.board();
            { float a, c; p.sample(a, c); }   // the setting is taken at the first sample
            require(b.baked() && b.bakedModes() == 266, "B5 expected 266 radiating modes kept");
            require(b.bakedPaddedModes() % 32 == 0 && b.bakedLightModes() % 32 == 0 && b.bakedLightModes() < b.bakedPaddedModes(), "B5 padding");
            Piano q; require(q.setBakedTolerance(1e-6) && q.setWorkerThreads(0) && q.prepare(pb::kFs), "B5 prepare");
            require(q.board().bakedModes() == 261, "B5 tolerance 1e-6 should keep 261 modes");
            require(!q.setBakedTolerance(0.1), "B5 tolerance after prepare must be refused");
            std::cout << "B5 PASS: " << b.bakedModes() << " modes kept of 512 (246 zero-weight modes dropped), " << b.bakedPaddedModes() << " slots with padding, "
                      << b.bakedLightModes() << " slots driven by unstruck strings; tolerance 1e-6 keeps " << q.board().bakedModes() << "\n";
        }
        // ---- B6 ----
        {
            std::mt19937 rng(7); std::uniform_real_distribution<float> u(-1.f, 1.f);
            const int inputs = 4;
            for (int P : {64, 128}) {
                const std::size_t len = 3000;
                std::vector<std::vector<float>> irL(inputs, std::vector<float>(len)), irR = irL;
                for (int c = 0; c < inputs; ++c) for (std::size_t n = 0; n < len; ++n) { const float env = std::exp(-float(n) / 600.f); irL[c][n] = u(rng) * env; irR[c][n] = u(rng) * env; }
                std::vector<const float*> pl, pr; for (int c = 0; c < inputs; ++c) { pl.push_back(irL[c].data()); pr.push_back(irR[c].data()); }
                pb::PartConv conv; require(conv.prepare(P, inputs, pl, pr, len), "B6 prepare");
                const int frames = 12000;
                std::vector<std::vector<float>> in(inputs, std::vector<float>(frames, 0.f));
                for (int c = 0; c < inputs; ++c) for (int n = 0; n < frames; ++n) if ((n / 700 + c) % 3 != 2 && n < 9000) in[c][n] = u(rng);   // gaps, so inputs go idle and return
                std::vector<double> refL(frames, 0.0), refR(frames, 0.0);
                for (int c = 0; c < inputs; ++c) for (int n = 0; n < frames; ++n) if (in[c][n] != 0.f) for (std::size_t k = 0; k < len && n + k < std::size_t(frames); ++k) { refL[n + k] += double(in[c][n]) * irL[c][k]; refR[n + k] += double(in[c][n]) * irR[c][k]; }
                double e = 0, s = 0;
                for (int n = 0; n < frames; ++n) {
                    int ids[inputs]; float g[inputs]; int cnt = 0;
                    for (int c = 0; c < inputs; ++c) if (in[c][n] != 0.f) { ids[cnt] = c; g[cnt++] = in[c][n]; }
                    double l, r; conv.step(ids, g, cnt, l, r);
                    e += (l - refL[n]) * (l - refL[n]) + (r - refR[n]) * (r - refR[n]); s += refL[n] * refL[n] + refR[n] * refR[n];
                }
                const double rel = std::sqrt(e / s);
                std::cout << "B6 P " << P << ": convolution vs direct rel rms " << 20 * std::log10(rel + 1e-30) << " dB\n";
                require(rel < 1e-5, "B6 convolution differs from direct convolution");
            }
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
