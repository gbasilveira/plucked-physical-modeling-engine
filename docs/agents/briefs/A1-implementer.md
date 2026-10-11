# Brief: A1 Modal oscillator — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A1-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the discrete oscillator every mode in the engine uses, with float64 coefficient compilation and a structure-of-arrays mode bank.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Real-time rules, Numerics rules, Testing policy); the phase card **A1 Modal oscillator**; for context the cards **A2**, **A3** (your first consumers) and **A8** (will vectorise your scalar kernel).
- SPEC and PLAN sections:
  - SPEC-OSC — docs/plan/engine-specification.md, "The modal oscillator" (all of it: continuous mode, discrete scheme, difference form, derived quantities, discrete energy, bandwidth, time-varying parameters, contract)
  - SPEC-CONV — docs/plan/engine-specification.md, "Scope, conventions and notation" (units, time, discrete operators, precision)
- docs/spec-issues/SI-0008-oscillator-coefficient-cancellation.md — **read before writing `compileOscillator`** (see A1-R1 below).
- docs/adr/ADR-0006-interface-status-returns.md — `Status` returns and out-parameters.
- engine/include/pme/osc.hpp — the frozen declaration you implement.
- docs/dev/testing.md and docs/dev/signal-analysis.md — test conventions, `pme_add_doctest`, `pme_add_benchmark`, tolerance macros, the analysis library.
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs: **G0** passed (docs/gates/G0.md, evaluated at 15a1a43). It rests on P0.1 (build skeleton, `pme_osc` target from `cmake/pme_module.cmake`), P0.2 (CI, `pme_rtcheck`), P0.3 (test framework `pme_test_framework`, `pme_add_doctest`, `pme_add_benchmark`, analysis library `pme_analysis`), P0.4 (headers). All on branch `ccr-5ef35d73-oqb6fd`; no pull requests.
- Frozen interfaces: **interfaces-v2** (ICR-0001; `interfacesVersionMajor == 2` in engine/include/pme/version.hpp). ICR-0001 does not change osc.hpp, so `compileOscillator` is as in interfaces-v1 (2daee85). Your worktree must start from a commit that contains the ICR-0001 application; if `interfacesVersionMajor` is 1, stop and report.
- Current state of your module: `engine/src/osc/osc_stub.cpp` returns `NotImplemented`; `engine/tests/interfaces/test_stubs.cpp` checks that.

## Concurrent work: P0.5, V1, A1 and D1 run at the same time

