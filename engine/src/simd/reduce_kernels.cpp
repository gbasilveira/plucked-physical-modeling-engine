// SPDX-License-Identifier: MIT
// Dot products (A8-R2) and the rank-one Woodbury vector operations (A8-R3), compiled once per
// instruction set by Highway's foreach_target and chosen at run time (A8-R5, ADR-0001).
//
// SPEC sections implemented: SPEC-BRG "Bridge force" (F_b = sum a_n q_n - (T/L) w_b, the dot
// product), SPEC-BRG "Bridge scheme" (a^T b and q = b - h~ w), SPEC-NL "Explicit low-rank solve"
// (r = 1: z = a - h (w^T a) / (1 + w^T h)), SPEC-EXEC "Threading contract" (fixed reduction order).
//
// Canonical order: element i belongs to virtual lane i mod L (L = 16 for float32, 8 for float64),
// each lane adds its elements in index order, and the lane sums combine by the fixed tree of
// combineLanes. With vectors of N lanes the L virtual lanes live in L / N accumulator vectors, so
// the result does not depend on N. MulAdd is fused on AVX2, AVX-512 and arm64 and split elsewhere
// (ADR-0001); targets differ only through that contraction.
#include "highway_config.hpp"  // first: defines HWY_DISABLED_TARGETS

#include "pme/simd/canonical.hpp"
#include "pme/simd/kernels.hpp"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "../reduce_kernels.cpp"  // relative to engine/src/simd/include
#include "hwy/foreach_target.h"  // IWYU pragma: keep; must come before highway.h
#include "hwy/highway.h"
#include "block-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace pme::simd::HWY_NAMESPACE {
namespace {

// Tag of the accumulator vectors: at most `kLanes` lanes, so that kLanes is a multiple of Lanes(d).
template <class T, std::size_t kLanes>
using AccTag = hn::CappedTag<T, kLanes>;

template <class D>
constexpr std::size_t LanesOf() {
    return HWY_MAX_LANES_D(D);
}

// sum_i a_i b_i in the canonical order with kLanes virtual lanes.
template <class T, std::size_t kLanes>
HWY_INLINE T DotCanonical(const T* HWY_RESTRICT a, const T* HWY_RESTRICT b, std::size_t n) {
    const AccTag<T, kLanes> d;
    constexpr std::size_t kN = LanesOf<decltype(d)>();
    static_assert(kLanes % kN == 0, "the vector width divides the virtual lane count");
    constexpr std::size_t kV = kLanes / kN;
    hn::VFromD<decltype(d)> acc[kV];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        acc[j] = hn::Zero(d);
    }
    std::size_t i = 0;
    for (; i + kLanes <= n; i += kLanes) {
        for (std::size_t j = 0; j < kV; ++j) {
            acc[j] = hn::MulAdd(hn::LoadU(d, a + i + j * kN), hn::LoadU(d, b + i + j * kN), acc[j]);
        }
    }
    if (i < n) {  // tail: the remaining elements go to their lanes like any other
        const std::size_t rem = n - i;
        for (std::size_t j = 0; j < kV && j * kN < rem; ++j) {
            const std::size_t cnt = (rem - j * kN < kN) ? (rem - j * kN) : kN;
            acc[j] = hn::MulAdd(hn::LoadN(d, a + i + j * kN, cnt), hn::LoadN(d, b + i + j * kN, cnt),
                                acc[j]);
        }
    }
    alignas(64) T lanes[kLanes];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        hn::Store(acc[j], d, lanes + j * kN);
    }
    return combineLanes<T, kLanes>(lanes);
}

