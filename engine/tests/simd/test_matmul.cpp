// SPDX-License-Identifier: MIT
// Covers: A8-T1, A8-R4
// The small matrix-matrix kernel: parity with the scalar reference on every target, accuracy
// against a float64 reference, and the SPEC-EXEC chunk order against an instrumented reference.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "test_support.hpp"

namespace {

using simdtest::Buf;
using simdtest::Rng;

struct Shape {
    std::size_t m, n, k;
};

// A8-T1 shapes: (K x P)(P x S) projections and (P x K)(K x S) velocity products.
std::vector<Shape> shapes() {
    std::vector<Shape> out;
    for (const std::size_t p : {std::size_t{8}, std::size_t{100}}) {
        for (const std::size_t k : {std::size_t{64}, std::size_t{1000}, std::size_t{4096}}) {
            for (const std::size_t s : {std::size_t{16}, std::size_t{32}}) {
                out.push_back({k, s, p});  // (K x P)(P x S): reduction over P
                out.push_back({p, s, k});  // (P x K)(K x S): reduction over K
            }
        }
    }
    // tails: odd N, K not a multiple of 16 or 64, single row and column, K = 1
    out.push_back({3, 17, 65});
    out.push_back({1, 1, 1});
    out.push_back({5, 15, 129});
    out.push_back({2, 33, 191});
    return out;
}

// Independent restatement of the SPEC-EXEC order in float32 (no fused arithmetic): chunks of 64
// along K in chunk order, each chunk in 16 virtual lanes (k mod 16) combined pairwise.
float orderedElement(const float* A, std::size_t lda, const float* B, std::size_t ldb,
                     std::size_t m, std::size_t n, std::size_t K) {
    float total = 0.0F;
    for (std::size_t k0 = 0; k0 < K; k0 += 64) {
        std::array<float, 16> lane{};
        for (std::size_t k = k0; k < std::min(K, k0 + 64); ++k) {
            lane[(k - k0) % 16] += A[m * lda + k] * B[k * ldb + n];
        }
        for (std::size_t half = 8; half >= 1; half /= 2) {
            for (std::size_t i = 0; i < half; ++i) {
                lane[i] += lane[i + half];
            }
        }
        total += lane[0];
    }
    return total;
}

}  // namespace

TEST_CASE("A8-T1 matmul equals the scalar reference on every target") {
    simdtest::forEachTarget("A8-T1 matmul", [&](std::size_t, const char* name, bool fused) {
        double worst = 0.0;
        for (const Shape& sh : shapes()) {
            Rng rng(800 + sh.m * 31 + sh.n * 7 + sh.k);
            Buf<float> A(sh.m * sh.k);
            Buf<float> B(sh.k * sh.n);
            Buf<float> C(sh.m * sh.n);
            Buf<float> R(sh.m * sh.n);
            simdtest::fillRandom(A, rng, -1.0, 1.0);
            simdtest::fillRandom(B, rng, -1.0, 1.0);
            pme::simd::matmul(A.data(), sh.k, B.data(), sh.n, C.data(), sh.n, sh.m, sh.n, sh.k);
            pme::simd::reference::matmul(A.data(), sh.k, B.data(), sh.n, R.data(), sh.n, sh.m, sh.n, sh.k);
            for (std::size_t m = 0; m < sh.m; ++m) {
                for (std::size_t n = 0; n < sh.n; ++n) {
                    double sumAbs = 0.0;
                    for (std::size_t k = 0; k < sh.k; ++k) {
                        sumAbs += std::abs(static_cast<double>(A[m * sh.k + k]) * B[k * sh.n + n]);
                    }
                    const float c = C[m * sh.n + n];
                    const float r = R[m * sh.n + n];
                    if (!fused) {
                        REQUIRE(c == r);
                    }
                    const double e = simdtest::reductionError(c, r, sumAbs);
                    worst = std::max(worst, e);
                    REQUIRE(e <= 1e-6);
                }
            }
        }
        std::printf("[simd] A8-T1 matmul worst error vs reference on %s: %.3g\n", name, worst);
    });
}

