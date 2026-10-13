// SPDX-License-Identifier: MIT
// Internal SIMD kernels (A8-R1 to A8-R4), runtime-dispatched over the instruction sets that
// Highway compiled in (ADR-0001). No Highway header is included here: the declarations are plain
// C++, and the Highway code lives in the .cpp files of engine/src/simd.
//
// SPEC sections implemented: SPEC-OSC "Difference form" (stepModes), SPEC-BRG "Bridge force" and
// "Bridge scheme" (dot, axpyNeg), SPEC-NL "Explicit low-rank solve" (dot2, axpyNeg,
// rankOneUpdate), SPEC-EXEC "Threading contract" (canonical reduction order, matmul chunks of 64).
//
// Preconditions common to every kernel (checked by assert in debug builds):
//  - pointers are 64-byte aligned (pme::osc::AlignedArray provides this);
//  - for n elements, the arrays hold at least n elements; nothing is read or written past n.
// Every kernel is noexcept and allocation-free; call `initialise()` once (non-audio thread) before
// the first audio-thread call so that CPU detection does not run in `process`.
#pragma once

#include <cstddef>

namespace pme::simd {

/// Required alignment in bytes of every kernel pointer (one cache line, whole AVX-512 vectors).
inline constexpr std::size_t kAlignment = 64;
/// Virtual lanes of every float32 reduction (index mod 16), whatever the vector width.
inline constexpr std::size_t kCanonicalLanesF32 = 16;
/// Virtual lanes of every float64 reduction (index mod 8).
inline constexpr std::size_t kCanonicalLanesF64 = 8;
/// Chunk length along K of `matmul` (SPEC-EXEC: global body modes in fixed chunks of 64).
inline constexpr std::size_t kMatmulChunk = 64;

// ---- A8-R1: mode-bank update --------------------------------------------------------------

/// SPEC-OSC difference form, one sample for modes 0..n-1, exactly the operation order of
/// pme::osc::stepModes<float, float>: u' = (beta u - kappa q) + gamma f, q' = q + u'. No fused
/// multiply-add is used, so the result is bit-identical to the scalar loop on every target.
/// `force` may be null (no force). If `qPrevOut` / `uPrevOut` are non-null they receive the
/// values before the step (q^n, u^n). Thread: audio. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(float* q, float* u, const float* beta, const float* kappa, const float* gamma,
               const float* force, std::size_t n, float* qPrevOut = nullptr,
               float* uPrevOut = nullptr) noexcept;

/// float64 state and coefficients (tests of A3). Same arithmetic as the float kernel.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* force, std::size_t n, double* qPrevOut = nullptr,
               double* uPrevOut = nullptr) noexcept;

/// float64 state with float32 coefficients: pme::osc::stepModes<double, float>.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(double* q, double* u, const float* beta, const float* kappa, const float* gamma,
               const double* force, std::size_t n, double* qPrevOut = nullptr,
               double* uPrevOut = nullptr) noexcept;

// ---- A8-R2: dot products ------------------------------------------------------------------
//
// Canonical reduction order: element i goes to virtual lane i mod 16 (float32) or i mod 8
// (float64); each lane accumulates in index order; the lanes combine pairwise (0+8, 1+9, ...,
// then 0+4, ..., 0+2, 0+1). The order does not depend on the vector width.

/// sum a_i b_i, float32 accumulation in 16 virtual lanes. Serves the bridge force
/// sum a_n q_n (SPEC-BRG), the hammer contact displacement and the implicit scheme's a^T b.
[[nodiscard]] float dot(const float* a, const float* b, std::size_t n) noexcept;

/// sum a_i b_i, float64, 8 virtual lanes.
[[nodiscard]] double dot(const double* a, const double* b, std::size_t n) noexcept;

/// float32 inputs, float64 products and accumulation (A3 forms reductions in float64 from float32
/// states), 8 virtual lanes. The products are exact in float64, so the result is bit-identical
/// on every target.
[[nodiscard]] double dotF64(const float* a, const float* b, std::size_t n) noexcept;

// ---- A8-R3: rank-one Woodbury vector operations -------------------------------------------

