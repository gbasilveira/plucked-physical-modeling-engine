# Brief: A3 SAV framework and low-rank solve — implementer

Status: **ready once A1 is complete** (A1 merged at 1512985, in review). Do not start before the orchestrator marks A1 complete in docs/status/phase-status.md; if A1's review changes the osc internal headers, start from the commit that contains those changes.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A3-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the generic nonlinear update used by hammers, plucks, dampers, barriers and tension modulation.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Real-time rules, Numerics rules, Testing policy); the phase card **A3 SAV framework and low-rank solve**; for context the cards **A1** (your input), **A5** (hammer potential and dashpots), **A6** (rank-one damper), **A7** (tension modulation), **P1**, **P2** (plucks, barriers) and **A8** (will vectorise your scalar kernel).
- SPEC and PLAN sections:
  - SPEC-NL — docs/plan/engine-specification.md, "Nonlinear framework" (all of it: coordinates, SAV formulation, discrete scheme, explicit low-rank solve, skipping, drift control, contracts)
  - SPEC-OSC — "The modal oscillator" ("Difference form", "Discrete energy") — the oscillator each coordinate uses
  - SPEC-HAM "Oracle" (the rigid-contact contact time, for ORC-08 context) and SPEC-DMP "Rank-one" (the dashpot pair a damper will use)
  - SPEC-CONV — "Scope, conventions and notation"
- docs/dev/osc.md and the A1 internal headers `engine/src/osc/include/pme/osc/mode_bank.hpp`, `oscillator.hpp` (`ModeBank`, `stepModes`, energy, `compileOscillatorF64`).
- engine/include/pme/nonlinear.hpp — declares no API (`nonlinearApiDeclared = false`): the SAV types are internal to the engine. Its comment says A5 declares them; in fact you do (A5 adds the hammer potential). Do not edit the header; the comment fix is carried to the next ICR.
- docs/adr/ADR-0006-interface-status-returns.md; docs/dev/rtcheck.md (`ScopedRealtimeThread`); docs/dev/testing.md.
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A1** (modal oscillator; merged at 1512985 on `ccr-5ef35d73-oqb6fd`, `pme_osc` with internal headers). Through G0: P0.2 `pme_rtcheck`, P0.3 test framework and `pme_analysis` (`account_energy`, contact-time measurement).
- Frozen interfaces: **interfaces-v2 = 300b995**. No public header changes for A3. If `interfacesVersionMajor` is not 2, stop and report.
- Current state of your module: `engine/src/nonlinear/nonlinear_placeholder.cpp` (a placeholder function); delete it when your sources exist.

## Concurrent work: P0.5, D1, B1, A2 and A3 run at the same time

