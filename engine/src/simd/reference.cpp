// SPDX-License-Identifier: MIT
// Scalar references of every SIMD kernel (A8-T1, A8-T3): plain C++, one element at a time, no
// Highway. This file is compiled with the auto-vectoriser switched off (see CMakeLists.txt, block
// A8, and docs/dev/simd.md "Scalar references"), so that A8-T3 compares the vector kernels with
// truly scalar code. The arithmetic order is the canonical one of kernels.hpp; no fused
// multiply-add is used (the build sets -ffp-contract=off).
//
// SPEC sections implemented: SPEC-OSC "Difference form", SPEC-BRG "Bridge force", SPEC-NL
// "Explicit low-rank solve", SPEC-EXEC "Threading contract" (fixed reduction order).
#include <cstddef>

#include "pme/simd/canonical.hpp"
#include "pme/simd/kernels.hpp"

namespace pme::simd::reference {

namespace {

// SPEC-OSC difference form, same operation order as pme::osc::stepModes: (beta u - kappa q) + gamma f.
template <class S, class C>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModesImpl(S* q, S* u, const C* beta, const C* kappa, const C* gamma, const S* force,
                   std::size_t n, S* qPrevOut, S* uPrevOut) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const S qn = q[i];
        const S un = u[i];
        if (qPrevOut != nullptr) {
            qPrevOut[i] = qn;
        }
        if (uPrevOut != nullptr) {
            uPrevOut[i] = un;
        }
        S uNext = static_cast<S>(beta[i]) * un - static_cast<S>(kappa[i]) * qn;
        if (force != nullptr) {
            uNext = uNext + static_cast<S>(gamma[i]) * force[i];
        }
        u[i] = uNext;
        q[i] = qn + uNext;
    }
}

// SPEC-EXEC canonical order: element i to virtual lane i mod Lanes, lanes in index order, then the
// pairwise tree of combineLanes.
template <class T, std::size_t Lanes>
T dotImpl(const T* a, const T* b, std::size_t n) noexcept {
    T lanes[Lanes] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t i = 0; i < n; ++i) {
        lanes[i % Lanes] = lanes[i % Lanes] + a[i] * b[i];
    }
    return combineLanes<T, Lanes>(lanes);
}

template <class T, std::size_t Lanes>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void dot2Impl(const T* w, const T* a, const T* h, std::size_t n, T& wa, T& wh) noexcept {
    T la[Lanes] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    T lh[Lanes] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t i = 0; i < n; ++i) {
        la[i % Lanes] = la[i % Lanes] + w[i] * a[i];
        lh[i % Lanes] = lh[i % Lanes] + w[i] * h[i];
    }
    wa = combineLanes<T, Lanes>(la);
    wh = combineLanes<T, Lanes>(lh);
}

// SPEC-NL explicit low-rank solve, r = 1: z = a - h (w^T a) / (1 + w^T h).
template <class T, std::size_t Lanes>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h, w in SPEC order
T rankOneImpl(T* z, const T* a, const T* h, const T* w, std::size_t n) noexcept {
    T wa{};
    T wh{};
    dot2Impl<T, Lanes>(w, a, h, n, wa, wh);
    const T c = wa / (T{1} + wh);
    axpyNeg(z, a, h, c, n);
    return c;
}

}  // namespace

void stepModes(float* q, float* u, const float* beta, const float* kappa, const float* gamma,
               const float* force, std::size_t n, float* qPrevOut, float* uPrevOut) noexcept {
    stepModesImpl<float, float>(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut);
}

void stepModes(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* force, std::size_t n, double* qPrevOut, double* uPrevOut) noexcept {
    stepModesImpl<double, double>(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut);
}

void stepModes(double* q, double* u, const float* beta, const float* kappa, const float* gamma,
               const double* force, std::size_t n, double* qPrevOut, double* uPrevOut) noexcept {
    stepModesImpl<double, float>(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut);
}

float dot(const float* a, const float* b, std::size_t n) noexcept {
    return dotImpl<float, kCanonicalLanesF32>(a, b, n);
}

double dot(const double* a, const double* b, std::size_t n) noexcept {
    return dotImpl<double, kCanonicalLanesF64>(a, b, n);
}

double dotF64(const float* a, const float* b, std::size_t n) noexcept {
    double lanes[kCanonicalLanesF64] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lane = i % kCanonicalLanesF64;
        lanes[lane] = lanes[lane] + static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return combineLanes<double, kCanonicalLanesF64>(lanes);
}

void dot2(const float* w, const float* a, const float* h, std::size_t n, float& wa,
          float& wh) noexcept {
    dot2Impl<float, kCanonicalLanesF32>(w, a, h, n, wa, wh);
}

void dot2(const double* w, const double* a, const double* h, std::size_t n, double& wa,
          double& wh) noexcept {
    dot2Impl<double, kCanonicalLanesF64>(w, a, h, n, wa, wh);
}

void axpyNeg(float* z, const float* a, const float* h, float c, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        z[i] = a[i] - c * h[i];
    }
}

void axpyNeg(double* z, const double* a, const double* h, double c, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        z[i] = a[i] - c * h[i];
    }
}

float rankOneUpdate(float* z, const float* a, const float* h, const float* w,
                    std::size_t n) noexcept {
    return rankOneImpl<float, kCanonicalLanesF32>(z, a, h, w, n);
}

double rankOneUpdate(double* z, const double* a, const double* h, const double* w,
                     std::size_t n) noexcept {
    return rankOneImpl<double, kCanonicalLanesF64>(z, a, h, w, n);
}

// SPEC-EXEC: chunks of 64 along K summed in chunk order; each chunk in the canonical 16-lane order.
void matmul(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C,
            std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) noexcept {
    for (std::size_t m = 0; m < M; ++m) {
        for (std::size_t n = 0; n < N; ++n) {
            float total = 0.0F;
            for (std::size_t k0 = 0; k0 < K; k0 += kMatmulChunk) {
                const std::size_t len = (K - k0 < kMatmulChunk) ? (K - k0) : kMatmulChunk;
                float lanes[kCanonicalLanesF32] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
                for (std::size_t kk = 0; kk < len; ++kk) {
                    const std::size_t lane = kk % kCanonicalLanesF32;
                    lanes[lane] =
                        lanes[lane] + A[m * lda + k0 + kk] * B[(k0 + kk) * ldb + n];
                }
                total = total + combineLanes<float, kCanonicalLanesF32>(lanes);
            }
            C[m * ldc + n] = total;
        }
    }
}

}  // namespace pme::simd::reference
