// SPDX-License-Identifier: MIT
// Placeholder so that pme_simd is a buildable library target (P0.1-R1).
// Replaced by stub sources in P0.4 and by the real module in A8 (SIMD kernels).
namespace pme::detail {
int placeholder_simd() noexcept;
int placeholder_simd() noexcept { return 0; }
}  // namespace pme::detail
