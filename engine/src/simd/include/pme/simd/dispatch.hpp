// SPDX-License-Identifier: MIT
// Runtime dispatch control (A8-R5, ADR-0001): resolve the instruction set off the audio thread,
// and a test hook that runs the kernels on each supported target in turn.
//
// SPEC sections implemented: PLAN-COMP "SIMD"; Real-time rules (no CPU detection in `process`).
#pragma once

#include <cstddef>

namespace pme::simd {

/// Resolve the dispatch (the first dynamic-dispatch call detects the CPU) and return the name of
/// the chosen target, for example "AVX2" or "NEON". The pointer is a string literal. Call from a
/// non-audio thread (`Engine::prepare`). Idempotent. Allocates: yes (first call).
const char* initialise() noexcept;

/// True if the active target's multiply-add is a fused instruction (AVX2, AVX-512, NEON on
/// arm64). Reductions, `axpyNeg` and `matmul` then differ from the unfused scalar reference in the
/// last bits only; `stepModes` and `dotF64` never use fused arithmetic.
[[nodiscard]] bool activeTargetIsFused() noexcept;

/// Name of the active target (as `initialise()` returns it).
[[nodiscard]] const char* activeTargetName() noexcept;

// ---- test hook -----------------------------------------------------------------------------

/// Number of targets that were compiled in and that this CPU supports, best first. Test code only.
[[nodiscard]] std::size_t testTargetCount() noexcept;

/// Name of the supported target `index` (< testTargetCount()). Test code only.
[[nodiscard]] const char* testTargetName(std::size_t index) noexcept;

/// Restrict dispatch to the supported target `index` (hwy::SetSupportedTargetsForTest). Not
/// thread-safe; call with no kernel running. Test code only. Returns false for a bad index.
bool testSelectTarget(std::size_t index) noexcept;

/// Undo `testSelectTarget`: dispatch picks the best target again. Test code only.
void testResetTarget() noexcept;

}  // namespace pme::simd
