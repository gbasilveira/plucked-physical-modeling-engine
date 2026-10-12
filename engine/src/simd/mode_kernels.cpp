// SPDX-License-Identifier: MIT
// Vectorised mode-bank update (A8-R1), compiled once per instruction set by Highway's
// foreach_target and chosen at run time (A8-R5, ADR-0001).
//
// SPEC sections implemented: SPEC-OSC "Difference form":
//   u^{n+1} = beta u^n - kappa q^n + gamma f^n,  q^{n+1} = q^n + u^{n+1}.
// The operation order is that of pme::osc::stepModes: ((beta u) - (kappa q)) + (gamma f), with
// separate multiplies and adds (never MulAdd), so every target is bit-identical to the scalar loop.
#include "highway_config.hpp"  // first: defines HWY_DISABLED_TARGETS

#include "pme/simd/kernels.hpp"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "../mode_kernels.cpp"  // relative to engine/src/simd/include
#include "hwy/foreach_target.h"  // IWYU pragma: keep; must come before highway.h
#include "hwy/highway.h"
#include "block-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace pme::simd::HWY_NAMESPACE {
namespace {

// One vector (kPartial = false) or a partial vector of `cnt` lanes starting at `off`.
template <bool kPartial, bool kForce, bool kQPrev, bool kUPrev, class D, class S, class C>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
HWY_INLINE void StepBlock(D d, S* HWY_RESTRICT q, S* HWY_RESTRICT u, const C* HWY_RESTRICT beta,
                          const C* HWY_RESTRICT kappa, const C* HWY_RESTRICT gamma,
                          const S* HWY_RESTRICT force, S* HWY_RESTRICT qPrev,
                          S* HWY_RESTRICT uPrev, std::size_t off, std::size_t cnt) {
    const auto qn = LoadBlock<kPartial>(d, q + off, cnt);
    const auto un = LoadBlock<kPartial>(d, u + off, cnt);
    if constexpr (kQPrev) {
        StoreBlock<kPartial>(qn, d, qPrev + off, cnt);
    }
    if constexpr (kUPrev) {
        StoreBlock<kPartial>(un, d, uPrev + off, cnt);
    }
    const auto b = LoadCoefBlock<kPartial>(d, beta + off, cnt);
    const auto k = LoadCoefBlock<kPartial>(d, kappa + off, cnt);
    auto next = hn::Sub(hn::Mul(b, un), hn::Mul(k, qn));  // beta u - kappa q
    if constexpr (kForce) {
        const auto g = LoadCoefBlock<kPartial>(d, gamma + off, cnt);
        next = hn::Add(next, hn::Mul(g, LoadBlock<kPartial>(d, force + off, cnt)));  // + gamma f
    }
    StoreBlock<kPartial>(next, d, u + off, cnt);
    StoreBlock<kPartial>(hn::Add(qn, next), d, q + off, cnt);  // q^{n+1} = q^n + u^{n+1}
}

template <bool kForce, bool kQPrev, bool kUPrev, class S, class C>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
HWY_INLINE void StepAll(S* q, S* u, const C* beta, const C* kappa, const C* gamma, const S* force,
                        std::size_t n, S* qPrev, S* uPrev) {
    const hn::ScalableTag<S> d;
    const std::size_t lanes = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + lanes <= n; i += lanes) {
        StepBlock<false, kForce, kQPrev, kUPrev>(d, q, u, beta, kappa, gamma, force, qPrev, uPrev,
                                                 i, lanes);
    }
    if (i < n) {
        StepBlock<true, kForce, kQPrev, kUPrev>(d, q, u, beta, kappa, gamma, force, qPrev, uPrev, i,
                                                n - i);
    }
}

// Select the compile-time variant from the run-time null checks (outside the sample loop).
template <class S, class C>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
HWY_INLINE void Step(S* q, S* u, const C* beta, const C* kappa, const C* gamma, const S* force,
                     std::size_t n, S* qPrev, S* uPrev) {
    const bool f = force != nullptr;
    const bool qp = qPrev != nullptr;
    const bool up = uPrev != nullptr;
    if (f) {
        if (qp) {
            if (up) {
                StepAll<true, true, true>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
            } else {
                StepAll<true, true, false>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
            }
        } else if (up) {
            StepAll<true, false, true>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
        } else {
            StepAll<true, false, false>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
        }
    } else {
        if (qp) {
            if (up) {
                StepAll<false, true, true>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
            } else {
                StepAll<false, true, false>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
            }
        } else if (up) {
            StepAll<false, false, true>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
        } else {
            StepAll<false, false, false>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
        }
    }
}

}  // namespace

// Entry points exported through HWY_EXPORT (extern "C"-free, one per target).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void StepModesF32(float* q, float* u, const float* beta, const float* kappa, const float* gamma,
                  const float* force, std::size_t n, float* qPrev, float* uPrev) {
    Step<float, float>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void StepModesF64(double* q, double* u, const double* beta, const double* kappa,
                  const double* gamma, const double* force, std::size_t n, double* qPrev,
                  double* uPrev) {
    Step<double, double>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void StepModesF64Coef32(double* q, double* u, const float* beta, const float* kappa,
                        const float* gamma, const double* force, std::size_t n, double* qPrev,
                        double* uPrev) {
    Step<double, float>(q, u, beta, kappa, gamma, force, n, qPrev, uPrev);
}

}  // namespace pme::simd::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace pme::simd {

HWY_EXPORT(StepModesF32);
HWY_EXPORT(StepModesF64);
HWY_EXPORT(StepModesF64Coef32);

void stepModes(float* q, float* u, const float* beta, const float* kappa, const float* gamma,
               const float* force, std::size_t n, float* qPrevOut, float* uPrevOut) noexcept {
    PME_SIMD_ASSERT_ALIGNED(q);
    PME_SIMD_ASSERT_ALIGNED(u);
    PME_SIMD_ASSERT_ALIGNED(beta);
    PME_SIMD_ASSERT_ALIGNED(kappa);
    PME_SIMD_ASSERT_ALIGNED(gamma);
    PME_SIMD_ASSERT_ALIGNED(force);
    HWY_DYNAMIC_DISPATCH(StepModesF32)(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut);
}

void stepModes(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* force, std::size_t n, double* qPrevOut, double* uPrevOut) noexcept {
    PME_SIMD_ASSERT_ALIGNED(q);
    PME_SIMD_ASSERT_ALIGNED(u);
    PME_SIMD_ASSERT_ALIGNED(beta);
    PME_SIMD_ASSERT_ALIGNED(kappa);
    PME_SIMD_ASSERT_ALIGNED(gamma);
    PME_SIMD_ASSERT_ALIGNED(force);
    HWY_DYNAMIC_DISPATCH(StepModesF64)(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut);
}

void stepModes(double* q, double* u, const float* beta, const float* kappa, const float* gamma,
               const double* force, std::size_t n, double* qPrevOut, double* uPrevOut) noexcept {
    PME_SIMD_ASSERT_ALIGNED(q);
    PME_SIMD_ASSERT_ALIGNED(u);
    PME_SIMD_ASSERT_ALIGNED(beta);
    PME_SIMD_ASSERT_ALIGNED(kappa);
    PME_SIMD_ASSERT_ALIGNED(gamma);
    PME_SIMD_ASSERT_ALIGNED(force);
    HWY_DYNAMIC_DISPATCH(StepModesF64Coef32)(q, u, beta, kappa, gamma, force, n, qPrevOut,
                                             uPrevOut);
}

}  // namespace pme::simd
#endif  // HWY_ONCE
