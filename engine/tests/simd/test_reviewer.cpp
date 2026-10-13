// SPDX-License-Identifier: MIT
// Covers: A8-T1, A8-R1, A8-R2, A8-R3, A8-R4
// Independent review tests (code-reviewer, phase A8):
//  - a bit-exact oracle of the canonical reduction order written separately from
//    pme::simd::combineLanes and from pme::simd::reference: it uses std::fma where the active
//    target's multiply-add is fused, so even fused targets must match bit for bit;
//  - tail safety: every operand sits at the end of an allocation followed (on POSIX) by a
//    PROT_NONE guard page, with NaN poison between the last element and the page end, at
//    n = 1, 15, 17 and 1023 (and others). A read past the poison faults; a leaked poison lane turns
//    the result into NaN; a write past n is caught by the poison check.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "test_support.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define PME_SIMD_REVIEW_GUARD_PAGES 1  // NOLINT(cppcoreguidelines-macro-usage)
#else
#define PME_SIMD_REVIEW_GUARD_PAGES 0  // NOLINT(cppcoreguidelines-macro-usage)
#endif

// NOLINTBEGIN(bugprone-easily-swappable-parameters,modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-special-member-functions,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-bounds-pointer-arithmetic,readability-function-cognitive-complexity)
namespace {

using simdtest::Buf;
using simdtest::Rng;

// ---- independent oracle ------------------------------------------------------------------

template <class T>
T mulAdd(bool fused, T a, T b, T acc) {
    if (fused) {
        return std::fma(a, b, acc);
    }
    const T p = a * b;
    return acc + p;
}

template <class T, std::size_t L>
T oracleDot(const T* a, const T* b, std::size_t n, bool fused) {
    T lane[L] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t i = 0; i < n; ++i) {
        lane[i % L] = mulAdd(fused, a[i], b[i], lane[i % L]);
    }
    for (std::size_t half = L / 2; half >= 1; half /= 2) {
        for (std::size_t i = 0; i < half; ++i) {
            lane[i] = lane[i] + lane[i + half];
        }
    }
    return lane[0];
}

template <class T>
T oracleAxpyNeg(bool fused, T a, T h, T c) {
    return fused ? std::fma(-c, h, a) : a - c * h;
}

void oracleMatmul(bool fused, const float* A, std::size_t lda, const float* B, std::size_t ldb,
                  float* C, std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) {
    for (std::size_t m = 0; m < M; ++m) {
        for (std::size_t n = 0; n < N; ++n) {
            float total = 0.0F;
            for (std::size_t k0 = 0; k0 < K; k0 += 64) {
                float lane[16] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays)
                const std::size_t len = (K - k0 < 64) ? (K - k0) : 64;
                for (std::size_t kk = 0; kk < len; ++kk) {
                    lane[kk % 16] =
                        mulAdd(fused, A[m * lda + k0 + kk], B[(k0 + kk) * ldb + n], lane[kk % 16]);
                }
                for (std::size_t half = 8; half >= 1; half /= 2) {
                    for (std::size_t i = 0; i < half; ++i) {
                        lane[i] = lane[i] + lane[i + half];
                    }
                }
                total = (k0 == 0) ? lane[0] : total + lane[0];
            }
            C[m * ldc + n] = total;
        }
    }
}

// ---- buffers at the end of a page, guard page after them -----------------------------------