// Both w.a and w.h in one pass, canonical order each.
template <class T, std::size_t kLanes>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
HWY_INLINE void Dot2Canonical(const T* HWY_RESTRICT w, const T* HWY_RESTRICT a,
                              const T* HWY_RESTRICT h, std::size_t n, T& wa, T& wh) {
    const AccTag<T, kLanes> d;
    constexpr std::size_t kN = LanesOf<decltype(d)>();
    static_assert(kLanes % kN == 0, "the vector width divides the virtual lane count");
    constexpr std::size_t kV = kLanes / kN;
    hn::VFromD<decltype(d)> accA[kV];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    hn::VFromD<decltype(d)> accH[kV];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        accA[j] = hn::Zero(d);
        accH[j] = hn::Zero(d);
    }
    std::size_t i = 0;
    for (; i + kLanes <= n; i += kLanes) {
        for (std::size_t j = 0; j < kV; ++j) {
            const auto wv = hn::LoadU(d, w + i + j * kN);
            accA[j] = hn::MulAdd(wv, hn::LoadU(d, a + i + j * kN), accA[j]);
            accH[j] = hn::MulAdd(wv, hn::LoadU(d, h + i + j * kN), accH[j]);
        }
    }
    if (i < n) {
        const std::size_t rem = n - i;
        for (std::size_t j = 0; j < kV && j * kN < rem; ++j) {
            const std::size_t cnt = (rem - j * kN < kN) ? (rem - j * kN) : kN;
            const auto wv = hn::LoadN(d, w + i + j * kN, cnt);
            accA[j] = hn::MulAdd(wv, hn::LoadN(d, a + i + j * kN, cnt), accA[j]);
            accH[j] = hn::MulAdd(wv, hn::LoadN(d, h + i + j * kN, cnt), accH[j]);
        }
    }
    alignas(64) T lanesA[kLanes];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    alignas(64) T lanesH[kLanes];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        hn::Store(accA[j], d, lanesA + j * kN);
        hn::Store(accH[j], d, lanesH + j * kN);
    }
    wa = combineLanes<T, kLanes>(lanesA);
    wh = combineLanes<T, kLanes>(lanesH);
}

// float32 inputs, float64 products and accumulation, 8 virtual lanes. The accumulator tag is the
// double tag; the float vector is the half-width tag with the same lane count. The product of two
// floats is exact in double, so MulAdd and Mul+Add give the same bits on every target.
HWY_INLINE double DotMixed(const float* HWY_RESTRICT a, const float* HWY_RESTRICT b,
                           std::size_t n) {
    constexpr std::size_t kLanes = kCanonicalLanesF64;
    const AccTag<double, kLanes> dd;
    const hn::Rebind<float, decltype(dd)> df;
    constexpr std::size_t kN = LanesOf<decltype(dd)>();
    static_assert(kLanes % kN == 0, "the vector width divides the virtual lane count");
    constexpr std::size_t kV = kLanes / kN;
    hn::VFromD<decltype(dd)> acc[kV];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        acc[j] = hn::Zero(dd);
    }
    std::size_t i = 0;
    for (; i + kLanes <= n; i += kLanes) {
        for (std::size_t j = 0; j < kV; ++j) {
            const auto x = hn::PromoteTo(dd, hn::LoadU(df, a + i + j * kN));
            const auto y = hn::PromoteTo(dd, hn::LoadU(df, b + i + j * kN));
            acc[j] = hn::MulAdd(x, y, acc[j]);
        }
    }
    if (i < n) {
        const std::size_t rem = n - i;
        for (std::size_t j = 0; j < kV && j * kN < rem; ++j) {
            const std::size_t cnt = (rem - j * kN < kN) ? (rem - j * kN) : kN;
            const auto x = hn::PromoteTo(dd, hn::LoadN(df, a + i + j * kN, cnt));
            const auto y = hn::PromoteTo(dd, hn::LoadN(df, b + i + j * kN, cnt));
            acc[j] = hn::MulAdd(x, y, acc[j]);
        }
    }
    alignas(64) double lanes[kLanes];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    for (std::size_t j = 0; j < kV; ++j) {
        hn::Store(acc[j], dd, lanes + j * kN);
    }
    return combineLanes<double, kLanes>(lanes);
}

// z_i = a_i - c h_i (SPEC-NL r = 1; SPEC-BRG q = b - h~ w). z may alias a.
template <class T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
HWY_INLINE void AxpyNegImpl(T* z, const T* a, const T* h, T c, std::size_t n) {
    const hn::ScalableTag<T> d;
    const std::size_t lanes = hn::Lanes(d);
    const auto cv = hn::Set(d, c);
    std::size_t i = 0;
    for (; i + lanes <= n; i += lanes) {
        hn::StoreU(hn::NegMulAdd(cv, hn::LoadU(d, h + i), hn::LoadU(d, a + i)), d, z + i);
    }
    if (i < n) {
        const std::size_t cnt = n - i;
        hn::StoreN(hn::NegMulAdd(cv, hn::LoadN(d, h + i, cnt), hn::LoadN(d, a + i, cnt)), d, z + i,
                   cnt);
    }
}

}  // namespace

