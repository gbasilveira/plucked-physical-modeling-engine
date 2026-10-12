// SPDX-License-Identifier: MIT
// Highway configuration shared by every kernel translation unit of pme_simd (ADR-0001). Include it
// before any Highway header.
//
// The canonical reduction order keeps its accumulators in a fixed-size array of vectors, which
// needs vector types of a known size. Highway's SVE and RVV targets use sizeless vectors, so they
// are not compiled. NEON, SSE4, AVX2 and AVX-512 (and the emulated and scalar fallbacks) are.
#pragma once

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (HWY_ALL_SVE | HWY_RVV)
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>

/// Debug check of the 64-byte alignment precondition of the 1-D kernels (a null pointer passes).
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): assert-like macro, expands to nothing in release
#define PME_SIMD_ASSERT_ALIGNED(p)                                                             \
    assert(((p) == nullptr || reinterpret_cast<std::uintptr_t>(p) % 64U == 0U) && /* NOLINT */ \
           "pme::simd kernel pointers must be 64-byte aligned")