template <class T>
class GuardBuf {
public:
    explicit GuardBuf(std::size_t n) : n_(n) {
        const std::size_t bytes = ((n * sizeof(T) + 63U) / 64U) * 64U;  // 64-byte multiple
        slackEnd_ = bytes / sizeof(T);
#if PME_SIMD_REVIEW_GUARD_PAGES
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const std::size_t dataPages = (bytes + page - 1U) / page;
        mapBytes_ = (dataPages + 1U) * page;
        void* m =
            mmap(nullptr, mapBytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        REQUIRE(m != MAP_FAILED);
        map_ = m;
        auto* guard = static_cast<char*>(m) + dataPages * page;
        REQUIRE(mprotect(guard, page, PROT_NONE) == 0);
        data_ = reinterpret_cast<T*>(guard - bytes);
#else
        mem_.allocate(slackEnd_ + 16U);
        data_ = mem_.data();
#endif
        for (std::size_t i = 0; i < slackEnd_; ++i) {
            data_[i] = i < n ? T(0) : std::numeric_limits<T>::quiet_NaN();
        }
    }
    GuardBuf(const GuardBuf&) = delete;
    GuardBuf& operator=(const GuardBuf&) = delete;
    ~GuardBuf() {
#if PME_SIMD_REVIEW_GUARD_PAGES
        munmap(map_, mapBytes_);
#endif
    }
    [[nodiscard]] T* data() { return data_; }
    [[nodiscard]] const T* data() const { return data_; }
    T& operator[](std::size_t i) { return data_[i]; }
    /// True if every poison element after the last one is still NaN (nothing wrote past n).
    [[nodiscard]] bool poisonIntact() const {
        for (std::size_t i = n_; i < slackEnd_; ++i) {
            if (!std::isnan(data_[i])) {
                return false;
            }
        }
        return true;
    }

private:
    std::size_t n_;
    std::size_t slackEnd_{0};
    T* data_{nullptr};
#if PME_SIMD_REVIEW_GUARD_PAGES
    void* map_{nullptr};
    std::size_t mapBytes_{0};
#else
    pme::osc::AlignedArray<T> mem_;
#endif
};

template <class T>
void fill(GuardBuf<T>& b, std::size_t n, Rng& rng, double lo, double hi,
          double scaleDecades = 0.0) {
    for (std::size_t i = 0; i < n; ++i) {
        const double s =
            scaleDecades > 0.0 ? std::pow(10.0, rng.uniform(-scaleDecades, scaleDecades)) : 1.0;
        b[i] = static_cast<T>(rng.uniform(lo, hi) * s);
    }
}

const std::size_t kSizes[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 33, 63, 65, 1023, 1025};  // NOLINT

}  // namespace

TEST_CASE("A8-T1 reviewer parity of dot2 and axpyNeg in float64") {
    simdtest::forEachTarget("A8-T1 reviewer f64", [&](std::size_t, const char* name, bool fused) {
        CAPTURE(name);
        for (const std::size_t n : kSizes) {
            CAPTURE(n);
            Rng rng(7000 + n);
            for (int cancelling = 0; cancelling < 2; ++cancelling) {
                const double lo = cancelling != 0 ? -1.0 : 0.1;
                GuardBuf<double> w(n);
                GuardBuf<double> a(n);
                GuardBuf<double> h(n);
                GuardBuf<double> z(n);
                fill(w, n, rng, lo, 1.0);
                fill(a, n, rng, lo, 1.0, 3.0);
                fill(h, n, rng, lo, 1.0);
                double wa = 0.0;
                double wh = 0.0;
                pme::simd::dot2(w.data(), a.data(), h.data(), n, wa, wh);
                // bit-exact against the independent order (fused targets use std::fma)
                CHECK(wa == oracleDot<double, 8>(w.data(), a.data(), n, fused));
                CHECK(wh == oracleDot<double, 8>(w.data(), h.data(), n, fused));
                CHECK(pme::simd::dot(w.data(), a.data(), n) ==
                      oracleDot<double, 8>(w.data(), a.data(), n, fused));
                // near the exact sum
                long double exact = 0.0L;
                long double sumAbs = 0.0L;
                for (std::size_t i = 0; i < n; ++i) {
                    exact += static_cast<long double>(w[i]) * a[i];
                    sumAbs += std::fabs(static_cast<long double>(w[i]) * a[i]);
                }
                CHECK(simdtest::reductionError(wa, static_cast<double>(exact),
                                               static_cast<double>(sumAbs)) <= 1e-12);

                const double c = wa / (1.0 + wh);
                pme::simd::axpyNeg(z.data(), a.data(), h.data(), c, n);
                for (std::size_t i = 0; i < n; ++i) {
                    REQUIRE(z[i] == oracleAxpyNeg(fused, a[i], h[i], c));
                }
                // in place (z aliases a)
                std::vector<double> expect(n);
                for (std::size_t i = 0; i < n; ++i) {
                    expect[i] = oracleAxpyNeg(fused, a[i], h[i], c);
                }
                pme::simd::axpyNeg(a.data(), a.data(), h.data(), c, n);
                for (std::size_t i = 0; i < n; ++i) {
                    REQUIRE(a[i] == expect[i]);
                }
                CHECK(z.poisonIntact());
                CHECK(a.poisonIntact());
            }
        }
    });
}