// Entry points exported through HWY_EXPORT, one per target.
float DotF32(const float* a, const float* b, std::size_t n) {
    return DotCanonical<float, kCanonicalLanesF32>(a, b, n);
}
double DotF64(const double* a, const double* b, std::size_t n) {
    return DotCanonical<double, kCanonicalLanesF64>(a, b, n);
}
double DotMixedF64(const float* a, const float* b, std::size_t n) { return DotMixed(a, b, n); }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void Dot2F32(const float* w, const float* a, const float* h, std::size_t n, float* wa, float* wh) {
    Dot2Canonical<float, kCanonicalLanesF32>(w, a, h, n, *wa, *wh);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void Dot2F64(const double* w, const double* a, const double* h, std::size_t n, double* wa,
             double* wh) {
    Dot2Canonical<double, kCanonicalLanesF64>(w, a, h, n, *wa, *wh);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void AxpyNegF32(float* z, const float* a, const float* h, float c, std::size_t n) {
    AxpyNegImpl<float>(z, a, h, c, n);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void AxpyNegF64(double* z, const double* a, const double* h, double c, std::size_t n) {
    AxpyNegImpl<double>(z, a, h, c, n);
}

}  // namespace pme::simd::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace pme::simd {

HWY_EXPORT(DotF32);
HWY_EXPORT(DotF64);
HWY_EXPORT(DotMixedF64);
HWY_EXPORT(Dot2F32);
HWY_EXPORT(Dot2F64);
HWY_EXPORT(AxpyNegF32);
HWY_EXPORT(AxpyNegF64);

float dot(const float* a, const float* b, std::size_t n) noexcept {
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(b);
    return HWY_DYNAMIC_DISPATCH(DotF32)(a, b, n);
}

double dot(const double* a, const double* b, std::size_t n) noexcept {
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(b);
    return HWY_DYNAMIC_DISPATCH(DotF64)(a, b, n);
}

double dotF64(const float* a, const float* b, std::size_t n) noexcept {
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(b);
    return HWY_DYNAMIC_DISPATCH(DotMixedF64)(a, b, n);
}

void dot2(const float* w, const float* a, const float* h, std::size_t n, float& wa,
          float& wh) noexcept {
    PME_SIMD_ASSERT_ALIGNED(w);
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(h);
    HWY_DYNAMIC_DISPATCH(Dot2F32)(w, a, h, n, &wa, &wh);
}

void dot2(const double* w, const double* a, const double* h, std::size_t n, double& wa,
          double& wh) noexcept {
    PME_SIMD_ASSERT_ALIGNED(w);
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(h);
    HWY_DYNAMIC_DISPATCH(Dot2F64)(w, a, h, n, &wa, &wh);
}

void axpyNeg(float* z, const float* a, const float* h, float c, std::size_t n) noexcept {
    PME_SIMD_ASSERT_ALIGNED(z);
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(h);
    HWY_DYNAMIC_DISPATCH(AxpyNegF32)(z, a, h, c, n);
}

void axpyNeg(double* z, const double* a, const double* h, double c, std::size_t n) noexcept {
    PME_SIMD_ASSERT_ALIGNED(z);
    PME_SIMD_ASSERT_ALIGNED(a);
    PME_SIMD_ASSERT_ALIGNED(h);
    HWY_DYNAMIC_DISPATCH(AxpyNegF64)(z, a, h, c, n);
}

// SPEC-NL explicit low-rank solve, r = 1: z = a - h (w^T a) / (1 + w^T h).
float rankOneUpdate(float* z, const float* a, const float* h, const float* w,
                    std::size_t n) noexcept {
    float wa = 0.0F;
    float wh = 0.0F;
    dot2(w, a, h, n, wa, wh);
    const float c = wa / (1.0F + wh);
    axpyNeg(z, a, h, c, n);
    return c;
}

double rankOneUpdate(double* z, const double* a, const double* h, const double* w,
                     std::size_t n) noexcept {
    double wa = 0.0;
    double wh = 0.0;
    dot2(w, a, h, n, wa, wh);
    const double c = wa / (1.0 + wh);
    axpyNeg(z, a, h, c, n);
    return c;
}

}  // namespace pme::simd
#endif  // HWY_ONCE
