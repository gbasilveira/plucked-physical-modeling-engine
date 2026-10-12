# Brief: A8 SIMD kernels — implementer

Status: **ready** (A1, A2, A4 complete). Start from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A8-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

vectorised versions of the hot loops with runtime dispatch and a scalar reference for each.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Real-time rules, especially 5: flush-to-zero; Numerics rules 2 to 5; Testing policy, Performance row; "Quality settings and numerical options (decision D-13)"); the phase card **A8 SIMD kernels**; for context **A1**, **A3**, **A4**, **A9** (the implicit bridge scheme your kernels must also serve), **C1**, **C2** (the consumers) and **V2** (performance validation).
- SPEC and PLAN sections:
  - PLAN-COMP — docs/plan/implementation-plan.md, "Computation strategy" (budget, "Parallel structure", "SIMD")
  - SPEC-EXEC — "Execution order" (per-sample loop; "Threading contract": fixed reduction order, global body modes in fixed chunks of 64 summed in chunk order; denormals)
  - SPEC-OSC — "Difference form" (the mode update you vectorise)
  - SPEC-BRG — "Bridge force", "Discrete coupling order", **"Bridge scheme"** (explicit and implicit)
  - SPEC-NL — "Explicit low-rank solve" (the rank-one Woodbury operations)
  - SPEC-BODY — the global body's projection and velocity products (for A8-R4's shapes)
- docs/adr/ADR-0001-highway.md (binding: Highway, runtime dispatch, dispatch resolved off the audio thread, no Highway include outside engine/src/simd, MSVC fallback to clang-cl recorded in docs/dev/simd.md).
- The scalar loops you mirror (read only):
  - `engine/src/osc/include/pme/osc/mode_bank.hpp` (`stepModes<S, C>`, `ModeBank::step`, `AlignedArray`: 64-byte aligned, padded to a multiple of 16);
  - `engine/src/bridge/include/pme/bridge/terms.hpp` (`bridgeForce`);
  - `engine/src/nonlinear/include/pme/nonlinear/sav.hpp` and docs/dev/nonlinear.md ("Low-rank solve", "Precision");
  - docs/dev/bridge.md.