TEST_CASE("A8-T1 reviewer bit-exact canonical order for float32 reductions and matmul") {
    simdtest::forEachTarget("A8-T1 reviewer f32", [&](std::size_t, const char* name, bool fused) {
        CAPTURE(name);
        for (const std::size_t n : kSizes) {
            CAPTURE(n);
            Rng rng(8000 + n);
            GuardBuf<float> a(n);
            GuardBuf<float> b(n);
            fill(a, n, rng, -1.0, 1.0, 3.0);
            fill(b, n, rng, -1.0, 1.0);
            CHECK(pme::simd::dot(a.data(), b.data(), n) ==
                  oracleDot<float, 16>(a.data(), b.data(), n, fused));
            // dotF64: exact float products, 8 float64 lanes, no rounding difference possible
            double lane[8] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays)
            for (std::size_t i = 0; i < n; ++i) {
                lane[i % 8] += static_cast<double>(a[i]) * static_cast<double>(b[i]);
            }
            for (std::size_t half = 4; half >= 1; half /= 2) {
                for (std::size_t i = 0; i < half; ++i) {
                    lane[i] += lane[i + half];
                }
            }
            CHECK(pme::simd::dotF64(a.data(), b.data(), n) == lane[0]);
        }
        // matmul: chunk boundaries (K = 1, 63, 64, 65, 130) and ragged N
        for (const std::size_t K : {std::size_t{1}, std::size_t{63}, std::size_t{64},
                                    std::size_t{65}, std::size_t{130}}) {
            for (const std::size_t N : {std::size_t{1}, std::size_t{15}, std::size_t{17}}) {
                const std::size_t M = 3;
                CAPTURE(K);
                CAPTURE(N);
                Rng rng(9000 + K * 31 + N);
                GuardBuf<float> A(M * K);
                GuardBuf<float> B(K * N);
                GuardBuf<float> C(M * N);
                fill(A, M * K, rng, -1.0, 1.0);
                fill(B, K * N, rng, -1.0, 1.0);
                std::vector<float> expect(M * N);
                oracleMatmul(fused, A.data(), K, B.data(), N, expect.data(), N, M, N, K);
                pme::simd::matmul(A.data(), K, B.data(), N, C.data(), N, M, N, K);
                for (std::size_t i = 0; i < M * N; ++i) {
                    REQUIRE(C[i] == expect[i]);
                }
                CHECK(C.poisonIntact());
            }
        }
    });
}

TEST_CASE("A8-R1 reviewer stepModes tails with guard pages and one-sided outputs") {
    simdtest::forEachTarget("A8-R1 reviewer", [&](std::size_t, const char* name, bool) {
        CAPTURE(name);
        for (const std::size_t n : kSizes) {
            CAPTURE(n);
            simdtest::Coefficients co(n, 11);
            Rng rng(6000 + n);
            for (int variant = 0; variant < 8; ++variant) {
                const bool withForce = (variant & 1) != 0;
                const bool withQPrev = (variant & 2) != 0;
                const bool withUPrev = (variant & 4) != 0;
                GuardBuf<float> q(n);
                GuardBuf<float> u(n);
                GuardBuf<float> beta(n);
                GuardBuf<float> kappa(n);
                GuardBuf<float> gamma(n);
                GuardBuf<float> force(n);
                GuardBuf<float> qp(n);
                GuardBuf<float> up(n);
                fill(q, n, rng, -1.0, 1.0);
                fill(u, n, rng, -0.01, 0.01);
                fill(force, n, rng, -100.0, 100.0);
                for (std::size_t i = 0; i < n; ++i) {
                    beta[i] = co.beta[i];
                    kappa[i] = co.kappa[i];
                    gamma[i] = co.gamma[i];
                }
                // independent scalar expression, A1's order: ((beta u) - (kappa q)) + (gamma f)
                std::vector<float> qe(n);
                std::vector<float> ue(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const float qn = q[i];
                    const float un = u[i];
                    const float p1 = beta[i] * un;
                    const float p2 = kappa[i] * qn;
                    float next = p1 - p2;
                    if (withForce) {
                        const float p3 = gamma[i] * force[i];
                        next = next + p3;
                    }
                    ue[i] = next;
                    qe[i] = qn + next;
                }
                std::vector<float> qBefore(q.data(), q.data() + n);
                std::vector<float> uBefore(u.data(), u.data() + n);
                pme::simd::stepModes(q.data(), u.data(), beta.data(), kappa.data(), gamma.data(),
                                     withForce ? force.data() : nullptr, n,
                                     withQPrev ? qp.data() : nullptr,
                                     withUPrev ? up.data() : nullptr);
                for (std::size_t i = 0; i < n; ++i) {
                    REQUIRE(q[i] == qe[i]);
                    REQUIRE(u[i] == ue[i]);
                    if (withQPrev) {
                        REQUIRE(qp[i] == qBefore[i]);
                    }
                    if (withUPrev) {
                        REQUIRE(up[i] == uBefore[i]);
                    }
                }
                CHECK(q.poisonIntact());
                CHECK(u.poisonIntact());
                CHECK(qp.poisonIntact());
                CHECK(up.poisonIntact());
            }
        }
    });
}
// NOLINTEND(bugprone-easily-swappable-parameters,modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-special-member-functions,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-bounds-pointer-arithmetic,readability-function-cognitive-complexity)
