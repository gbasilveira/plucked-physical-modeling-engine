// SPDX-License-Identifier: MIT
// Covers: A8-R1, A8-R2, A8-R3, A8-R4, A8-R5
// Real-time safety: after `initialise`, every kernel, the dispatch and the flush-denormals guard
// run under ScopedRealtimeThread. With the rt-safety preset any allocation or blocking call
// aborts the process.
#include <doctest/doctest.h>

#include <vector>

#include "pme/rtcheck/rtcheck.hpp"
#include "pme/simd/denormals.hpp"
#include "test_support.hpp"

TEST_CASE("A8-R5 dispatch and every kernel allocate nothing under ScopedRealtimeThread") {
    pme::simd::initialise();  // non-audio: detects the CPU
    const std::size_t n = 1000;
    simdtest::Coefficients co(n);
    simdtest::Rng rng(5);
    simdtest::Buf<float> q(n);
    simdtest::Buf<float> u(n);
    simdtest::Buf<float> f(n);
    simdtest::Buf<float> w(n);
    simdtest::Buf<float> h(n);
    simdtest::Buf<float> z(n);
    simdtest::Buf<double> ad(n);
    simdtest::Buf<double> bd(n);
    simdtest::Buf<float> A(8 * 100);
    simdtest::Buf<float> B(100 * 32);
    simdtest::Buf<float> C(8 * 32);
    simdtest::fillRandom(q, rng, -1.0, 1.0);
    simdtest::fillRandom(f, rng, -1.0, 1.0);
    simdtest::fillRandom(w, rng, 0.0, 1.0);
    simdtest::fillRandom(h, rng, 0.0, 1.0);
    simdtest::fillRandom(ad, rng, -1.0, 1.0);
    simdtest::fillRandom(bd, rng, -1.0, 1.0);
    simdtest::fillRandom(A, rng, -1.0, 1.0);
    simdtest::fillRandom(B, rng, -1.0, 1.0);
    double sink = 0.0;
    {
        const pme::rtcheck::ScopedRealtimeThread realtime;
        const pme::simd::ScopedFlushDenormals flush;
        for (int s = 0; s < 8; ++s) {
            pme::simd::stepModes(q.data(), u.data(), co.beta.data(), co.kappa.data(),
                                 co.gamma.data(), f.data(), n, z.data(), nullptr);
            sink += static_cast<double>(pme::simd::dot(q.data(), w.data(), n));
            sink += pme::simd::dot(ad.data(), bd.data(), n);
            sink += pme::simd::dotF64(q.data(), w.data(), n);
            float wa = 0.0F;
            float wh = 0.0F;
            pme::simd::dot2(w.data(), q.data(), h.data(), n, wa, wh);
            sink += static_cast<double>(wa + wh);
            pme::simd::axpyNeg(z.data(), q.data(), h.data(), 0.1F, n);
            sink += static_cast<double>(pme::simd::rankOneUpdate(z.data(), q.data(), h.data(), w.data(), n));
            pme::simd::matmul(A.data(), 100, B.data(), 32, C.data(), 32, 8, 32, 100);
            sink += static_cast<double>(C[0]) + pme::simd::activeTargetIsFused() +
                    (pme::simd::activeTargetName() != nullptr ? 1.0 : 0.0);
        }
    }
    CHECK(sink != 0.0);
}
