# SIMD kernels (A8)

SPEC and PLAN sections implemented: PLAN-COMP "SIMD" and "Parallel structure"; SPEC-EXEC "Threading
contract" (fixed reduction order, global body modes in fixed chunks of 64, denormals); SPEC-OSC
"Difference form"; SPEC-BRG "Bridge force" and "Bridge scheme"; SPEC-NL "Explicit low-rank solve"
(r = 1). ADR-0001 (Highway) is applied as written. Everything is internal, namespace `pme::simd`,
headers under `engine/src/simd/include/pme/simd/`, target `pme_simd`. No Highway header is included
outside `engine/src/simd/`, and the public headers contain no Highway types. A8 wires nothing into
osc, string, bridge or nonlinear: C1 composes the kernels, and those modules keep their scalar loops
as references.

| File | Content | Requirement |
| --- | --- | --- |
| `kernels.hpp` | declarations of all kernels and of `reference::*` | A8-R1 to A8-R4 |
| `dispatch.hpp`, `dispatch.cpp` | `initialise`, `activeTargetName`, `activeTargetIsFused`, test hook | A8-R5 |
| `denormals.hpp`, `denormals.cpp` | `ScopedFlushDenormals` | A8-R5 |
| `mode_kernels.cpp` | `stepModes` (float, double, double with float coefficients) | A8-R1 |
| `reduce_kernels.cpp` | `dot`, `dotF64`, `dot2`, `axpyNeg`, `rankOneUpdate` | A8-R2, A8-R3 |
| `matmul_kernels.cpp` | `matmul` | A8-R4 |
| `reference.cpp` | scalar references | A8-T1, A8-T3 |
| `canonical.hpp`, `highway_config.hpp`, `block-inl.h`, `target_description.hpp` | shared helpers | |

## Kernels and their scalar counterparts