/// wa = sum w_i a_i and wh = sum w_i h_i in one pass, canonical order (SPEC-NL r = 1).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void dot2(const float* w, const float* a, const float* h, std::size_t n, float& wa,
          float& wh) noexcept;
/// float64 overload of `dot2`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void dot2(const double* w, const double* a, const double* h, std::size_t n, double& wa,
          double& wh) noexcept;

/// z_i = a_i - c h_i. `z` may alias `a` (in place). Serves the rank-one solve (SPEC-NL) and the
/// implicit bridge scheme's q^{n+1} = b - h~ w^{n+1} (SPEC-BRG), also for u^{n+1}.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void axpyNeg(float* z, const float* a, const float* h, float c, std::size_t n) noexcept;
/// float64 overload of `axpyNeg`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void axpyNeg(double* z, const double* a, const double* h, double c, std::size_t n) noexcept;

/// SPEC-NL "Explicit low-rank solve", r = 1: z = a - h (w^T a) / (1 + w^T h), that is `dot2`
/// then `axpyNeg` with c = wa / (1 + wh). Returns c. `z` may alias `a`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h, w in SPEC order
float rankOneUpdate(float* z, const float* a, const float* h, const float* w,
                    std::size_t n) noexcept;
/// float64 overload of `rankOneUpdate`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h, w in SPEC order
double rankOneUpdate(double* z, const double* a, const double* h, const double* w,
                     std::size_t n) noexcept;

// ---- A8-R4: small matrix-matrix product ---------------------------------------------------

/// C = A B, row-major: A is M x K (leading dimension lda), B is K x N (ldb), C is M x N (ldc).
/// Vectorised along N. The reduction over K follows SPEC-EXEC: fixed chunks of 64 along K, each
/// chunk reduced in the canonical 16-lane order (k mod 16 within the chunk), the chunk sums added
/// to C in chunk order. float32 accumulation. C must not overlap A or B.
/// Typical shapes: (K x P)(P x S) projection and (P x K)(K x S) velocity products of C2.
/// Base pointers are 64-byte aligned; row strides are free (unaligned loads).
void matmul(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C,
            std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) noexcept;

// ---- scalar references (outside Highway, not auto-vectorised) -----------------------------

/// Plain C++ element-at-a-time versions of every kernel above, with the same signatures and the
/// same arithmetic order and no fused multiply-add. They are the references of A8-T1 and A8-T3.
/// The translation unit is compiled with the compiler's auto-vectoriser switched off
/// (docs/dev/simd.md, "Scalar references").
namespace reference {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(float* q, float* u, const float* beta, const float* kappa, const float* gamma,
               const float* force, std::size_t n, float* qPrevOut = nullptr,
               float* uPrevOut = nullptr) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(double* q, double* u, const double* beta, const double* kappa, const double* gamma,
               const double* force, std::size_t n, double* qPrevOut = nullptr,
               double* uPrevOut = nullptr) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SoA pointers in SPEC order
void stepModes(double* q, double* u, const float* beta, const float* kappa, const float* gamma,
               const double* force, std::size_t n, double* qPrevOut = nullptr,
               double* uPrevOut = nullptr) noexcept;

[[nodiscard]] float dot(const float* a, const float* b, std::size_t n) noexcept;
[[nodiscard]] double dot(const double* a, const double* b, std::size_t n) noexcept;
[[nodiscard]] double dotF64(const float* a, const float* b, std::size_t n) noexcept;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void dot2(const float* w, const float* a, const float* h, std::size_t n, float& wa,
          float& wh) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): w, a, h in SPEC order
void dot2(const double* w, const double* a, const double* h, std::size_t n, double& wa,
          double& wh) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void axpyNeg(float* z, const float* a, const float* h, float c, std::size_t n) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h in SPEC order
void axpyNeg(double* z, const double* a, const double* h, double c, std::size_t n) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h, w in SPEC order
float rankOneUpdate(float* z, const float* a, const float* h, const float* w,
                    std::size_t n) noexcept;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): z, a, h, w in SPEC order
double rankOneUpdate(double* z, const double* a, const double* h, const double* w,
                     std::size_t n) noexcept;

void matmul(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C,
            std::size_t ldc, std::size_t M, std::size_t N, std::size_t K) noexcept;

}  // namespace reference

}  // namespace pme::simd
