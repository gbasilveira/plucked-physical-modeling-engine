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
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
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
// NOLINTEND(bugprone-easily-swappable-parameters)

// Everything that does not depend on the target, computed once: inputs, the scalar reference,
// the float64 truth, the sum of absolute terms and the instrumented chunk-order value.
struct Case {
    Shape sh;
    bool cancelling;
    Buf<float> A, B, ref, ordered;
    std::vector<double> exact, sumAbs;
    Case(Shape s, bool canc)
        : sh(s),
          cancelling(canc),
          A(s.m * s.k),
          B(s.k * s.n),
          ref(s.m * s.n),
          ordered(s.m * s.n),
          exact(s.m * s.n),
          sumAbs(s.m * s.n) {
        Rng rng(800 + s.m * 31 + s.n * 7 + s.k + (canc ? 1 : 0));
        const double lo = canc ? -1.0 : 0.0;
        simdtest::fillRandom(A, rng, lo, 1.0);
        simdtest::fillRandom(B, rng, lo, 1.0);
        pme::simd::reference::matmul(A.data(), s.k, B.data(), s.n, ref.data(), s.n, s.m, s.n, s.k);
        for (std::size_t m = 0; m < s.m; ++m) {
            for (std::size_t n = 0; n < s.n; ++n) {
                double e = 0.0;
                double sa = 0.0;
                for (std::size_t k = 0; k < s.k; ++k) {
                    const double t = static_cast<double>(A[m * s.k + k]) * B[k * s.n + n];
                    e += t;
                    sa += std::abs(t);
                }
                exact[m * s.n + n] = e;
                sumAbs[m * s.n + n] = sa;
                ordered[m * s.n + n] = orderedElement(A.data(), s.k, B.data(), s.n, m, n, s.k);
            }
        }
    }
};

const std::vector<Case>& cases() {
    static const std::vector<Case> all = [] {
        std::vector<Case> v;
        for (const Shape& sh : shapes()) {
            v.emplace_back(sh, false);
            v.emplace_back(sh, true);
        }
        return v;
    }();
    return all;
}

}  // namespace

TEST_CASE("A8-T1 matmul equals the scalar reference on every target") {
    simdtest::forEachTarget("A8-T1 matmul", [&](std::size_t, const char* name, bool fused) {
        double worst = 0.0;
        for (const Case& c : cases()) {
            const Shape& sh = c.sh;
            Buf<float> C(sh.m * sh.n);
            pme::simd::matmul(c.A.data(), sh.k, c.B.data(), sh.n, C.data(), sh.n, sh.m, sh.n, sh.k);
            for (std::size_t i = 0; i < sh.m * sh.n; ++i) {
                if (!fused) {
                    REQUIRE(C[i] == c.ref[i]);
                }
                const double e = simdtest::reductionError(C[i], c.ref[i], c.sumAbs[i]);
                worst = std::max(worst, e);
                REQUIRE(e <= 1e-6);
            }
        }
        simdtest::log("[simd] A8-T1 matmul worst error vs reference on ", name, ": ", worst);
    });
}

TEST_CASE("A8-R4 matmul against a float64 reference and the SPEC-EXEC chunk order") {
    simdtest::forEachTarget("A8-R4 matmul", [&](std::size_t, const char* name, bool fused) {
        double worstPositive = 0.0;    // cancellation-free: relative to |C|
        double worstCancelling = 0.0;  // relative to max(|C|, sum|terms| / 4)
        for (const Case& c : cases()) {
            const Shape& sh = c.sh;
            Buf<float> C(sh.m * sh.n);
            pme::simd::matmul(c.A.data(), sh.k, c.B.data(), sh.n, C.data(), sh.n, sh.m, sh.n, sh.k);
            for (std::size_t i = 0; i < sh.m * sh.n; ++i) {
                const double e = simdtest::reductionError(C[i], c.exact[i], c.sumAbs[i]);
                (c.cancelling ? worstCancelling : worstPositive) =
                    std::max(c.cancelling ? worstCancelling : worstPositive, e);
                REQUIRE(e <= 1e-5);
                if (!fused) {
                    REQUIRE(C[i] == c.ordered[i]);  // the chunk order reproduces bit for bit
                }
            }
        }
        simdtest::log("[simd] A8-R4 matmul worst relative error vs float64 on ", name, ": ",
                      worstPositive, " (positive), ", worstCancelling, " (cancelling)");
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
