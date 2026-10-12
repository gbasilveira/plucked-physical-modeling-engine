// SPDX-License-Identifier: MIT
// Covers: A8-R5
// Runtime dispatch (`initialise`, the test hook) and the flush-denormals guard.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "pme/simd/denormals.hpp"
#include "test_support.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define PME_TEST_HAS_FTZ 1
#else
#define PME_TEST_HAS_FTZ 0
#endif

TEST_CASE("A8-R5 initialise returns the name of the chosen target") {
    const char* name = pme::simd::initialise();
    REQUIRE(name != nullptr);
    CHECK(std::strlen(name) > 0);
    CHECK(std::strcmp(name, pme::simd::activeTargetName()) == 0);
    CHECK(std::strcmp(name, pme::simd::initialise()) == 0);  // idempotent
    std::printf("[simd] A8-R5 chosen target: %s (fused multiply-add: %s)\n", name,
                pme::simd::activeTargetIsFused() ? "yes" : "no");
    // The chosen target is the best supported one.
    REQUIRE(pme::simd::testTargetCount() >= 1);
    CHECK(std::strcmp(name, pme::simd::testTargetName(0)) == 0);
}

TEST_CASE("A8-R5 the test hook selects each supported target in turn") {
    pme::simd::initialise();
    const std::size_t count = pme::simd::testTargetCount();
    REQUIRE(count >= 1);
    for (std::size_t t = 0; t < count; ++t) {
        REQUIRE(pme::simd::testSelectTarget(t));
        CHECK(std::strcmp(pme::simd::activeTargetName(), pme::simd::testTargetName(t)) == 0);
        std::printf("[simd] A8-R5 supported target %zu: %s\n", t, pme::simd::testTargetName(t));
    }
    CHECK_FALSE(pme::simd::testSelectTarget(count));
    pme::simd::testResetTarget();
    CHECK(std::strcmp(pme::simd::activeTargetName(), pme::simd::testTargetName(0)) == 0);
}

TEST_CASE("A8-R5 ScopedFlushDenormals flushes inside the guard and restores the mode") {
#if PME_TEST_HAS_FTZ
    // volatile keeps the compiler from folding the products at compile time
    volatile float denormalIn = 1e-40F;   // below the smallest normal float (1.18e-38)
    volatile float one = 1.0F;
    volatile float small = 1e-30F;
    volatile float tinyFactor = 1e-10F;

    const float before = denormalIn * one;
    const float producedBefore = small * tinyFactor;  // a denormal result
    CHECK(before != 0.0F);          // default mode: denormal input survives
    CHECK(producedBefore != 0.0F);  // and denormal results are produced
    {
        const pme::simd::ScopedFlushDenormals guard;
        const float inside = denormalIn * one;
        const float producedInside = small * tinyFactor;
        CHECK(inside == 0.0F);          // DAZ: denormal input read as zero
        CHECK(producedInside == 0.0F);  // FTZ: denormal result flushed
        {
            const pme::simd::ScopedFlushDenormals nested;
            CHECK(denormalIn * one == 0.0F);
        }
        CHECK(denormalIn * one == 0.0F);  // the outer guard is still active
    }
    CHECK(denormalIn * one == before);  // previous mode restored
    CHECK(small * tinyFactor == producedBefore);
#else
    MESSAGE("A8-R5 flush-denormals: no FTZ control on this architecture; guard is a no-op");
    const pme::simd::ScopedFlushDenormals guard;
    CHECK(true);
#endif
}

TEST_CASE("A8-R5 the kernels stay correct under the flush-denormals guard") {
    // u halves every step towards the denormal range; with the guard on it reaches exact zero
    // instead of a denormal.
    const std::size_t n = 40;
    simdtest::Coefficients co(n);
    simdtest::Buf<float> q(n);
    simdtest::Buf<float> u(n);
    for (std::size_t i = 0; i < n; ++i) {
        q[i] = 0.0F;
        u[i] = 1e-30F;
        co.beta[i] = 0.5F;
        co.kappa[i] = 0.0F;
    }
    const pme::simd::ScopedFlushDenormals guard;
    for (int s = 0; s < 400; ++s) {
        pme::simd::stepModes(q.data(), u.data(), co.beta.data(), co.kappa.data(), co.gamma.data(),
                             nullptr, n);
    }
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(std::fpclassify(q[i]) != FP_SUBNORMAL);
        CHECK(std::fpclassify(u[i]) != FP_SUBNORMAL);
#if PME_TEST_HAS_FTZ
        CHECK(u[i] == 0.0F);  // flushed rather than left denormal
#endif
    }
}
