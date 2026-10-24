// SPDX-License-Identifier: MIT
// Small matrix-matrix kernel (A8-R4), compiled once per instruction set by Highway's
// foreach_target and chosen at run time (A8-R5, ADR-0001).
//
// SPEC sections implemented: SPEC-EXEC "Threading contract" (fixed reduction order: global body
// modes in fixed chunks of 64 summed in chunk order), SPEC-BODY projection and velocity products
// (shapes only; the body itself is C2).
//
// C = A B, row-major. The vectors run along N (the output columns), so every output element is its
// own scalar reduction over K, in an order that does not depend on the vector width: K is cut into
// chunks of 64; inside a chunk, k goes to virtual lane (k mod 16) and each lane adds its products
// in index order; the 16 lane sums combine by the pairwise tree (0+8, ..., 0+4, ..., 0+2, 0+1) of
// combineLanes; the chunk sums are added to C in chunk order. MulAdd is fused on AVX2, AVX-512 and
// arm64 and split elsewhere (ADR-0001).
#include "highway_config.hpp"  // first: defines HWY_DISABLED_TARGETS
#include "pme/simd/kernels.hpp"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "../matmul_kernels.cpp"  // NOLINT(cppcoreguidelines-macro-usage) relative to engine/src/simd/include
#include "hwy/foreach_target.h"  // IWYU pragma: keep; must come before highway.h
#include "hwy/highway.h"
#include "block-inl.h"
// clang-format on

// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,modernize-loop-convert)
HWY_BEFORE_NAMESPACE();
namespace pme::simd::HWY_NAMESPACE {
namespace {

// One block of output columns [n0, n0 + cnt) for every row. The B chunk (64 rows of one vector)
// stays in cache across the rows m.
template <bool kPartial, class D>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): BLAS-style argument order
HWY_INLINE void MatmulColumns(D d, const float* HWY_RESTRICT A, std::size_t lda,
                              const float* HWY_RESTRICT B, std::size_t ldb, float* HWY_RESTRICT C,
                              std::size_t ldc, std::size_t M, std::size_t K, std::size_t n0,
                              std::size_t cnt) {
    using V = hn::VFromD<D>;
    constexpr std::size_t kLanes = kCanonicalLanesF32;
    for (std::size_t k0 = 0; k0 < K; k0 += kMatmulChunk) {
        const std::size_t len = (K - k0 < kMatmulChunk) ? (K - k0) : kMatmulChunk;
        for (std::size_t m = 0; m < M; ++m) {
            V acc[kLanes];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
            for (V& a : acc) {
                a = hn::Zero(d);
            }
            const float* arow = A + m * lda + k0;
            const float* bcol = B + k0 * ldb + n0;
            for (std::size_t kk = 0; kk < len; ++kk) {
                // virtual lane kk mod 16 (the chunk starts at a multiple of 64)
                const std::size_t l = kk % kLanes;
                acc[l] = hn::MulAdd(hn::Set(d, arow[kk]),
                                    LoadBlock<kPartial>(d, bcol + kk * ldb, cnt), acc[l]);
            }
            // pairwise tree over the 16 virtual lanes: (0+8, ...), (0+4, ...), (0+2, ...), (0+1)
            for (std::size_t half = kLanes / 2; half >= 1; half /= 2) {
                for (std::size_t l = 0; l < half; ++l) {
                    acc[l] = hn::Add(acc[l], acc[l + half]);
                }
            }
            // chunk sums are added in chunk order, starting from zero (as the scalar reference
            // does)
            float* crow = C + m * ldc + n0;
            const V prev = (k0 == 0) ? hn::Zero(d) : LoadBlock<kPartial>(d, crow, cnt);
            StoreBlock<kPartial>(hn::Add(prev, acc[0]), d, crow, cnt);
        }
    }
}

}  // namespace

// Entry point exported through HWY_EXPORT, one per target.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): BLAS-style argument order
void MatmulF32(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C,
               std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) {
    const hn::ScalableTag<float> d;
    const std::size_t lanes = hn::Lanes(d);
    if (K == 0) {  // empty sum: C = 0
        for (std::size_t m = 0; m < M; ++m) {
            for (std::size_t n = 0; n < N; ++n) {
                C[m * ldc + n] = 0.0F;
            }
        }
        return;
    }
    std::size_t n0 = 0;
    for (; n0 + lanes <= N; n0 += lanes) {
        MatmulColumns<false>(d, A, lda, B, ldb, C, ldc, M, K, n0, lanes);
    }
    if (n0 < N) {
        MatmulColumns<true>(d, A, lda, B, ldb, C, ldc, M, K, n0, N - n0);
    }
}

}  // namespace pme::simd::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace pme::simd {

HWY_EXPORT(MatmulF32);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): BLAS-style argument order
void matmul(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C,
            std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) noexcept {
    PME_SIMD_ASSERT_ALIGNED(A);
    PME_SIMD_ASSERT_ALIGNED(B);
    PME_SIMD_ASSERT_ALIGNED(C);
    HWY_DYNAMIC_DISPATCH(MatmulF32)(A, lda, B, ldb, C, ldc, M, N, K);
}

}  // namespace pme::simd
#endif  // HWY_ONCE

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,modernize-loop-convert)