- docs/dev/testing.md ("Benchmarks", baselines), docs/dev/rtcheck.md, docs/dev/osc.md (A1's carried denormal item).
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A1** (`pme_osc`, `stepModes`, `ModeBank`, `AlignedArray`), **A2** (`pme_string`), **A4** (complete 2026-10-12: `bridgeForce`, `BridgePoint`, `ModalSegment`, benchmark `pme_bridge_bench`); A3 (`pme_nonlinear`) for the rank-one shapes.
- Highway 1.4.0 at the pinned commit is fetched by CPM and linked privately into `pme_simd` (P0.1). `engine/src/simd/simd_placeholder.cpp` is the current content.
- Frozen interfaces: **interfaces-v2 = 300b995**. A8 declares no public API: everything is internal under `engine/src/simd/include/pme/simd/`.
- Carried from A1: **denormal policy**. Real-time rule 5 requires flush-to-zero and denormals-are-zero at `process` entry and on every worker; A1 left it to A8 and C1. A8 provides the mechanism and C1 calls it.

## Concurrent work: A5, A7, A8 and A9 run at the same time

A5, A7 and A8 (each with its test-author where briefed) run in separate git worktrees, and A9 (implicit bridge step, D-13), each with its test-author where briefed. The orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. B2 and D5 follow-ups (in review) and an interface agent for ICR-0002 (once approved) may also be committing. Edits must not overlap. **This table is identical in every A5, A7, A8 and A9 brief.**

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/exciters/**` (replaces `exciters_placeholder.cpp`; internal headers in `engine/src/exciters/include/pme/exciters/`), `engine/tests/exciters/**`, `docs/dev/hammer.md` | A5 implementer | read only |
| `engine/tests/oracles/exciters/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-09 and ORC-19, and an appended note in the test-location cell of ORC-08 | A5 test-author | read only |
| New files only: `engine/src/string/longitudinal*` and `engine/src/string/include/pme/string/longitudinal*`, `engine/src/nonlinear/tension*` and `engine/src/nonlinear/include/pme/nonlinear/tension*`; `engine/tests/geometric/**`; `docs/dev/geometric.md` | A7 implementer | read only |
| `engine/tests/oracles/geometric/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-11 and ORC-12 | A7 test-author | read only |
| `engine/src/simd/**` (replaces `simd_placeholder.cpp`; internal headers in `engine/src/simd/include/pme/simd/`), `engine/tests/simd/**`, `docs/dev/simd.md`, one new file `tools/bench/baselines/<machine-id>-a8.json` | A8 implementer | read only. **No Highway include outside `engine/src/simd/`** (ADR-0001). A8 adds kernels only and wires nothing into osc, string, bridge or nonlinear (C1 composes them). Its kernels are generic primitives (dot products, mode update, rank-one update) that serve **both** bridge schemes; A8 MUST NOT encode the explicit coupling order (for example a fused "bridge force, then advance with δ\_tt w" kernel) in a way that the implicit scheme cannot reuse. |
| `engine/src/bridge/**`, `engine/tests/bridge/**`, `engine/tests/oracles/bridge/**`, `docs/dev/bridge.md` | A9 implementer (and A9 test-author for `engine/tests/oracles/bridge/test_a9_*` and new files there; the existing A4 oracle files stay as they are) | read only: call the A4 internal headers, never edit them. **Only A9 edits engine/src/bridge while these phases run.** A5 and A7 use `BridgePoint`, `ModalSegment`, `bridgeForce` and the hooks as they are. |
| Existing files of `engine/src/string/**` (`string_model.*`), `engine/src/nonlinear/**` (`sav.*`), `engine/src/osc/**`, `engine/src/dampers/**`, `reference/**`, `tools/reference-cli/**`, `engine/tests/{osc,string,nonlinear,dampers,oracles/osc,oracles/string,oracles/nonlinear,oracles/dampers}/**` and their module notes | A1, A2, A3, A6, P0.5 (complete; follow-ups only when the orchestrator routes them) | read only; call their internal headers |
| `engine/include/pme/**`, `engine/tests/interfaces/**`, `tools/params/**`, `params.json`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime` except the B1 loader, `barriers`, `sched`) | nobody (frozen at interfaces-v2 = 300b995; ICR-0002 is proposed and will be applied by an interface agent) | read only |
| `compiler/**`, `plugin/**`, `engine/src/runtime/loader/**` | B2, D5 follow-ups, B1 (complete) | read only |
| `.github/workflows/**`, `tools/ci/**`, `tools/trace/**`, `tools/tests/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in the order A5, A7, A8, A9.** Later merges rebase onto earlier ones. Keep every block self-contained, so the order does not change behaviour.

| Shared file | A5 | A7 | A8 | A9 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A5: hammer and felt ----` … `# ---- end A5 ----` appended after the last line | one block `# ---- A7: geometric nonlinearity ----` … `# ---- end A7 ----` appended | one block `# ---- A8: SIMD kernels ----` … `# ---- end A8 ----` appended | none (tests go into engine/tests/bridge/, already built by A4's block) |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends `add_subdirectory(exciters)` | test-author appends `add_subdirectory(geometric)` | none | none (the A9 test-author appends its `test_a9_*` registrations to `engine/tests/oracles/bridge/CMakeLists.txt`) |
| `docs/dev/interfaces.md`, the sentence under "Stubs (P0.4-R3)" | move `exciters` out of the placeholder list, add "`exciters`: hammer by A5 (plucked exciters P1)" | none | move `simd` out of the placeholder list, add "`simd`: implemented by A8" | none |
| `engine/tests/oracles.md` | rows ORC-08 (location note), ORC-09, ORC-19 only | rows ORC-11, ORC-12 only | none | ORC-06: an appended note in the test-location cell (A9 test-author) |

**Rules for every agent in these phases.**

- **Test names** start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;`. Check with `uv run pytest tools/tests/test_p0_3_r1.py`. **Every requirement ID and every test ID of your card needs at least one test whose name starts with that ID** (the traceability job, docs/dev/traceability.md, fails a complete phase that lacks one).
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.
- **Disk space is limited.** Build one preset at a time. Before configuring another preset, delete the previous build directory (`rm -rf build/<preset>`). Never keep more than two build trees. Delete scratch programs and their builds from your scratchpad when you are done. The reference-comparison tests need an optimised preset (`linux-release` or `linux-rt-safety`), so build it last, by itself.
- **Engine settings (D-13).** When SPEC offers alternatives, implement each behind an option (SPEC-CONV "Quality and numerical options"). Until ICR-0002 is applied, options are internal enums whose names and values match ICR-0002. Never pick one alternative silently.
- If something cannot be done as written (a tolerance not reached, an instability, SPEC unclear), stop that item. Do not loosen a tolerance or change the scheme. Describe it in the report with measured values; the planner files a spec issue.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A8: SIMD kernels ----
target_include_directories(pme_simd PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/simd/include")
add_subdirectory(engine/tests/simd)
# ---- end A8 ----
```

`pme_simd` already links `hwy` privately (root CMakeLists.txt, ADR-0001). Your public internal headers must not include Highway headers. Expose plain function declarations, and keep `HWY_EXPORT` and `HWY_DYNAMIC_DISPATCH` in the `.cpp` files.

## Scope

- May edit (create or change): the paths the ownership table gives the **A8 implementer**, plus your root `CMakeLists.txt` block and your `docs/dev/interfaces.md` words.
- Read only: everything else. In particular you do **not** wire the kernels into engine/src/osc, string, bridge or nonlinear. C1 composes them, and those modules keep their scalar loops as the references. Your kernels match their signatures and arithmetic so that the swap is a one-line change later.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A8-R1 Vectorised mode-bank update with explicit force vector.
2. A8-R2 Dot products (bridge force, contact displacement) with a fixed reduction order.
3. A8-R3 Rank-one Woodbury vector operations.
4. A8-R4 Small matrix–matrix kernel with fixed reduction order for C2's projection and velocity products.
5. A8-R5 Runtime dispatch across SSE4.2, AVX2, AVX-512 and NEON; scalar fallback; 64-byte alignment.

### How to meet each requirement

Namespace `pme::simd`. Each kernel has:

- a **scalar reference** in plain C++, outside Highway, element at a time. Compile the reference so the compiler cannot auto-vectorise it (`#pragma clang loop vectorize(disable)` / GCC `#pragma GCC optimize("no-tree-vectorize")` on the function, or `#pragma loop(no_vector)` on MSVC; state the mechanism in docs/dev/simd.md). A8-T3 compares against true scalar code.
- a Highway implementation compiled for every target via `foreach_target.h`.

Kernel rules:

- Kernels are `noexcept`, allocation-free and take raw pointers plus a count.
- Inputs are 64-byte aligned. Arbitrary n is handled with a masked or scalar tail that never reads or writes past n, plus a fast path when n is a multiple of 16 (A1's padding).
- No fast-math. FMA (`MulAdd`) is allowed: it is fused on AVX2, AVX-512 and arm64 and split on SSE4 and the scalar fallback (ADR-0001). Results may differ between targets in the last bits; parity uses 10⁻⁶ relative.

**Canonical reduction order (A8-R2, A8-R4; Numerics rule 5, SPEC-EXEC).** Every reduction uses one order that does not depend on the target's vector width:

- elements are assigned to **16 virtual lanes** by index mod 16;
- each virtual lane accumulates its elements in index order;
- the 16 lane sums are combined by a fixed pairwise tree: (0+8, 1+9, …), then (0+4, …), then (0+2, …), then (0+1);
- the tail elements go to their lanes like any other.

AVX-512 holds the 16 lanes in one float vector, AVX2 in two, SSE4 and NEON in four, the scalar fallback in 16 scalars. With this order, results are bit-identical across runs and thread counts on one machine (A8-T2) and differ between targets only through FMA contraction. Document the mapping in docs/dev/simd.md (ADR-0001 requires it). For float64 kernels use 8 virtual lanes the same way.

**A8-R1.** `stepModes(float* q, float* u, const float* beta, const float* kappa, const float* gamma, const float* force, std::size_t n)`, the SPEC-OSC difference form exactly as A1's scalar `stepModes<float, float>`:

- uⁿ⁺¹ = β uⁿ − κ qⁿ + γ fⁿ, qⁿ⁺¹ = qⁿ + uⁿ⁺¹;
- the same operation order as A1, so the non-FMA targets are bit-identical to A1's loop; test it;
- `force` may be null (no force), as A1 allows;
- also a variant that writes qⁿ (the previous q) to an output span, as `ModeBank::step` does;
- also a float64 instantiation, for A3's double tests.

**A8-R2.** `dot(const float* a, const float* b, std::size_t n) → float` (canonical order, float32 accumulation as A4's `bridgeForce<float>`), `dot` in float64, and a mixed `dot(const float* a, const float* b) → double` with float64 accumulation (A3 forms reductions in float64 from float32 states).

Uses:

- the bridge force F\_b = Σ a\_n q\_n − (T/L) w (the scalar term stays outside);
- the hammer contact displacement Σ φ̄\_n q\_n;
- the implicit scheme's aᵀb (SPEC-BRG "Bridge scheme").

**A8-R3.** Rank-one Woodbury vector operations (SPEC-NL "Explicit low-rank solve", r = 1: z = a − h (wᵀa)/(1 + wᵀh)):

- `dot2(const T* w, const T* a, const T* h, std::size_t n, T& wa, T& wh)`: both dot products in one pass, canonical order;
- `axpyNeg(T* z, const T* a, const T* h, T c, std::size_t n)`: z = a − c h;
- a fused `rankOneUpdate(z, a, h, w, n)` = the two above.

All three come in float32 and float64. The same `axpyNeg` serves the implicit bridge scheme's qⁿ⁺¹ = **b** − **h̃** wⁿ⁺¹ (SPEC-BRG "Bridge scheme"; SPEC-EXEC 5′).

**A8-R4.** `matmul(const float* A, std::size_t lda, const float* B, std::size_t ldb, float* C, std::size_t ldc, std::size_t M, std::size_t N, std::size_t K)`, C = A·B, row-major. The reduction over K uses SPEC-EXEC's rule:

- fixed chunks of 64 along K, summed in chunk order;
- each chunk reduced in the canonical 16-lane order.

Typical shapes:

- the projection of P bridge-point forces over S samples onto K body modes: (K×P)·(P×S), with P up to about 100 and S = 16 or 32;
- the body velocity at bridge points: (P×K)·(K×S), with K up to a few thousand.

Accumulation in float32 (body states are float32). If your error analysis shows float32 accumulation over K = 4096 loses more than 10⁻⁵ relative, offer a float64-accumulating variant and document both (a D-13-style option for C2, not a silent choice).

**A8-R5.** Dispatch and platform:

- Highway dynamic dispatch over the targets ADR-0001 lists: SSE4 (SSE4.2), AVX2, AVX3 (AVX-512) on x86-64, NEON on arm64, plus Highway's emulated or scalar fallback.
- `pme::simd::initialise()` (non-audio) resolves the dispatch (the first `HWY_DYNAMIC_DISPATCH` call detects the CPU) and returns the chosen target's name. C1 and `Engine::prepare` call it, so no detection runs inside `process` (ADR-0001, Real-time rules).
- A test hook selects each target the CPU supports in turn (`hwy::SetSupportedTargetsForTest`), for A8-T1.
- **Denormals (carried from A1).** A RAII guard `ScopedFlushDenormals` (noexcept, no system call) sets FTZ and DAZ on x86-64 (MXCSR) and FZ on arm64 (FPCR), and restores the previous state on destruction. C1 uses it at `process` entry and on every worker (Real-time rule 5).

**Bridge schemes (D-13; SI-0021, SI-0018).** Your kernels serve both bridge schemes, and you MUST NOT encode the explicit coupling order in them. Do not write a kernel that fuses "bridge force from qⁿ, then advance with −g δ\_tt w" into one pass. Provide the primitives and compose both schemes in a benchmark:

- explicit: `dot` → bridge advance (scalar) → `stepModes` with force −g δ\_tt w;
- implicit: `stepModes` with the non-bridge force (giving **b**) → `dot` (aᵀb) → solve (scalar) → `axpyNeg`.

**Benchmark `bridge_scheme_bench`** (label `perf`): A4's benchmark configuration (3 segments × about 59 modes × 2 polarisations on one bridge point, 64 samples per call), composed from your kernels and from the scalar references, explicit and implicit. Report the four medians. **This is the cost figure SI-0021 asked for before G1**; A9 measures its own scalar implementation separately.

## Tests that must pass

- **A8-T1** parity with the scalar reference within 10⁻⁶ relative for every instruction set;
- **A8-T2** repeated runs are bit-identical;
- **A8-T3** the AVX2 mode-bank update is at least 4× the scalar throughput on a 1,024-mode bank, with the measured baseline stored.

### How each test is built

All doctest in `engine/tests/simd/`.

**A8-T1** (label `unit`):

- Inputs: random inputs (fixed seed) at n = 1, 15, 16, 17, 64, 1023, 1024, 4099, plus matmul shapes (K×P)·(P×S) and (P×K)·(K×S) with P = 8, 100, K = 64, 1000, 4096, S = 16, 32. Use realistic magnitudes: mode-bank coefficients from `compileOscillatorF64` for modes spread from 20 Hz to 20 kHz at 48 kHz; dots with cancellation-free and with cancelling sums.
- For **every target the CPU supports and Highway compiled**, compare each kernel with its scalar reference.
- Pass: relative error ≤ 10⁻⁶, measured against max(|reference|, Σ|terms|·ε-scale) for reductions; state the norm in the test.
- Non-FMA targets must be **bit-identical** to the scalar reference for `stepModes` (same operation order) and for the dots in canonical order.
- Each test case logs which targets ran. The report lists them per CI system. If a target cannot run on any CI machine (AVX-512 may be absent), say so; G1's "SIMD parity on every instruction set" is judged on what ran.

**A8-T2** (label `unit`): run each kernel 100 times on the same input, and on copies at different (still 64-byte aligned) addresses. Results must be bit-identical. Repeat with every supported target.

**A8-T3** (label `unit`, so it runs in pr.yml, which excludes `perf`; keep it under 2 s):

- Measure the median time of `stepModes` on a 1,024-mode bank (11 repetitions of a 10 ms loop) for the AVX2 target and for the non-vectorised scalar reference.
- Pass: ratio ≥ 4.
- On a CPU without AVX2, or on arm64, the test passes with a logged message "AVX2 not available" and also logs the best target's ratio. The report says on which CI systems the 4× was actually measured.
- If MSVC's code generation misses 4× on Windows, first follow ADR-0001 (compile engine/src/simd with clang-cl on Windows; record in docs/dev/simd.md). If it still misses, report the measured ratio; do not lower the bar.
- **Stored baseline:** run the `perf` benchmark `simd_bench` (A8-R1 to R4 kernels, each target and scalar) on your machine with `tools/bench`'s `--out`. Commit the JSON as `tools/bench/baselines/<machine-id>-a8.json` (docs/dev/testing.md "Baselines"; the machine-id is the CPU model slug). Put the CI runner's numbers in the report.

### Other tests (unit label unless stated)

- `A8-R1 …`: `stepModes` equals A1's `ModeBank::step` bit for bit on non-FMA targets, with and without force, and with the previous-q output.
- `A8-R2 …`: the canonical order is target-independent: on two different targets, the dot results differ by at most the FMA effect (bit-identical on non-FMA targets).
- `A8-R3 …`: the fused rank-one update equals A3's `lowRankSolve` with r = 1 on random systems (10⁻¹² in float64).
- `A8-R4 …`: matmul against a float64 reference (relative 10⁻⁵ in float32); the chunk order is respected (an instrumented reference reproduces it bit for bit).
- `A8-R5 …`:
  - `initialise` returns a non-empty target name;
  - the dispatch does not allocate when called afterwards under `ScopedRealtimeThread` (label `rt`);
  - `ScopedFlushDenormals`: a denormal input to a multiply yields 0 inside the guard and the previous mode is restored afterwards.
- Alignment: debug builds assert 64-byte alignment; misaligned input is a documented precondition, not a silent slow path.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A8:

- docs/dev/simd.md: PLAN-COMP and SPEC-EXEC parts implemented; the kernel list with signatures and their scalar counterparts in osc, bridge and nonlinear; the canonical 16-lane order and the 64-chunk rule; the FMA note; dispatch and `initialise`; the denormal guard; how both bridge schemes map to the kernels; the anti-vectorisation mechanism of the scalar references; the A8-T3 and benchmark numbers; the MSVC or clang-cl decision.
- docs/dev/interfaces.md: move `simd` out of the placeholder list.
- Coverage: the scalar references and the dispatch glue count in the 90% gate. Highway's per-target clones that cannot run on the coverage machine are excluded only if tools/ci/coverage\_gate.py already supports exclusions; otherwise report the effect.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A8: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Highway compiles each kernel file once per target, so builds are slower and larger. Keep Highway code to the kernel files, and build one preset at a time.
- Run before you finish:
  - clang-format and clang-tidy on your files;
  - `ctest --preset linux-debug` (GCC and Clang, one tree at a time);
  - `linux-release` for A8-T3 and the benchmarks;
  - the rt-safety preset;
  - `uv run pytest tools/tests/test_p0_3_r1.py`.

### Reporting rule

Report every test `not run`, `status` `partial`. Put in `notes`:

- the targets exercised and the worst parity error per kernel and target;
- the A8-T3 ratio (compiler and CPU);
- the four `bridge_scheme_bench` medians;
- the baseline file path;
- any MSVC finding.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A8`, `role` `implementer`, `model` `sonnet`;
- `requirements` A8-R1 to A8-R5;
- `tests` A8-T1 to A8-T3;
- every created or changed path in `outputs`;
- `pull_requests` empty;
- `adrs` ADR-0001 as applied;
- `open_risks` as found (for example AVX-512 not available on CI).
