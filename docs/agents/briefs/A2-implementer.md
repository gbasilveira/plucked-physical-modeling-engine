# Brief: A2 Stiff string compiler — implementer

Status: **ready once A1 is complete** (A1 merged at 1512985, in review). Do not start before the orchestrator marks A1 complete in docs/status/phase-status.md; if A1's review changes the osc internal headers, start from the commit that contains those changes.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A2-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

turns string geometry, materials, tuning and losses into a mode table, oscillator coefficients and shape vectors.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Numerics rules, Testing policy); the phase card **A2 Stiff string compiler**; for context the cards **A1** (your input), **A4** (`compileBridgeTerms` consumes `StringModes`), **A5**, **A6** (consume φ̄) and **C1** (compileInstrument calls you).
- SPEC and PLAN sections:
  - SPEC-STR — docs/plan/engine-specification.md, "Stiff string model" (all of it)
  - For the coefficients and the bandwidth rule you call: SPEC-OSC — "The modal oscillator" ("Bandwidth", "Contract")
  - SPEC-CONV — "Scope, conventions and notation" (units, precision)
- engine/include/pme/string.hpp — the frozen declaration you implement (`StringParams`, `StringModes`, `compileString`); engine/include/pme/instrument.hpp (`TuningCurve`, `SegmentDesc::unisonDetuneCents`, `muteRatio`).
- docs/dev/osc.md and the A1 internal headers `engine/src/osc/include/pme/osc/oscillator.hpp` (`compileOscillatorF64`, `passesBandwidthRule`, `dampedFrequencyTimesDt`) — call them, do not re-implement them.
- docs/spec-issues/SI-0001-compile-string-registered-points.md (point shapes, W = 1 in `compileString`), SI-0004-tuning-keys-and-segment-addressing.md (f₁ formula, key = −1), SI-0010-rear-segment-tension-and-mute.md (open: At0 tension and σ\_felt).
- docs/adr/ADR-0006-interface-status-returns.md.
- docs/dev/testing.md and docs/dev/signal-analysis.md.
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A1** (modal oscillator; merged at 1512985 on `ccr-5ef35d73-oqb6fd`, `pme_osc` with internal headers under `engine/src/osc/include/pme/osc/`, docs/dev/osc.md). Through G0: P0.3 test framework and `pme_analysis`, P0.4 headers.
- Frozen interfaces: **interfaces-v2 = 300b995** (ICR-0001 changed only comments in string.hpp: `phiBar` holds point values φ\_n(x₀) with W = 1; `f1Target` is replaced by compileInstrument for keys ≥ 0). If `interfacesVersionMajor` is not 2, stop and report.
- Current state of your module: `engine/src/string/string_stub.cpp` returns `NotImplemented`; `engine/tests/interfaces/test_stubs.cpp` checks that in its `compileString` SUBCASE.
- P0.5 (reference solver) is in progress; A2-T3 (against P0.5) is the A2 test-author's, not yours.

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
# ---- A2: stiff string compiler ----
target_include_directories(pme_string PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/string/include")
target_link_libraries(pme_string PUBLIC pme_osc)
add_subdirectory(engine/tests/string)
# ---- end A2 ----
```

(`pme_string` needs `pme_osc` for `compileOscillatorF64` and `passesBandwidthRule`; do not edit `cmake/pme_module.cmake`.) A3 edits the same sentence of docs/dev/interfaces.md ("Stubs (P0.4-R3)") for `nonlinear`; if both merge, the orchestrator keeps both one-word changes.

## Scope

- May edit (create or change): the paths the ownership table gives the **A2 implementer**, plus your root `CMakeLists.txt` block.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A2-R1 Wound-string ρA and EI formulas, including packing factor and ε\_w.
2. A2-R2 Closed-form tension for pitch; tuning curve = equal temperament plus stretch plus unison detune.
3. A2-R3 Pinned frequencies, and clamped ends with Fletcher's correction.
4. A2-R4 Losses b₁, b₂, b₃ with an optional per-partial override table.
5. A2-R5 Truncation by the bandwidth rule; two polarisations with horizontal detune.
6. A2-R6 Shape vectors φ̄ for point, rectangular and raised-cosine windows at registered points, computed in float64 and stored float32.
7. A2-R7 Rear and duplex segments (BridgeEnd At0) with mute ratio.

### How to meet each requirement

Implement `pme::compileString(const StringParams&, double fs, double fMax, std::span<const double> registeredPoints, StringModes& out)` in `engine/src/string/` (delete `string_stub.cpp`). Internal helpers go in `engine/src/string/include/pme/string/*.hpp`, namespace `pme::string`, so tests, compileInstrument (C1) and later phases can call them. Every formula cites `// SPEC-STR <paragraph>`. All arithmetic in float64; only `phiBar` and the float32 fields of `OscCoeffs` are float32 (Numerics rule 1). Status per ADR-0006: `InvalidArgument` for non-physical input (fs ≤ 0 or not finite; L, dCore, rhoCore, ECore ≤ 0; dOuter < dCore; dOuter > dCore with rhoWind ≤ 0 or packing outside (0, 1]; epsWind < 0; f1Target ≤ 0; b₁, b₂, b₃ < 0; any `sigmaOverride` entry < 0 or not finite; a registered point outside [0, L]; T ≤ 0 from the tension formula); `Rejected` when mode 1 fails the bandwidth rule; `out` unchanged unless `Ok`.

**A2-R1.** ρA = (π/4)[ρ\_c d\_c² + η\_w ρ\_w (d\_o² − d\_c²)], EI = (1 + ε\_w) π E\_c d\_c⁴ / 64 (SPEC-STR "Wound strings"). A plain string has d\_o = d\_c (the winding term is exactly 0 whatever ρ\_w and η\_w hold). Expose `pme::string::linearDensity(const StringParams&)` and `bendingStiffness(const StringParams&)`.

**A2-R2.** T = 4L²ρA f₁² − π²EI/L² (SPEC-STR "Tension for pitch"), with f₁ = `params.f1Target`. Expose:

- `double tuningF1(const TuningCurve&, int key, double unisonDetuneCents)` implementing f₁ = f\_ref 2^((key − 69)/12) 2^((stretch[key] + d\_s)/1200), stretch[key] = 0 beyond the vector (SPEC-STR, SI-0004). compileInstrument (C1) calls it for groups with key ≥ 0; `compileString` itself always uses `f1Target` as given.
- `double f1ForTension(const StringParams&, double T)` = √((T + π²EI/L²) / (4L²ρA)), the inverse, so a caller can give a rear segment a prescribed tension (SI-0010 item 1, proposed).

Store T, ρA, EI and B = π²EI/(TL²) in `StringModes`.

**A2-R3.** ω\_n² = (T k\_n² + EI k\_n⁴)/ρA, k\_n = nπ/L (SPEC-STR "Modes"), computed as n²ω₀²(1 + Bn²) or directly — choose the form with less cancellation and say which in docs/dev/string.md (both are sums of positive terms; the requirement is ORC-03 at 10⁻⁹). Clamped (`Boundary::Clamped`): shapes stay pinned; ω\_n is multiplied by Fletcher's factor (1 + 2√B/π + 4B/π²) (SPEC-STR "Modes"). State in docs/dev/string.md that this is the factor applied to every n.

**A2-R4.** σ\_n = b₁ + b₂√ω\_n + b₃ω\_n² (SPEC-STR "Losses") with ω\_n the (possibly Fletcher-corrected) undamped frequency; if `sigmaOverride` is non-empty, σ\_n = `sigmaOverride[n−1]` for n ≤ its size and the formula for higher n. Document the rule in docs/dev/string.md.

**A2-R5.** Truncation (SPEC-STR "Truncation"; SPEC-OSC "Bandwidth"): N is the largest n such that every mode 1 … n passes `pme::osc::passesBandwidthRule(ω_n, σ_n, fs, fMax)` for the vertical **and** the horizontal polarisation (one shared table, SPEC-STR "Polarisations"). Horizontal: ω\_h,n = ω\_n · 2^(Δ\_h/1200) with Δ\_h = `detuneH`, same σ\_n. Fill `k`, `omega`, `sigma` (vertical, n = 1 … N) and `oscV`, `oscH` with `compileOscillator` (public) or from `compileOscillatorF64`. Expose `int modeCount(const StringParams&, double fs, double fMax)` for tests and C1's budget.

**A2-R6.** `phiBar[p][n−1]` = φ\_n(x₀,p) = √(2/L) sin(k\_n x₀,p), evaluated in float64 (form the argument as nπ(x₀/L) to keep it exact at the ends) and rounded to float32 (SPEC-STR "Contract", SI-0001: `compileString` stores W = 1). The window factor is applied by the caller, so provide in `pme::string`:

- `double windowFactor(WindowKind kind, double k, double b)` with `WindowKind` ∈ {Point, Rectangular, RaisedCosine}: W = 1; sin(u)/u; (sin(u)/u)·π²/(π² − u²), u = kb/2 (SPEC-STR "Forces and observations"). Handle the removable singularities without loss of accuracy: u → 0 (both windows; use a series for |u| < 10⁻⁴ or similar) and u → π for the raised cosine (W(π) = 1/2; use a series or the identity sin(u)/(π² − u²) = sin(π − u)/((π − u)(π + u)) and evaluate sin(π − u)/(π − u) as a sinc). The relative error must stay at or below 10⁻¹² over u ∈ [0, 20] apart from the zeros of W, including within 10⁻⁸ of π (ORC-05 checks 10⁻⁹).
- `void windowedShapes(const StringModes&, double x0, WindowKind, double b, std::span<float> out)` forming φ̄\_n = φ\_n(x₀) W(k\_n) in float64 from L, k\_n and x₀ (not from the float32 `phiBar`) and storing float32 — the function compileInstrument will call for hammers, dampers and fingers.

**A2-R7.** A segment with `bridgeEnd = At0` is compiled by the same `compileString` call: its own L (= L\_d), its own `f1Target`, the same shapes (positions on its own x axis); `BridgeEnd` only matters to `compileBridgeTerms` (A4). The two open points of SI-0010 are handled outside `compileString`:

- Tension: a caller that must give an At0 segment the speaking string's T sets `f1Target = f1ForTension(params, T)` first (A2-R2). Test that compiling with that f₁ reproduces T to 10⁻¹² relative.
- Mute: provide `void applyMute(StringModes&, double muteRatio, double sigmaFelt)` adding σ\_mute = m\_d σ\_felt to every σ\_n and recompiling `oscV`/`oscH` (and re-truncating if a mode becomes overdamped; document). σ\_felt is an **argument without a default**: SPEC does not define it yet (SI-0010 item 2). `InvalidArgument` for m\_d outside [0, 1] or σ\_felt < 0.

## Tests that must pass

- **ORC-03** (Stiff-string frequencies), **ORC-04** (Tension for pitch), **ORC-05** (Window shape): written by the A2 test-author (docs/agents/briefs/A2-test-author.md), not by you. Make them possible: `compileString` and the `pme::string` helpers above must be reachable from `pme_string` through the public header and your internal headers. Do not write tests named `ORC-03`, `ORC-04` or `ORC-05`.
- **A2-T1** wound-string mass and stiffness unit tests;
- **A2-T2** mode counts match the bandwidth rule at 44.1, 48 and 96 kHz;
- **A2-T3** against P0.5, a freely vibrating stiff string matches partial frequencies within 0.05 cent and decay rates within 1% — written by the A2 test-author, not by you.

### How each test is built

**A2-T1** (`engine/tests/string/`, doctest `A2-T1 …`, label `unit`): hand-computed ρA and EI (worked in the test with the SPEC-STR formulas written out) for (a) a plain steel string d = 1.0 mm, ρ = 7850 kg/m³, E = 200 GPa; (b) a wound bass string d\_c = 1.6 mm, d\_o = 4.7 mm, ρ\_c = 7850, ρ\_w = 8960, η\_w = 0.85, ε\_w = 0; (c) the same with ε\_w = 0.1; (d) η\_w at 0.75 and 0.9. Pass: 10⁻¹² relative. Also: plain strings ignore ρ\_w and η\_w; the invalid inputs listed above return `InvalidArgument` with `out` unchanged.

**A2-T2**: for fs = 44 100, 48 000, 96 000 Hz and three strings (a treble plain string with f₁ ≈ 2 kHz and B ≈ 10⁻², a mid string f₁ ≈ 262 Hz, a bass wound string f₁ ≈ 27.5 Hz), with f\_max = 16 kHz and f\_max = +∞, compute the expected N independently in the test (loop n upward evaluating ω\_n, σ\_n, ω\_d,nΔt and ω\_d,n/2π from the SPEC formulas in float64; stop at the first failure) and check `modeCount` and `StringModes::k.size()` equal it. Include a case where f\_max binds, one where 0.95π binds, one where a large b₃ makes a high mode overdamped, and one where `detuneH` = +5 cents makes the horizontal polarisation the binding one. Report N per case in docs/dev/string.md.

### Other tests (unit label)

- `A2-R2 …`: `tuningF1` at keys 21, 69, 108 with and without stretch and detune (10⁻¹⁴ relative against the formula); `f1ForTension` inverts the tension formula to 10⁻¹².
- `A2-R3 …`: clamped frequencies equal the pinned ones times Fletcher's factor to 10⁻¹⁴.
- `A2-R4 …`: override shorter than N, equal to N, longer than N; values used exactly.
- `A2-R6 …`: `phiBar` at x₀ = 0 and x₀ = L is exactly 0 for every n; at x₀ = L/2 the even modes are 0 to float32 precision; float32 values equal the correctly rounded float64 values; `windowFactor` limits at u = 0 and u = π.
- `A2-R7 …`: `applyMute` with m\_d = 0 leaves the tables bit-identical; m\_d = 1 adds σ\_felt exactly.
- Benchmark `engine/tests/string/string_bench.cpp` (`pme_add_benchmark`, label `perf`): compile one bass and one treble string at 48 kHz with 4 registered points. No baseline file (D-4, V2); report the numbers.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A2:

- Module note: docs/dev/string.md — SPEC-STR sections implemented; the internal API (`linearDensity`, `bendingStiffness`, `tuningF1`, `f1ForTension`, `modeCount`, `windowFactor`, `windowedShapes`, `applyMute`) for C1, A4, A5, A6; the ω\_n evaluation form; the Fletcher and override rules; the truncation rule for two polarisations; the SI-0010 status; N per A2-T2 case.
- `engine/tests/interfaces/test_stubs.cpp`: replace only the `compileString` SUBCASE with one that checks `InvalidArgument` for L = 0 leaves `out` unchanged (the rest of the P0.4-R3 test stays). In docs/dev/interfaces.md drop `string` from the stub-module list in "Stubs (P0.4-R3)" and add "(`string`: implemented by A2)".
- `compileString` runs on non-audio threads only; no RT-safety test is required. `applyMute` likewise.
- Coverage: `engine/src/string` is part of the 90% engine/src line-coverage gate.
- Performance baseline: the benchmark exists and runs nightly; the baseline file waits for V2 and D-4.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A2: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` with GCC and with Clang; `uv run pytest tools/tests/test_p0_3_r1.py` (test names).
- If something cannot be done as written (a tolerance not reached, SPEC unclear), stop that item, do not loosen a tolerance, and describe it in the report with the measured values; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, presets, the worst A2-T1 error, N per A2-T2 case, the `windowFactor` worst relative error near u = 0 and u = π, benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A2`, `role` `implementer`, `model` `sonnet`; `requirements` A2-R1 to A2-R7; `tests` A2-T1 and A2-T2 (ORC-03 to ORC-05 and A2-T3 are the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0001, SI-0004, SI-0010 as relevant plus any new defect; `open_risks` as found.
