// SPDX-License-Identifier: MIT
// Per-target helpers shared by the kernel files of pme_simd (Highway "-inl.h" idiom: this header is
// included once per target). Loads and stores of a full vector or of a partial vector of `cnt`
// lanes; the partial forms never touch memory past `cnt` elements (Highway LoadN / StoreN) and
// zero the unused lanes.
#if defined(PME_SIMD_BLOCK_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT(llvm-header-guard)
#ifdef PME_SIMD_BLOCK_INL_H_
#undef PME_SIMD_BLOCK_INL_H_
#else
#define PME_SIMD_BLOCK_INL_H_
#endif

#include <cstddef>
#include <type_traits>

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace pme::simd::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

/// True if `MulAdd` on this target is a fused instruction (Highway HWY_NATIVE_FMA).
inline constexpr bool kFusedMulAdd = HWY_NATIVE_FMA != 0;

/// Load `cnt` lanes (all lanes of `d` if !kPartial; `cnt` is then ignored), zero-filling the rest.
template <bool kPartial, class D, class T>
HWY_INLINE hn::VFromD<D> LoadBlock(D d, const T* HWY_RESTRICT p, std::size_t cnt) {
    if constexpr (kPartial) {
        return hn::LoadN(d, p, cnt);
    } else {
        static_cast<void>(cnt);
        return hn::LoadU(d, p);
    }
}

/// Store the first `cnt` lanes (all lanes of `d` if !kPartial).
template <bool kPartial, class D, class T>
HWY_INLINE void StoreBlock(hn::VFromD<D> v, D d, T* HWY_RESTRICT p, std::size_t cnt) {
    if constexpr (kPartial) {
        hn::StoreN(v, d, p, cnt);
    } else {
        static_cast<void>(cnt);
        hn::StoreU(v, d, p);
    }
}

/// Load `cnt` coefficients of type C and convert them to the lane type of `d` (exact for
/// float to double).
template <bool kPartial, class D, class C>
HWY_INLINE hn::VFromD<D> LoadCoefBlock(D d, const C* HWY_RESTRICT p, std::size_t cnt) {
    if constexpr (std::is_same_v<C, hn::TFromD<D>>) {
        return LoadBlock<kPartial>(d, p, cnt);
    } else {
        const hn::Rebind<C, D> dc;
        return hn::PromoteTo(d, LoadBlock<kPartial>(dc, p, cnt));
    }
}

}  // namespace pme::simd::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // PME_SIMD_BLOCK_INL_H_
