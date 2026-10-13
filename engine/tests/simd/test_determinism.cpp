// SPDX-License-Identifier: MIT
// Covers: A8-T2
// Every kernel run 100 times on the same input, and on copies at other 64-byte-aligned addresses,
// gives bit-identical results, on every supported target.
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "test_support.hpp"

namespace {

using simdtest::Buf;
using simdtest::Rng;

template <class T>
bool sameBits(const T* a, const T* b, std::size_t n) {
    return std::memcmp(a, b, n * sizeof(T)) == 0;
}

// An arena holding `copies` copies of one array, each starting on its own 64-byte boundary at
// a different offset (the stride is not a multiple of the page size).
template <class T>
struct Copies {
    Buf<T> arena;
    std::size_t stride;
    std::size_t n;
    Copies(const Buf<T>& src, std::size_t count, std::size_t padElements)
        : arena(count * (((src.size() + padElements + 15U) / 16U) * 16U)),
          stride(((src.size() + padElements + 15U) / 16U) * 16U),
          n(src.size()) {
        for (std::size_t c = 0; c < count; ++c) {
            std::memcpy(arena.data() + c * stride, src.data(), n * sizeof(T));
        }
    }
    [[nodiscard]] T* at(std::size_t c) { return arena.data() + c * stride; }
    [[nodiscard]] const T* at(std::size_t c) const { return arena.data() + c * stride; }
};

}  // namespace

TEST_CASE("A8-T2 repeated runs and aligned copies give bit-identical results") {
    constexpr std::size_t kCopies = 4;
    simdtest::forEachTarget("A8-T2", [&](std::size_t, const char* name, bool) {
        CAPTURE(name);
        for (const std::size_t n : {std::size_t{17}, std::size_t{1023}, std::size_t{4099}}) {
            simdtest::Coefficients co(n);
            Rng rng(1000 + n);
            Buf<float> q(n);
            Buf<float> u(n);
            Buf<float> f(n);
            Buf<float> w(n);
            Buf<float> a(n);
            Buf<float> h(n);
            Buf<double> ad(n);
            Buf<double> bd(n);
            simdtest::fillRandom(q, rng, -1.0, 1.0);
            simdtest::fillRandom(u, rng, -0.01, 0.01);
            simdtest::fillRandom(f, rng, -1e3, 1e3);
            simdtest::fillRandom(w, rng, 0.0, 1.0);
            simdtest::fillRandom(a, rng, -1.0, 1.0);
            simdtest::fillRandom(h, rng, 0.0, 1.0);
            simdtest::fillRandom(ad, rng, -1.0, 1.0);
            simdtest::fillRandom(bd, rng, -1.0, 1.0);

            // stepModes: 100 runs from the same initial state, each result equal to the first
            Buf<float> q1(n);
            Buf<float> u1(n);
            for (int run = 0; run < 100; ++run) {
                Buf<float> qr(n);
                Buf<float> ur(n);
                qr.copyFrom(q);
                ur.copyFrom(u);
                pme::simd::stepModes(qr.data(), ur.data(), co.beta.data(), co.kappa.data(),
                                     co.gamma.data(), f.data(), n);
                if (run == 0) {
                    q1.copyFrom(qr);
                    u1.copyFrom(ur);
                } else {
                    REQUIRE(sameBits(qr.data(), q1.data(), n));
                    REQUIRE(sameBits(ur.data(), u1.data(), n));
                }
            }
            // stepModes on copies at different aligned addresses
            Copies<float> qc(q, kCopies, 48U);
            Copies<float> uc(u, kCopies, 80U);
            Copies<float> bc(co.beta, kCopies, 112U);
            Copies<float> kc(co.kappa, kCopies, 176U);
            Copies<float> gc(co.gamma, kCopies, 208U);
            Copies<float> fc(f, kCopies, 32U);
            for (std::size_t c = 0; c < kCopies; ++c) {
                pme::simd::stepModes(qc.at(c), uc.at(c), bc.at(c), kc.at(c), gc.at(c), fc.at(c), n);
                REQUIRE(sameBits(qc.at(c), q1.data(), n));
                REQUIRE(sameBits(uc.at(c), u1.data(), n));
            }

            // dot products
            const float d0 = pme::simd::dot(a.data(), h.data(), n);
            const double e0 = pme::simd::dot(ad.data(), bd.data(), n);
            const double m0 = pme::simd::dotF64(a.data(), h.data(), n);
            float wa0 = 0.0F;
            float wh0 = 0.0F;
            pme::simd::dot2(w.data(), a.data(), h.data(), n, wa0, wh0);
            Buf<float> z0(n);
            pme::simd::axpyNeg(z0.data(), a.data(), h.data(), 0.25F, n);
            Buf<float> r0(n);
            r0.copyFrom(a);
            const float c0 = pme::simd::rankOneUpdate(r0.data(), r0.data(), h.data(), w.data(), n);
            for (int run = 0; run < 100; ++run) {
                REQUIRE(pme::simd::dot(a.data(), h.data(), n) == d0);
                REQUIRE(pme::simd::dot(ad.data(), bd.data(), n) == e0);
                REQUIRE(pme::simd::dotF64(a.data(), h.data(), n) == m0);
                float wa = 0.0F;
                float wh = 0.0F;
                pme::simd::dot2(w.data(), a.data(), h.data(), n, wa, wh);
                REQUIRE(wa == wa0);
                REQUIRE(wh == wh0);
                Buf<float> z(n);
                pme::simd::axpyNeg(z.data(), a.data(), h.data(), 0.25F, n);
                REQUIRE(sameBits(z.data(), z0.data(), n));
                Buf<float> r(n);
                r.copyFrom(a);
                REQUIRE(pme::simd::rankOneUpdate(r.data(), r.data(), h.data(), w.data(), n) == c0);
                REQUIRE(sameBits(r.data(), r0.data(), n));
            }
            Copies<float> ac(a, kCopies, 48U);
            Copies<float> hc(h, kCopies, 80U);
            Copies<double> adc(ad, kCopies, 112U);
            Copies<double> bdc(bd, kCopies, 144U);
            for (std::size_t c = 0; c < kCopies; ++c) {
                REQUIRE(pme::simd::dot(ac.at(c), hc.at(c), n) == d0);
                REQUIRE(pme::simd::dot(adc.at(c), bdc.at(c), n) == e0);
                REQUIRE(pme::simd::dotF64(ac.at(c), hc.at(c), n) == m0);
            }
        }

        // matmul
        const std::size_t m = 7;
        const std::size_t nn = 32;
        const std::size_t k = 1000;
        Rng rng(2000);
        Buf<float> A(m * k);
        Buf<float> B(k * nn);
        simdtest::fillRandom(A, rng, -1.0, 1.0);
        simdtest::fillRandom(B, rng, -1.0, 1.0);
        Buf<float> C0(m * nn);
        pme::simd::matmul(A.data(), k, B.data(), nn, C0.data(), nn, m, nn, k);
        for (int run = 0; run < 100; ++run) {
            Buf<float> C(m * nn);
            pme::simd::matmul(A.data(), k, B.data(), nn, C.data(), nn, m, nn, k);
            REQUIRE(sameBits(C.data(), C0.data(), m * nn));
        }
        Copies<float> Ac(A, 3, 48U);
        Copies<float> Bc(B, 3, 80U);
        for (std::size_t c = 0; c < 3; ++c) {
            Buf<float> C(m * nn);
            pme::simd::matmul(Ac.at(c), k, Bc.at(c), nn, C.data(), nn, m, nn, k);
            REQUIRE(sameBits(C.data(), C0.data(), m * nn));
        }
    });
}
