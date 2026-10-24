// SPDX-License-Identifier: MIT
// Placeholder so that pme_barriers is a buildable library target (P0.1-R1).
// Replaced by stub sources in P0.4 and by the real module in P2 (distributed barriers).
namespace pme::detail {
int placeholder_barriers() noexcept;
int placeholder_barriers() noexcept { return 0; }
}  // namespace pme::detail