TEST_CASE("A8-R4 matmul against a float64 reference and the SPEC-EXEC chunk order") {
    simdtest::forEachTarget("A8-R4 matmul", [&](std::size_t, const char* name, bool fused) {
        double worstPositive = 0.0;  // cancellation-free: relative to |C|
        double worstCancelling = 0.0;  // relative to max(|C|, sum|terms| / 16)
        for (const Shape& sh : shapes()) {
            for (int cancelling = 0; cancelling < 2; ++cancelling) {
                Rng rng(900 + sh.m * 31 + sh.n * 7 + sh.k);
                Buf<float> A(sh.m * sh.k);
                Buf<float> B(sh.k * sh.n);
                Buf<float> C(sh.m * sh.n);
                const double lo = cancelling != 0 ? -1.0 : 0.0;
                simdtest::fillRandom(A, rng, lo, 1.0);
                simdtest::fillRandom(B, rng, lo, 1.0);
                pme::simd::matmul(A.data(), sh.k, B.data(), sh.n, C.data(), sh.n, sh.m, sh.n, sh.k);
                for (std::size_t m = 0; m < sh.m; ++m) {
                    for (std::size_t n = 0; n < sh.n; ++n) {
                        double exact = 0.0;
                        double sumAbs = 0.0;
                        for (std::size_t k = 0; k < sh.k; ++k) {
                            const double t = static_cast<double>(A[m * sh.k + k]) * B[k * sh.n + n];
                            exact += t;
                            sumAbs += std::abs(t);
                        }
                        const double c = C[m * sh.n + n];
                        const double e = simdtest::reductionError(c, exact, sumAbs);
                        if (cancelling != 0) {
                            worstCancelling = std::max(worstCancelling, e);
                        } else {
                            worstPositive = std::max(worstPositive, e);
                        }
                        REQUIRE(e <= 1e-5);
                        if (!fused) {
                            // the chunk order reproduces bit for bit
                            REQUIRE(c == orderedElement(A.data(), sh.k, B.data(), sh.n, m, n, sh.k));
                        }
                    }
                }
            }
        }
        std::printf("[simd] A8-R4 matmul worst relative error vs float64 on %s: %.3g (positive), "
                    "%.3g (cancelling)\n",
                    name, worstPositive, worstCancelling);
    });
}

TEST_CASE("A8-R4 matmul with leading dimensions larger than the matrix and an empty reduction") {
    simdtest::forEachTarget("A8-R4 strides", [&](std::size_t, const char*, bool fused) {
        const std::size_t M = 4;
        const std::size_t N = 19;
        const std::size_t K = 70;
        const std::size_t lda = K + 9;
        const std::size_t ldb = N + 5;
        const std::size_t ldc = N + 3;
        Rng rng(77);
        Buf<float> A(M * lda);
        Buf<float> B(K * ldb);
        Buf<float> C(M * ldc);
        Buf<float> R(M * ldc);
        simdtest::fillRandom(A, rng, -1.0, 1.0);
        simdtest::fillRandom(B, rng, -1.0, 1.0);
        for (std::size_t i = 0; i < C.size(); ++i) {
            C[i] = -123.0F;  // sentinel: columns beyond N must stay untouched
            R[i] = -123.0F;
        }
        pme::simd::matmul(A.data(), lda, B.data(), ldb, C.data(), ldc, M, N, K);
        pme::simd::reference::matmul(A.data(), lda, B.data(), ldb, R.data(), ldc, M, N, K);
        for (std::size_t m = 0; m < M; ++m) {
            for (std::size_t n = 0; n < ldc; ++n) {
                if (n >= N) {
                    CHECK(C[m * ldc + n] == -123.0F);
                } else if (!fused) {
                    CHECK(C[m * ldc + n] == R[m * ldc + n]);
                } else {
                    CHECK(std::abs(C[m * ldc + n] - R[m * ldc + n]) <= 1e-4F);
                }
            }
        }
        // K = 0 gives zeros
        pme::simd::matmul(A.data(), lda, B.data(), ldb, C.data(), ldc, M, N, 0);
        for (std::size_t m = 0; m < M; ++m) {
            for (std::size_t n = 0; n < N; ++n) {
                CHECK(C[m * ldc + n] == 0.0F);
            }
        }
    });
}