P0.5 and D1 are in progress; B1 starts now; A2 and A3 (each with a test-author) start when A1 is complete. Each runs in its own git worktree and the orchestrator merges commits into `ccr-5ef35d73-oqb6fd`. Edits must not overlap. This table is identical in the B1, A2 and A3 briefs; P0.5 and D1 keep the rows of their own briefs, which this table repeats.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `compiler/pme_compiler/schema/**`, `compiler/pme_compiler/examples/**`, `compiler/tests/test_b1_*.py`, `compiler/tests/data/b1/**`, `engine/src/runtime/loader/**`, `engine/tests/loader/**`, `instruments/examples/**`, `docs/dev/instrument-format.md` | B1 implementer | read only |
| `engine/src/string/**` (replaces `string_stub.cpp`), `engine/tests/string/**`, `docs/dev/string.md` | A2 implementer | read only |
| In `engine/tests/interfaces/test_stubs.cpp`: the `compileString` SUBCASE only. In `docs/dev/interfaces.md`: the word `string` in the stub-module list of "Stubs (P0.4-R3)" only | A2 implementer | read only |
| `engine/tests/oracles/string/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-03, ORC-04, ORC-05 only; one line `add_subdirectory(string)` appended to `engine/tests/oracles/CMakeLists.txt` | A2 test-author | read only |
| `engine/src/nonlinear/**` (replaces `nonlinear_placeholder.cpp`), `engine/tests/nonlinear/**`, `docs/dev/nonlinear.md`; in `docs/dev/interfaces.md` the word `nonlinear` in the placeholder list of "Stubs (P0.4-R3)" only | A3 implementer | read only |
| `engine/tests/oracles/nonlinear/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-08 only; one line `add_subdirectory(nonlinear)` appended to `engine/tests/oracles/CMakeLists.txt` | A3 test-author | read only |
| `engine/src/osc/**`, `engine/tests/osc/**`, `engine/tests/oracles/osc/**`, `docs/dev/osc.md` | A1 (in review: review fixes only) | read only; A2 and A3 use the internal headers under `engine/src/osc/include/pme/osc/` |
| `reference/**`, `tools/reference-cli/**`, `tools/tests/test_p0_5_*.py`, `docs/dev/reference-solver.md` | P0.5 | read only |
| `pyproject.toml`, `uv.lock` | P0.5 | B1 adds exactly two dependency lines and regenerates `uv.lock` with `uv lock` (see B1's "Shared files"); nobody else |
| `plugin/**`, `docs/dev/clap-shell.md`, `cmake/dependencies.cmake`, `docs/dev/build.md` | D1 | read only |
| `.github/workflows/pr.yml`, `tools/ci/required_checks.json`, `CONTRIBUTING.md`, `docs/dev/ci.md` | existing content: nobody; P0.5 and D1 append their delimited blocks as their briefs say | B1, A2, A3 and the test-authors add nothing (their tests run in the existing build and python jobs) |
| Root `CMakeLists.txt` | nobody above the current last line; P0.5, D1, B1, A2 and A3 implementers each **append one delimited block at the end** | test-authors add none (they use `engine/tests/oracles/CMakeLists.txt`) |
| `tools/trace/**`, `tools/tests/test_v1_*.py`, `docs/dev/traceability.md`, `.github/workflows/nightly.yml`, `tools/ci/**`, `cmake/pme_testing.cmake`, `engine/tests/framework/**`, `engine/tests/rtcheck/**`, `tools/analysis/**`, `tools/bench/**`, `docs/dev/testing.md`, `docs/dev/signal-analysis.md` | V1 and P0.3 (complete; follow-ups only when the orchestrator routes them) | read only |
| `CMakePresets.json`, `cmake/pme_module.cmake`, `cmake/compiler_options.cmake`, `cmake/sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody | read only |
| `engine/include/pme/**`, the other `engine/src/*/` stubs (including `engine/src/runtime/*.cpp`), `engine/tests/interfaces/**` (except the A2 row), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2 = 300b995; changes only by ICR) | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

When two phases append at the same place (end of the root `CMakeLists.txt`, end of `engine/tests/oracles/CMakeLists.txt`), the orchestrator keeps both blocks when merging. Keep blocks self-contained so that order does not matter.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A3: SAV framework ----
target_include_directories(pme_nonlinear PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/nonlinear/include")
target_link_libraries(pme_nonlinear PUBLIC pme_osc)
add_subdirectory(engine/tests/nonlinear)
# ---- end A3 ----
```

In docs/dev/interfaces.md ("Stubs (P0.4-R3)") drop `nonlinear` from the placeholder list and add "(`nonlinear`: SAV framework by A3)". A2 edits the same sentence for `string`; if both merge, the orchestrator keeps both one-word changes. That sentence is the only edit you make outside your rows.

## Scope

- May edit (create or change): the paths the ownership table gives the **A3 implementer**, your root `CMakeLists.txt` block, and the one sentence of docs/dev/interfaces.md above.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A3-R1 `Potential` interface (value, gradient) and segment coordinate stacking (modes plus lumped coordinates with masses).
2. A3-R2 `savStep` per SPEC-NL: g evaluation, half-step ψ, explicit forces, dashpot pairs.
3. A3-R3 Woodbury solve: closed form for r = 1; a small dense solve in float64 for r ≤ 4.
4. A3-R4 Skip rule, reset-at-separation rule and persistent-contact drift regulation (power-balanced regulation, or the SPEC relaxation fallback).
5. A3-R5 Diagnostics counters for resets and drift corrections.

### How to meet each requirement

Everything lives in `engine/src/nonlinear/` with internal headers under `engine/src/nonlinear/include/pme/nonlinear/`, namespace `pme::nl`. Every formula cites `// SPEC-NL <paragraph>`.

**Precision and templates.** Write the step once as a template on the state type `S` (as A1's `stepModes`), so tests run it in float64 (A3-T1, A3-T2 say "float64 build") and the engine instantiates float32. Whatever `S` is: ψ, V, ε₀, every reduction (wᵀa, wᵀh, gᵀ(zⁿ⁺¹ − zⁿ⁻¹), energy) and the r × r solve are float64, accumulated in fixed index order (Numerics rules 1, 3, 5). No algebraic rearrangement of SPEC-NL's update beyond what is written here (Numerics rule 2).

**A3-R1 — coordinates and potentials.**

- `SavSegment<S>` (SPEC-NL "Coordinates"): a stack of n\_z coordinates — the modal coordinates of the segment (both polarisations when a contact needs them) followed by lumped coordinates (hammer, plectrum or finger tip) — each with its oscillator coefficients β, κ, γ (from A1; a free lumped mass has ω̂ = σ̂ = 0, so β = 1, κ = 0, γ = Δt²) and its mass m\_i (ρA for string modes, m\_H for a hammer). Store the state in an A1 `ModeBank` (64-byte aligned, padded) plus a mass array, ψ^(n−½) (float64), Vⁿ⁻¹ (float64), the contact-duration counter and the diagnostics counters. Allocation only in a non-real-time `configure(nModes, nLumped, maxDashpots, fs, …)`; `InvalidArgument` if `maxDashpots` > 3 (SPEC-NL: more than 3 dashpot directions must be lumped by the caller into the diagonal damper form).
- `Potential<S>` (SPEC-NL "Contracts"): an interface with `double value(std::span<const S> z) const noexcept` returning V ≥ 0 and `void gradient(std::span<const S> z, std::span<double> grad) const noexcept` writing ∇V. Several potentials on one segment add (one shared ψ, rank 1). The gradient buffer belongs to the segment (preallocated).
- `Dashpot<S>`: a direction **c** (span over the stacked coordinates), a coefficient Rⁿ ≥ 0 set by the caller each sample (Hunt–Crossley and damper laws are the callers'), and a flag `alongGradient` that the caller sets when **c** is parallel to **g** (one contact: SPEC-NL "For one contact, g and c are parallel and r = 1"); such a dashpot merges into the SAV rank so r stays 1.

**A3-R2 — `savStep`** (SPEC-NL "Discrete scheme", "Explicit low-rank solve"): `void savStep(SavSegment<S>&, std::span<const Potential<S>* const>, std::span<const Dashpot<S>>, std::span<const S> explicitForce) noexcept`, one sample:

1. zⁿ = qⁿ, zⁿ⁻¹ = qⁿ − uⁿ (difference form, A1). Vⁿ = Σ V\_j(zⁿ); ∇Vⁿ = Σ ∇V\_j(zⁿ); **g**ⁿ = ∇Vⁿ / √(2(Vⁿ + ε₀)), ε₀ = 10⁻¹² J (configurable).
2. Apply the reset and drift rules (A3-R4) to ψ^(n−½).
3. **a** = free oscillator update of every coordinate with the explicit force Fⁿ/m (SPEC-OSC difference form: uⁿ⁺¹ = βuⁿ − κqⁿ + γfⁿ, aᵢ = qⁿ + uⁿ⁺¹), then the known SAV terms −**h**\_g ψ^(n−½) + **h**\_g (**g**ᵀ zⁿ⁻¹)/4 and the known part of each dashpot (+**h**\_c (**c**ᵀ zⁿ⁻¹)/(2Δt)), with **h**\_g = γ ⊙ **g** / m, **w**\_g = **g**/4, **h**\_c = Rⁿ γ ⊙ **c** / m, **w**\_c = **c**/(2Δt) (SPEC-NL). Derive the signs from SPEC-NL's discrete scheme and write the derivation in docs/dev/nonlinear.md.
4. zⁿ⁺¹ = **a** − H (I\_r + WᵀH)⁻¹ Wᵀ**a** (A3-R3); uⁿ⁺¹ = zⁿ⁺¹ − qⁿ; qⁿ⁺¹ = zⁿ⁺¹.
5. ψ^(n+½) = ψ^(n−½) + ½ (**g**ⁿ)ᵀ(zⁿ⁺¹ − zⁿ⁻¹); store Vⁿ for the next step.

