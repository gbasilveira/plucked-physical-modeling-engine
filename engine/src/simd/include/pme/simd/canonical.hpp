// SPDX-License-Identifier: MIT
// The canonical reduction order of A8 (A8-R2, A8-R4; Numerics rule 5, SPEC-EXEC), in plain C++.
// Shared by the scalar references and the Highway kernels, so that the final combine step of every
// reduction is the same code on every target.
#pragma once

#include <cstddef>

namespace pme::simd {

/// Combine `Lanes` lane sums by the fixed pairwise tree (0+Lanes/2, 1+..., then 0+Lanes/4, ...,
/// finally 0+1). `Lanes` is a power of two. `lanes` is overwritten. SPEC-EXEC fixed order.
template <class T, std::size_t Lanes>
[[nodiscard]] inline T combineLanes(T* lanes) noexcept {
    static_assert(Lanes != 0 && (Lanes & (Lanes - 1)) == 0, "Lanes is a power of two");
    for (std::size_t half = Lanes / 2; half >= 1; half /= 2) {
        for (std::size_t i = 0; i < half; ++i) {
            lanes[i] = lanes[i] + lanes[i + half];
        }
    }
    return lanes[0];
}

}  // namespace pme::simd