Four phases are implemented in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. Edits must not overlap. This table is identical in every brief of the four phases.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/osc/**`, `engine/tests/osc/**`, `docs/dev/osc.md` | A1 implementer | read only |
| In `engine/tests/interfaces/test_stubs.cpp`: the `SUBCASE("compileOscillator")` block only. In `docs/dev/interfaces.md`: the word `osc` in the first sentence of "Stubs (P0.4-R3)" only | A1 implementer | read only |
| `engine/tests/oracles/**` (new); in `engine/tests/oracles.md`: the status and test-location cells of rows ORC-01 and ORC-02 only | A1 test-author | read only |
| `reference/**`, `tools/reference-cli/**`, `tools/tests/test_p0_5_*.py`, `docs/dev/reference-solver.md` | P0.5 | read only |
| `pyproject.toml`, `uv.lock` | P0.5 | read only |
| `tools/trace/**`, `tools/tests/test_v1_*.py`, `.github/workflows/nightly.yml`, `tools/ci/**` except `required_checks.json` | V1 | read only |
| P0.3 follow-ups: `cmake/pme_testing.cmake`, `engine/tests/framework/**`, `engine/tests/rtcheck/CMakeLists.txt`, `tools/analysis/**`, `tools/bench/**`, `tools/tests/test_p0_3_*.py`, `docs/dev/testing.md`, `docs/dev/signal-analysis.md` | V1 | read only |
| `# Covers:` / `// Covers:` comment lines added to test files of completed phases P0.1 to P0.3 (comments only) | V1 | read only |
| `plugin/**` (`plugin/CMakeLists.txt`, `plugin/clap/`, `plugin/common/`), `docs/dev/clap-shell.md` | D1 | read only |
| `cmake/dependencies.cmake`, `docs/dev/build.md` | D1 | read only |
| `.github/workflows/pr.yml` | V1 owns every existing job; V1, D1 and P0.5 each **append one new job in a delimited block at the end of the file** (`traceability`, `plugin`, `reference-python`) | nobody else edits existing jobs |
| `tools/ci/required_checks.json` | each phase that adds a pr.yml job appends its own matrix-expanded job names at the end of the array | — |
| `CONTRIBUTING.md` | each phase that adds a pr.yml job appends its own rows at the end of the required-checks table | — |
| `docs/dev/ci.md` | V1; D1 and P0.5 each append one delimited subsection at the end of the file describing their job | — |
| Root `CMakeLists.txt` | nobody above the current last line; A1 implementer, A1 test-author, P0.5 and D1 each **append one delimited block at the end** | — |
| `CMakePresets.json`, `cmake/pme_module.cmake`, `cmake/compiler_options.cmake`, `cmake/sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody | read only |
| `engine/include/pme/**`, the other `engine/src/*/` stubs, `engine/tests/interfaces/**` (except the A1 row above), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2; changes only by ICR) | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

When two phases append at the same place (end of the root `CMakeLists.txt`, end of pr.yml, end of a JSON array or table), the orchestrator keeps both blocks when merging. Keep your blocks self-contained so that order does not matter.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A1: modal oscillator ----
target_include_directories(pme_osc PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/osc/include")
add_subdirectory(engine/tests/osc)
# ---- end A1 ----
```

(Drop the first line if you keep every internal declaration in a header next to the sources and the tests reach it another way; do not edit `cmake/pme_module.cmake`.)

## Scope

- May edit (create or change): the paths the ownership table gives the **A1 implementer**, plus your one block in the root `CMakeLists.txt`.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A1-R1 `compileOscillator` exactly as SPEC-OSC: σ̂Δt = tanh(σΔt) and the cancellation-free ω̂²Δt² formula, in float64.
2. A1-R2 Bandwidth rule: reject modes with ω\_dΔt > 0.95π, above f\_max, or overdamped.
3. A1-R3 `ModeBank`: 64-byte-aligned arrays q, u, β, κ, γ (float32) and ω̂², σ̂ (float64), with a scalar difference-form update taking an explicit force vector.
4. A1-R4 Derived quantities: half-step velocity, centred velocity, centred acceleration.
5. A1-R5 Discrete energy and energy balance in float64.
6. A1-R6 Coefficient ramp utility implementing the 0.1%-per-sub-block rule.

### How to meet each requirement

**A1-R1.** Implement `pme::compileOscillator(double omega, double sigma, double fs, OscCoeffs& out)` (engine/include/pme/osc.hpp, ADR-0006) in `engine/src/osc/` and delete `osc_stub.cpp`.

- Δt = 1/fs; x = σΔt; ω\_dΔt = Δt·√((ω − σ)(ω + σ)) (the product form avoids cancellation near σ ≈ ω); σ̂Δt = tanh(x).
- **ω̂²Δt²: use the form proposed in SI-0008**, 4 [sinh²(x/2) + sin²(ω\_dΔt/2)] / cosh(x). It is algebraically identical to SPEC-OSC's printed formula; the printed formula loses up to three digits when σΔt is small and cannot meet A1-T1 (SI-0008 has the numbers). SI-0008 is open with "accept" recommended; A1 is not merged until it is resolved. Cite both in the code comment: `// SPEC-OSC discrete scheme; evaluation per SI-0008`.
- β = (1 − σ̂Δt)/(1 + σ̂Δt), κ = ω̂²Δt²/(1 + σ̂Δt), γ = Δt²/(1 + σ̂Δt) in float64, then stored as float32 (round to nearest). ω̂² = ω̂²Δt²·fs² and σ̂ = σ̂Δt·fs stay float64.
- Also expose the float64 values of β, κ, γ before rounding through an internal function (for example `pme::osc::compileOscillatorF64`, declared in an internal header under `engine/src/osc/include/pme/osc/`), so tests can check them at 10⁻¹⁴.
- Status: `InvalidArgument` if fs ≤ 0, or ω, σ or fs is not finite, or σ < 0, or ω < 0; `Rejected` if the bandwidth rule rejects the mode (σ ≥ ω, or ω\_dΔt > 0.95π); `Ok` otherwise. On any status other than `Ok`, `out` is unchanged (ADR-0006). No exceptions, no allocation.
- Keep a unit test `A1-R1 SPEC printed formula agrees with the SI-0008 form` that evaluates SPEC-OSC's printed expression in float64 and checks it agrees with yours within 5 × 10⁻¹² relative over the A1-T1 grid. It documents the difference whichever way SI-0008 is resolved.

**A1-R2.** `compileOscillator` takes no f\_max (its frozen signature), so the f\_max part of the rule is a separate internal function, for example `pme::osc::passesBandwidthRule(double omega, double sigma, double fs, double fMax)`, returning which rule rejected the mode (none, overdamped, above Nyquist margin, above f\_max). Interpret "above f\_max" as ω\_d/2π > f\_max (SPEC-OSC "its frequency"); state this in docs/dev/osc.md. `compileOscillator` must give the same verdict as this function with f\_max = +∞. A2 (`compileString`) and later compilers call it with the instrument's f\_max.

**A1-R3.** `ModeBank` (internal header, namespace `pme::osc`): structure-of-arrays storage for N modes. q, u, β, κ, γ as float32 and ω̂², σ̂ as float64, each array 64-byte aligned, padded to a multiple of 16 floats so A8's SIMD kernels can process whole vectors (padding lanes have β = κ = γ = 0 and stay 0). Allocation only in a non-real-time `resize`/`assign` function; the update is `noexcept` and allocation-free. The scalar update for one sample, for every mode i:

uⁿ⁺¹ = β uⁿ − κ qⁿ + γ fⁿ, qⁿ⁺¹ = qⁿ + uⁿ⁺¹ (SPEC-OSC "Difference form"),

with f an explicit span of N per-mode forces (per unit modal mass). Store only q and u, never qⁿ⁻¹. Keep the arithmetic in exactly this order (no algebraic rearrangement; Numerics rule 2). Write the kernel once as a template on the state type, so that tests can also run it in float64; the engine instantiates float32. This scalar kernel is the reference implementation A8 vectorises (Numerics rule 4).

**A1-R4.** Functions for the half-step velocity v^(n+½) = uⁿ⁺¹/Δt, the centred velocity (uⁿ⁺¹ + uⁿ)/(2Δt) and the centred acceleration (uⁿ⁺¹ − uⁿ)/Δt² (SPEC-OSC "Derived quantities"), evaluated in float64 from float32 states. The update must be able to return uⁿ (before the step) so callers can form the centred quantities.

**A1-R5.** Discrete energy H^(n+½) = ½(uⁿ⁺¹/Δt)² + ½ ω̂² qⁿ qⁿ⁺¹ per mode (unit modal mass; callers multiply by ρA or the body's modal mass), accumulated in float64, and the balance term Δt[−2σ̂ (δ\_t· qⁿ)² + fⁿ δ\_t· qⁿ] so that a test can check H^(n+½) − H^(n−½) against it (SPEC-OSC "Discrete energy"). Provide a bank-level sum in a fixed index order.

**A1-R6.** A real-time-safe utility (`noexcept`, no allocation; it runs on the audio thread when C4 applies bends and damper changes) that, given the coefficients in force and target coefficients for one or more modes, decides per mode whether the change exceeds 0.1% per sub-block and, if so, produces linear per-sample interpolation of β, κ, γ across the S samples of the sub-block (S = 16 or 32, SPEC-CONV), ending exactly on the target values; otherwise the target applies from the first sample of the sub-block. The state (q, u) is kept (SPEC-OSC "Time-varying parameters"). Define "larger than 0.1%" on the physical parameters: |Δω| > 10⁻³ ω, or |Δσ| > 10⁻³ σ (any change of σ from or to 0 ramps). Document the rule in docs/dev/osc.md.

## Tests that must pass

- **ORC-01** (Oscillator accuracy) and **ORC-02** (Oscillator energy): written by the A1 test-author (docs/agents/briefs/A1-test-author.md), not by you. Make them possible: everything they need must be reachable from `pme_osc` through the public header and your internal header (`ModeBank`, the templated kernel, energy, `compileOscillatorF64`). Do not write tests named `ORC-01` or `ORC-02`.
- **A1-T1** coefficients agree with a 50-digit reference within 10⁻¹⁴ relative for ωΔt from 10⁻⁴ to 0.95π and σΔt from 0 to 0.1;
- **A1-T2** bandwidth rejections at 44.1, 48 and 96 kHz;
- **A1-T3** a 1% frequency ramp over one sub-block tracks the analytic chirp phase within 0.1%.

### How each test is built

**A1-T1.** The 50-digit reference is a committed JSON table generated by a script you commit, `engine/tests/osc/make_a1_t1_reference.py`, run with `uv run --no-project --with mpmath==<exact version> python engine/tests/osc/make_a1_t1_reference.py` (no change to pyproject.toml; the script is not run in CI and names the mpmath version it used). The grid: at least 60 values of ωΔt log-spaced over [10⁻⁴, 0.95π] and at least 40 values of σΔt (0 plus log-spaced over [10⁻⁹, 0.1]); store the exact double inputs (ω, σ, fs with fs = 48 000) as hexadecimal floats or round-trip decimal strings, and for each accepted pair the reference σ̂Δt, ω̂²Δt², β, κ, γ to at least 20 significant digits; mark pairs the bandwidth rule rejects. The doctest (`P0.3-R1` naming rule: `A1-T1 …`, label `unit`) checks every accepted pair: `compileOscillatorF64` within 10⁻¹⁴ relative on all five quantities; `OscCoeffs::omegaHat2` and `sigmaHat` within 10⁻¹⁴; the float32 fields equal the correctly rounded float64 values exactly; and every rejected pair returns `Rejected` with `out` unchanged.

**A1-T2.** For fs = 44 100, 48 000 and 96 000 Hz: modes just inside and just outside ω\_dΔt = 0.95π (one ulp-scale step each side, using the product form for ω\_d) are accepted and rejected respectively; σ = ω and σ = 1.0001 ω are rejected as overdamped, σ = 0.9999 ω is accepted; with `passesBandwidthRule`, f\_max = 16 kHz rejects a mode at 16 000.1 Hz and accepts one at 15 999.9 Hz, and at 44.1 kHz f\_max above the Nyquist margin leaves the 0.95π rule as the binding one. Report the highest accepted frequency per sample rate in docs/dev/osc.md.

**A1-T3.** One mode at 1 kHz and one at 4 kHz, fs = 48 kHz, σ = 0.5 s⁻¹, S = 16 and S = 32. Run free oscillation until steady, then ramp the frequency from f to 1.01 f over one sub-block with the A1-R6 utility, then hold 1.01 f. Compare the oscillator's phase with the analytic phase of a linear chirp from f to 1.01 f over the same sub-block (φ(t) = 2π∫f(t)dt from the same initial phase). Measure the phase after the ramp with a least-squares fit of the free oscillation that follows, from the float64 values of the states (you may use `pme_analysis`). Pass: the phase error accumulated over the ramp is at most 0.1% of the phase advance during the ramp. Also check: without ramping (a step change at the sub-block start), the same metric is reported (it may pass or fail; it is information for docs/dev/osc.md).

### Other tests (unit label unless stated)

- `A1-R3 …`: alignment of every array (address % 64 == 0), padding lanes stay zero, the kernel in float32 and float64 agree to float32 precision over 10⁵ steps for a 27.5 Hz mode; the update allocates nothing — run it inside `pme::rtcheck::ScopedRealtimeThread` in a test registered with `RT` so it runs under the `rt-safety` preset.
- `A1-R4 …`, `A1-R5 …`: derived quantities and the energy-balance identity hold to 10⁻¹² relative in float64 for a damped, forced mode.
- `A1-R6 …`: changes at 0.09% switch at the boundary, changes at 0.11% ramp; the ramp ends exactly on the target.
- Benchmark: `engine/tests/osc/osc_bench.cpp` with `pme_add_benchmark` (label `perf`, nightly): the scalar update of 64, 512 and 4096 modes, work unit "mode-samples". There are no reference machines yet (decision D-4, phase V2), so do not commit a baseline file; put the numbers from your machine in the report.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A1:

- Module note: docs/dev/osc.md — SPEC-OSC and SPEC-CONV sections implemented; the SI-0008 evaluation; the A1-R2 f\_max interpretation; the A1-R6 ramp rule; the internal API (ModeBank, kernel, energy) for A2, A3 and A8; highest accepted frequency per sample rate.
- `engine/tests/interfaces/test_stubs.cpp`: replace only the `compileOscillator` SUBCASE with one that checks `Status::Rejected` for an overdamped mode leaves `out` unchanged (the rest of the P0.4-R3 test stays). In docs/dev/interfaces.md, drop `osc` from the list of stub modules in "Stubs (P0.4-R3)" and add "(`osc`: implemented by A1)".
- Coverage: `engine/src/osc` is part of the 90% line-coverage gate (coverage job); your unit tests must cover it.
- Performance baseline: the benchmark exists and runs nightly; the baseline file waits for V2 and D-4 (say so in `notes`).
- Reviewer approval and CI are collected by the orchestrator after merging.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A1: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` and `ctest --preset linux-rt-safety` with GCC and with Clang; `uv run pytest tools/tests/test_p0_3_r1.py` (test names).
- If something cannot be done as written (a tolerance not reached, SPEC unclear), stop that item, do not loosen a tolerance, and describe it in the report with the measured values; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, presets, worst A1-T1 error per quantity, the A1-T2 boundary values, the A1-T3 phase errors with and without ramp, and the benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A1`, `role` `implementer`, `model` `sonnet`; `requirements` A1-R1 to A1-R6; `tests` A1-T1 to A1-T3 (ORC-01 and ORC-02 are the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0008; `open_risks` as found.
