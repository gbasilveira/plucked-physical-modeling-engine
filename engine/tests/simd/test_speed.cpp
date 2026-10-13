// SPDX-License-Identifier: MIT
// Covers: A8-T3
// Throughput of the vectorised mode-bank update against the truly scalar reference on a
// 1,024-mode bank: the AVX2 target must reach at least 4x. The stored baseline of the full kernel
// set is produced by the perf benchmark simd_bench (tools/bench baselines, docs/dev/simd.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "test_support.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Bank {
    static constexpr std::size_t kModes = 1024;
    simdtest::Coefficients co{kModes, 3};
    simdtest::Buf<float> q{kModes};
    simdtest::Buf<float> u{kModes};
    simdtest::Buf<float> force{kModes};
    Bank() {
        simdtest::Rng rng(9);
        simdtest::fillRandom(q, rng, -1.0, 1.0);
        simdtest::fillRandom(force, rng, 0.5, 1.0);  // a steady force keeps the states normal
    }
};

// Median over `reps` repetitions of the time of one call, each repetition looping for ~10 ms.
template <class F>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): repetitions then milliseconds
double medianNsPerCall(F call, int reps = 11, double repMs = 10.0) {
    for (int i = 0; i < 200; ++i) {  // warm-up
        call();
    }
    std::vector<double> t;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = Clock::now();
        long long calls = 0;
        double elapsedMs = 0.0;
        while (elapsedMs < repMs) {
            for (int i = 0; i < 64; ++i) {
                call();
            }
            calls += 64;
            elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        }
        t.push_back(elapsedMs * 1e6 / static_cast<double>(calls));
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

}  // namespace

TEST_CASE("A8-T3 AVX2 mode-bank update is at least 4x the scalar throughput on 1024 modes") {
    pme::simd::initialise();
    Bank bank;
    constexpr std::size_t n = Bank::kModes;

    const double scalarNs = medianNsPerCall([&] {
        pme::simd::reference::stepModes(bank.q.data(), bank.u.data(), bank.co.beta.data(),
                                        bank.co.kappa.data(), bank.co.gamma.data(),
                                        bank.force.data(), n);
    });
    simdtest::log("[simd] A8-T3 scalar reference: ", scalarNs, " ns per 1024-mode step");

    double bestRatio = 0.0;
    double avx2Ratio = 0.0;
    bool haveAvx2 = false;
    const char* bestName = "";
    for (std::size_t t = 0; t < pme::simd::testTargetCount(); ++t) {
        pme::simd::testSelectTarget(t);
        const double ns = medianNsPerCall([&] {
            pme::simd::stepModes(bank.q.data(), bank.u.data(), bank.co.beta.data(),
                                 bank.co.kappa.data(), bank.co.gamma.data(), bank.force.data(), n);
        });
        const double ratio = scalarNs / ns;
        const char* name = pme::simd::testTargetName(t);
        simdtest::log("[simd] A8-T3 ", name, ": ", ns, " ns per step, ratio ", ratio);
        if (ratio > bestRatio) {
            bestRatio = ratio;
            bestName = name;
        }
        if (std::strcmp(name, "AVX2") == 0) {
            haveAvx2 = true;
            avx2Ratio = ratio;
        }
    }
    pme::simd::testResetTarget();
    simdtest::log("[simd] A8-T3 best target ", bestName, ", ratio ", bestRatio);

    if (!haveAvx2) {
        MESSAGE("AVX2 not available; best target " << bestName << " ratio " << bestRatio);
        CHECK(bestRatio > 1.0);
        return;
    }
#if PME_SIMD_UNOPTIMISED_BUILD
    MESSAGE("unoptimised (Debug) build: AVX2 ratio " << avx2Ratio << " logged, 4x not enforced");
    CHECK(avx2Ratio > 0.0);
#else
    MESSAGE("AVX2 ratio " << avx2Ratio);
    CHECK(avx2Ratio >= 4.0);
#endif
}
