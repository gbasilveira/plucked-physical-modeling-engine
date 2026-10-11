# ADR-0001: Google Highway as the SIMD library

- **Status:** Accepted
- **Date:** 2026-10-11
- **Deciders:** fixed by the Software guidelines of the phasing plan ("SIMD: Google Highway with runtime dispatch (SSE4.2, AVX2, AVX-512, NEON) by default, recorded as ADR-0001"); recorded by the planner (Opus) in P0.1.
- **Phase:** P0.1 (requirement P0.1-R6)
- **References:** PLAN-STACK (SIMD row), PLAN-COMP (SIMD paragraph), PLAN-ARCH; phasing plan, Software guidelines (Languages and dependencies, Numerics rules 2–5); phase card A8; SPEC-EXEC (fixed reduction order)

## Decision

All vector code in `engine/src/simd/` is written once against Google Highway's portable API and compiled for several instruction sets in one binary, with the target chosen at run time by Highway's dynamic dispatch. The targets that must be built and tested are SSE4.2, AVX2 and AVX-512 on x86-64 and NEON on arm64, plus Highway's scalar/emulated fallback. Other engine modules call the kernels in `engine/src/simd/`; they do not include Highway headers themselves.

## Context

- The engine's hot loops are mode-bank updates, dot products, rank-one Woodbury updates and a small matrix–matrix kernel (A8-R1 to A8-R4). They run on structure-of-arrays data, 8 or 16 lanes wide (PLAN-COMP, Parallel structure).
- The plug-in ships as one binary per system and architecture. A user's CPU may have SSE4.2 only, AVX2, or AVX-512 on x86-64; macOS ships a universal binary (arm64 plus x86-64). So the instruction set cannot be fixed at compile time; it must be chosen at run time (A8-R5).
- Every SIMD kernel needs a scalar reference and a parity test within 10⁻⁶ relative (Numerics rule 4); reductions run in a fixed order (Numerics rule 5, SPEC-EXEC); no fast-math (Numerics rule 2).
- The engine is built with Clang/AppleClang, MSVC or clang-cl, and GCC (Software guidelines). The engine is MIT-licensed, so the SIMD library must have a permissive licence.
- PLAN-STACK names two candidates: Google Highway or xsimd.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **Google Highway** (chosen) | Runtime dispatch is built in: one source file is compiled once per target (`foreach_target.h`, `HWY_EXPORT`, `HWY_DYNAMIC_DISPATCH`). Covers SSE4, AVX2, AVX-512 (`HWY_AVX3`) and NEON, plus SVE and an emulated 128-bit fallback. Length-agnostic API (`ScalableTag`) fits structure-of-arrays loops. Targets can be disabled at run time, which lets one test binary run the same kernel on every target the CPU supports. Licence: Apache-2.0 or BSD-3-Clause, at the user's choice. | Template-heavy API and longer compile times (each kernel file is compiled once per target). The `HWY_SSE4` and `HWY_AVX2` targets require more than the bare ISA name (for example AVX2 also needs FMA, BMI2 and F16C), so a CPU with AVX2 but no FMA falls back to SSE4. |
| xsimd | Header-only; BSD-3-Clause; simple batch types. | Runtime dispatch requires instantiating each kernel for an explicit architecture list and writing the dispatch glue ourselves; fewer ready-made operations for the masked and reduction patterns A8 needs. |
| `std::experimental::simd` / C++26 `std::simd` | Standard library, no dependency. | No runtime dispatch; not available in MSVC's standard library for C++20. |
| Hand-written intrinsics per ISA | Full control of every instruction. | Four or more copies of each kernel to write, test and keep in parity; no NEON/x86 sharing. |
| Compiler auto-vectorisation (`target_clones`) | No library. | Not available in MSVC. Without fast-math, compilers do not vectorise floating-point reductions, so dot products (A8-R2) would stay scalar; vector width and order are not under our control. |

## Consequences

- **A8** implements every kernel in Highway with runtime dispatch, a scalar reference outside Highway, and the parity test A8-T1 run on every target the CI machine supports (Highway's target-disabling API selects each target in turn). A8-T3 (AVX2 at least 4× scalar on 1,024 modes) measures Highway's code generation directly.
- **Dispatch is resolved off the audio thread.** The first `HWY_DYNAMIC_DISPATCH` call detects the CPU. `Engine::prepare` (or module initialisation) MUST trigger that detection, so no detection runs inside `Engine::process` (Real-time rules).
- **Numerics.** `MulAdd` is a fused multiply-add on targets with FMA (AVX2, AVX-512, NEON on arm64) and a separate multiply and add on SSE4 and the scalar fallback. Results can therefore differ in the last bits between targets. This is allowed: on one machine the chosen target is fixed, so output stays bit-identical for any thread count (Numerics rule 3), and comparisons between targets use the 10⁻⁶ relative parity tolerance (rule 4). Kernels that reduce MUST still sum in the SPEC-EXEC order; A8 documents the lane-to-order mapping in docs/dev/simd.md.
- **Compilers.** Highway builds with Clang, GCC and MSVC. If A8-T3 shows MSVC code generation misses the 4× target, Windows builds compile `engine/src/simd/` with clang-cl, which the Software guidelines already allow; that choice is recorded in docs/dev/simd.md.
- **Build.** P0.1 fetches Highway through CPM.cmake at an exact commit (ADR-0003) with its tests, examples and contrib disabled, and links it privately into `pme_simd` only. Compile time grows with the number of targets; A8 keeps Highway code to the kernel files.
- **Licence.** Distributions of the plug-in carry Highway's licence text; H1 lists it in the software bill of materials. If the Apache-2.0 option is used, its NOTICE requirement applies; the BSD-3-Clause option avoids it. H1 records which option the project uses.

## Verification

- P0.1-T1: every preset configures and builds with Highway fetched and linked into `pme_simd`.
- A8-T1 (parity on every instruction set), A8-T2 (bit-identical repeated runs), A8-T3 (throughput baseline).
- Code review: no Highway include outside `engine/src/simd/`.

## Revisiting

Reopen this decision if A8 cannot meet A8-T1 or A8-T3 with Highway on one of the three systems, or if Highway stops supporting one of the required compilers or targets. The replacement (xsimd or hand-written intrinsics) would be a new ADR superseding this one, and A8 would be reopened.