Provide `double savEnergy(const SavSegment<S>&)` = Σᵢ mᵢ Hᵢ^(n+½) + ½(ψ^(n+½))² − ε₀ (SPEC-NL; Hᵢ from SPEC-OSC "Discrete energy", evaluated in float64 from the stored state) and `double savDissipation(...)` returning the dashpot energy removed in the last step, Δt Σ Rⁿ (**c**ᵀ δ\_t· zⁿ)², so tests can check the balance.

**A3-R3 — Woodbury.** r = (1 for the SAV pair, if any potential is active) + (one per dashpot not `alongGradient`) ≤ 4. r = 1: zⁿ⁺¹ = **a** − **h** (**w**ᵀ**a**)/(1 + **w**ᵀ**h**) (SPEC-NL). r = 2 … 4: form M = I\_r + WᵀH and Wᵀ**a** in float64, solve M **y** = Wᵀ**a** by Gaussian elimination with partial pivoting on fixed-size arrays (no allocation), zⁿ⁺¹ = **a** − H**y**. M has real eigenvalues ≥ 1 (H and W are positive diagonal scalings of the same directions), so it is never singular; assert that in debug builds. Keep the r = 1 path separate (it is the hot path).

**A3-R4 — rules** (SPEC-NL "Skipping", "Drift control"):