| Kernel | Scalar counterpart outside this module |
| --- | --- |
| `stepModes(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut)` | `pme::osc::stepModes<float, float>`, `ModeBank::step`; also `<double, double>` and `<double, float>` |
| `dot(a, b, n)` (float, double), `dotF64(a, b, n)` (float inputs, double accumulation) | `pme::bridge::bridgeForce` (the sum), A3's float64 reductions |
| `dot2(w, a, h, n, wa, wh)`, `axpyNeg(z, a, h, c, n)`, `rankOneUpdate(z, a, h, w, n)` | `pme::nl::lowRankSolve` with r = 1 |
| `matmul(A, lda, B, ldb, C, ldc, M, N, K)` | none yet (C2's projection and velocity products) |

The mixed dot is named `dotF64` because C++ cannot overload on the return type. `rankOneUpdate`
returns c = (wᵀa)/(1 + wᵀh). `force == nullptr` in `stepModes` means no force.

**Preconditions.** 64-byte-aligned base pointers (`AlignedArray` provides them); debug builds
assert it. Row strides of `matmul` are free. Nothing is read or written past n. All kernels are
`noexcept` and allocation-free.

## Canonical reduction order (A8-R2, A8-R4)

Every reduction uses one order that does not depend on the vector width:

- element i belongs to virtual lane i mod 16 (float32) or i mod 8 (float64);
- each virtual lane adds its elements in index order, tail elements included;
- the lane sums combine by the pairwise tree (0+8, 1+9, …), then (0+4, …), then (0+2, …), then
  (0+1) (`combineLanes`, plain C++ run after the lanes are stored).

AVX-512 holds the 16 lanes in one float vector, AVX2 in two, SSE and NEON in four, the scalar
fallback in sixteen scalars (the accumulator tag is `CappedTag<float, 16>`, so the number of vectors
is 16 / Lanes). `dotF64` and the float64 `dot` use 8 lanes the same way.

`matmul` vectorises along N, so each output element is its own reduction over K: K is cut into
chunks of 64 (SPEC-EXEC); inside a chunk k goes to lane (k mod 16) and the 16 lane vectors combine
by the same tree; the chunk sums are added to C in chunk order, starting from zero. The result does
not depend on the vector width.

## Fused multiply-add

`stepModes` and `dotF64` use no fused arithmetic (separate multiply and add; the products of
`dotF64` are exact), so they are **bit-identical to the scalar loop on every target**. The other
kernels use `MulAdd` (`NegMulAdd` in `axpyNeg`): fused on AVX2, AVX-512 and arm64 and split on SSE
and the scalar fallback (ADR-0001). On non-fused targets they are bit-identical to the references;
on fused targets they differ in the last bits. The build uses `-ffp-contract=off`, so the compiler
never contracts anything on its own. `activeTargetIsFused()` tells the tests which case applies.

## Dispatch (A8-R5)

Kernel files are compiled once per target with `foreach_target.h` (`HWY_TARGET_INCLUDE` is
relative to `engine/src/simd/include`). `HWY_DYNAMIC_DISPATCH` selects the target. SVE and RVV are
disabled in `highway_config.hpp` (the canonical order keeps its accumulators in an array of
vectors, which needs sized vector types). `pme::simd::initialise()` (non-audio) detects the CPU and
returns the chosen target's name; `Engine::prepare` / C1 call it so that `process` never detects. The
test hook `testTargetCount/Name/Select/Reset` wraps `hwy::SetSupportedTargetsForTest` and lists the
targets that are compiled in **and** supported.

## Denormals

`ScopedFlushDenormals`: MXCSR FTZ|DAZ on x86, FPCR.FZ on arm64 (`_ReadStatusReg(ARM64_FPCR)` on MSVC),
no-op elsewhere; restores the previous value on destruction; no system call. C1 constructs one at
`process` entry and on every worker thread (real-time rule 5). Resolves A1's carried item.

## Scalar references

`reference.cpp` is plain C++, one element at a time, with the same arithmetic order. It is compiled
with the auto-vectoriser off: `-fno-tree-vectorize -fno-tree-slp-vectorize` (GCC), `-fno-vectorize
-fno-slp-vectorize` (Clang, AppleClang), `/Qvec-` (MSVC; clang-cl adds `/clang:-fno-slp-vectorize`),
set as source properties in the A8 block of the root CMakeLists.txt.

## Bridge schemes (D-13)

No kernel encodes the coupling order; both schemes compose from the same primitives:

- explicit: `dot` per bank (F_b) → scalar bridge advance → `axpyNeg(force, zero, g, δ_tt w)` gives
  −g δ_tt w → `stepModes` with that force;
- implicit: `stepModes` without the bridge force (gives **b**) → `dot` (aᵀb) → scalar solve →
  `axpyNeg` for q **and for u** (qⁿ⁺¹ = b − h̃ wⁿ⁺¹, and uⁿ⁺¹ = u_b − h̃ wⁿ⁺¹, because qⁿ⁺¹ = qⁿ + uⁿ⁺¹).
  The brief names one `axpyNeg`; the state needs two (note for A9 and C1).

`bridge_scheme_bench` (perf, 3 segments × 59 modes × 2 polarisations, 64 samples per call, scalar
bridge model with fixed constants) measured on Intel Xeon @ 2.80 GHz, linux-release, GCC 13
(median per 64-sample call; this CI-class VM is noisy, iqr 2 to 4 µs):

| Scheme | SIMD kernels | scalar references |
| --- | --- | --- |
| explicit | 17.5 µs | 95.2 µs |
| implicit | 18.0 µs | 60.8 µs |

A9's own scalar implementation of the implicit scheme is measured by A9; A8's figures are the cost
of the kernels with SIMD.

## Accuracy and error analysis of `matmul`

Against a float64 reference over all A8-T1 shapes (K up to 4096), the worst relative error is
4·10⁻⁷ for cancellation-free data and 4·10⁻⁷ for cancelling data (measured against
max(|C|, Σ|terms|/4)), far below 10⁻⁵. A float64-accumulating variant is therefore **not** offered.

## Tests and norms

- A8-T1: parity with the reference for every target that is compiled in and supported, at
  n = 1, 15, 16, 17, 64, 1023, 1024, 4099 and the matmul shapes. Norm for reductions:
  |x − ref| / max(|ref|, Σ|terms|/4); the second term only matters when the sum cancels, where no
  float32 result can beat a few roundings of the partial sums. Bound 10⁻⁶. Non-fused targets are
  checked for bit equality. `stepModes` and `dotF64` are bit-identical on all targets.
- A8-T2: 100 repeated runs and copies at other 64-byte-aligned addresses, bit-identical.
- A8-T3: median of 11 repetitions of 10 ms of `stepModes` on 1,024 modes, AVX2 target vs the scalar
  reference, ratio ≥ 4. Without AVX2 the test logs "AVX2 not available" and the best ratio. In a
  Debug build (unoptimised) the ratio is logged and not enforced: the 4× bar is a statement about
  optimised code, and the rt-safety preset (RelWithDebInfo) runs it enforced on CI.

Measured on Intel Xeon @ 2.80 GHz (linux-release, GCC 13.3), scalar 1060 ns per step: AVX2 177 ns
(5.98×), AVX3 220 ns (4.81×), SSE4 335 ns (3.16×).

Worst parity errors (linux-debug run; targets SSE2, SSSE3, SSE4, AVX2, AVX3, AVX3_DL, AVX3_SPR,
varying with the host CPU): stepModes 0 on all targets; dot f32 1.7·10⁻⁷ on AVX2 and above, 0 on SSE;
dot f64 3·10⁻¹⁶; dotF64 0; axpyNeg 4·10⁻⁷ on fused targets; rank-one vs A3 `lowRankSolve` 6·10⁻¹⁴
(bound 10⁻¹²); matmul vs reference 3·10⁻⁷ on fused targets, 0 on SSE.

## Baseline and benchmarks

`simd_bench` (perf; each kernel on each target and the scalar reference) wrote
`tools/bench/baselines/linux-x86_64-intel-xeon-2-10ghz.json`. CI-runner numbers are in the
handoff report once CI runs.

## Compilers

GCC and Clang build with the project flags. The MSVC / clang-cl decision of ADR-0001 is **open**: no
Windows machine was available to A8. If MSVC misses the 4× bar of A8-T3 on `windows-msvc` (rt-safety
preset), compile `engine/src/simd/` with clang-cl on Windows and record it here. `/Qvec-`,
`_mm_getcsr` and `_ReadStatusReg` were written to the documented MSVC interfaces but not run.