- **Skip (MUST):** when Vⁿ = 0 and every Rⁿ = 0, the step is the free update (**g** = 0, no solve, ψ unchanged). Do not evaluate gradients in that case.
- **Reset at separation (MUST):** when Vⁿ = Vⁿ⁻¹ = 0, set ψ^(n−½) ← √(2ε₀). It can only remove energy.
- **Persistent contact (SHOULD in SPEC, required here):** implement the SPEC relaxation fallback. When V has been positive for more than 50 ms of consecutive samples and ψ^(n−½) > ψ\* = √(2(Vⁿ + ε₀)), set ψ^(n−½) ← ψ\* + (ψ^(n−½) − ψ\*) e^(−Δt/τ), τ = 10 ms. It only lowers ψ, so it only removes energy. Thresholds configurable; the rule can be disabled per segment (A3-T2 disables it). Power-balanced regulation is not required; if you implement it instead, say so in docs/dev/nonlinear.md with its reference, keep the fallback selectable, and show A3-T3 and A3-T4 hold for both.

**A3-R5 — counters.** Per segment, monotonically increasing `std::uint32_t` counters: resets that changed ψ (ψ was above √(2ε₀)), drift corrections applied, and solves skipped (for A8 and V2). Plain members, read and cleared by the runtime (C1) on the audio thread; no atomics. C1 turns them into `Diagnostic` records (SPEC-API "Diagnostics").

## Tests that must pass

- **ORC-08** with a rigid-wall test potential — written by the A3 test-author (docs/agents/briefs/A3-test-author.md), not by you. Make it possible: a single lumped coordinate with a user-written `Potential` must run through `savStep`. Do not write tests named `ORC-08`.
- **A3-T1** Woodbury result equals a dense solve within 10⁻¹² on random systems (float64 build);
- **A3-T2** a quartic test potential across many modes conserves energy within 10⁻¹⁰ (float64);
- **A3-T3** any dashpot makes energy non-increasing;
- **A3-T4** the reset rule fires on separation and never raises energy.

### How each test is built (`engine/tests/nonlinear/`, doctest, label `unit`)

**A3-T1.** 1 000 random systems (fixed seed): n\_z from 1 to 256, r from 1 to 4 (SAV pair plus 0–3 dashpots, some `alongGradient`), positive random γ and m, random **g**, **c**, **a**, Rⁿ. Compare the `savStep` solve (expose the solve as a function so it can be called directly) with a dense n\_z × n\_z solve of (I + H Wᵀ) zⁿ⁺¹ = **a** by LU with partial pivoting written in the test. Pass: ‖z\_W − z\_dense‖ / ‖z\_dense‖ ≤ 10⁻¹² in float64. Report the worst case.

**A3-T2.** float64 instantiation, 64 lossless modes (σ = 0, frequencies spread over 50 Hz–5 kHz at 48 kHz, unit masses or ρA) plus no lumped coordinate, drift regulation disabled, two potentials in separate cases: V = (K/4)(**c**ᵀ**z**)⁴ with random **c**, and V = (K/4)(Σ wₙ zₙ²)² (the tension-modulation shape, wₙ > 0). Initial condition with V large compared with ε₀. Run 10⁵ steps. Pass: max over n of |Eⁿ − E⁰| / E⁰ ≤ 10⁻¹⁰ with E = `savEnergy`. Report the drift.

**A3-T3.** As A3-T2 plus one or more dashpots with Rⁿ > 0 (constant, and varying with the state), in float64: every step Eⁿ⁺¹ − Eⁿ ≤ 10⁻¹³ E⁰ (round-off allowance), and Eⁿ⁺¹ − Eⁿ + `savDissipation` = 0 within 10⁻¹² E⁰. Cases: r = 1 (`alongGradient`), r = 2, r = 4.

**A3-T4.** A lumped mass bouncing off a one-sided contact potential (V = K/(p+1) [z − z\_w]₊^(p+1)), float64: at each separation (Vⁿ = Vⁿ⁻¹ = 0 after contact) the reset counter increments exactly once, ψ becomes √(2ε₀), and E after the reset ≤ E before (record both). Over 20 contacts the counter equals the number of separations. Also check the reset never fires during contact.

### Other tests

- `A3-R4 …`: skip rule (V = 0, R = 0) leaves the result bit-identical to A1's `stepModes` free update and increments the skip counter; the drift relaxation fires only after 50 ms of contact, only lowers ψ, and never raises E (float64, a held contact of 200 ms).
- `A3-R2 …` RT-safety: `savStep` with r = 1 and r = 4 inside `pme::rtcheck::ScopedRealtimeThread`, label `RT`, so it runs under the `rt-safety` preset; float32 instantiation.
- `A3-R1 …`: `configure` rejects more than 3 dashpots; padding lanes stay zero.
- Benchmark `engine/tests/nonlinear/sav_bench.cpp` (`pme_add_benchmark`, label `perf`): `savStep` float32 for n\_z = 65 and 257 with r = 1 and r = 4, and the skip path; work unit "coordinate-samples". No baseline file (D-4, V2); report the numbers.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A3:

- Module note: docs/dev/nonlinear.md — SPEC-NL sections implemented; the derivation of **a** and the signs; the internal API (`SavSegment`, `Potential`, `Dashpot`, `savStep`, `savEnergy`, `savDissipation`, counters) for A5, A6, A7, P1, P2 and A8; the drift rule chosen and its parameters; the precision choices.
- `savStep` is reachable from `Engine::process` (through C1): `noexcept`, no allocation, no locks, no I/O; RT-safety test as above.
- Coverage: `engine/src/nonlinear` is part of the 90% engine/src line-coverage gate.
- Performance baseline: the benchmark exists and runs nightly; the baseline file waits for V2 and D-4.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A3: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` and `ctest --preset linux-rt-safety` with GCC and with Clang; `uv run pytest tools/tests/test_p0_3_r1.py` (test names).
- If something cannot be done as written (a tolerance not reached, SPEC unclear), stop that item, do not loosen a tolerance, and describe it in the report with the measured values; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, presets, the A3-T1 worst error, the A3-T2 drift per potential, the A3-T3 worst per-step increase, the A3-T4 reset counts, benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A3`, `role` `implementer`, `model` `sonnet`; `requirements` A3-R1 to A3-R5; `tests` A3-T1 to A3-T4 (ORC-08 is the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `open_risks` as found.
