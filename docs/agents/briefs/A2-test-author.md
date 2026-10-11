# Brief: A2 Stiff string compiler — test-author

Status: **ready once A1 is complete and the A2 implementer's commits are merged** (A1 merged at 1512985, in review). **A2-T3 additionally needs P0.5** (reference solver, in progress) merged with its string solver (P0.5-R1) and its library or CLI (P0.5-R7). Write ORC-03 to ORC-05 first; write A2-T3 only once the orchestrator confirms P0.5 is merged, otherwise report it `not run` and stop.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

turns string geometry, materials, tuning and losses into a mode table, oscillator coefficients and shape vectors.

Your job: write the three SPEC-ORC oracle tests the A2 card names, **ORC-03**, **ORC-04** and **ORC-05**, and the reference-solver comparison **A2-T3**, independently of the A2 implementer, deriving every expected value from SPEC formulas, closed-form results, independent quadrature or the P0.5 reference solver, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase cards **A2 Stiff string compiler** and **P0.5 Reference solver**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-STR — docs/plan/engine-specification.md, "Stiff string model" ("Modes", "Wound strings", "Tension for pitch", "Losses", "Forces and observations", "Truncation", "Contract")
  - SPEC-OSC — "The modal oscillator" ("Difference form", "Bandwidth") — for running the compiled modes in A2-T3
  - SPEC-ORC — "Verification oracles", rows "Stiff-string frequencies", "Tension for pitch", "Window shape"
  - PLAN-VAL — docs/plan/implementation-plan.md, "Validation and testing" (reference-solver comparisons)
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md ("Oracle registry", tolerance macros); docs/dev/signal-analysis.md (partial tracking, decay fitting).
- engine/include/pme/string.hpp (public) and the A2 internal headers under `engine/src/string/include/pme/string/` (`windowFactor`, `windowedShapes`, `tuningF1`, `f1ForTension`, `linearDensity`, `bendingStiffness`); the A1 internal header `engine/src/osc/include/pme/osc/mode_bank.hpp` (to run modes). Read headers only, never `engine/src/string/*.cpp`.
- For A2-T3: docs/dev/reference-solver.md and reference/README.md (P0.5: scenario format, CLI, library target, the b₂ = 0 restriction, grid refinement tool).

## Inputs

- Completed dependencies and their outputs: A1 (`pme_osc`), the A2 implementer's commits (`git log --grep '^A2: '`), G0 (P0.3 `pme_test_framework`, `pme_analysis`). For A2-T3: P0.5 merged (`reference/`, `tools/reference-cli/`).
- Frozen interfaces: interfaces-v2 = 300b995.

## Concurrent work

The ownership table in docs/agents/briefs/A2-implementer.md ("Concurrent work") applies to you. Your rows: `engine/tests/oracles/string/**` (new); in `engine/tests/oracles.md` the status and test-location cells of rows ORC-03, ORC-04, ORC-05 only; one line `add_subdirectory(string)` appended at the end of `engine/tests/oracles/CMakeLists.txt` (the A3 test-author appends `add_subdirectory(nonlinear)`; the orchestrator keeps both). No root `CMakeLists.txt` block.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/` or `reference/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A2-R1 to A2-R7, see docs/agents/briefs/A2-implementer.md). You verify them through ORC-03, ORC-04, ORC-05 and A2-T3.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-03 | Stiff-string frequencies | Compiled table vs n f₀ √(1 + B n²) | Exact formula | 10⁻⁹ relative |
| ORC-04 | Tension for pitch ✓ | Closed-form T, then first partial | f₁ target | 10⁻⁹ relative |
| ORC-05 | Window shape ✓ | Raised-cosine W(k) vs quadrature, n up to 60 | Same value | 10⁻⁹ relative |

From the card: **A2-T3** against P0.5, a freely vibrating stiff string matches partial frequencies within 0.05 cent and decay rates within 1%.

### How to build them

All doctest, labels `oracle` and `unit` (A2-T3: `integration`), one `TEST_CASE` per case, names start with the ID and contain no commas.

**ORC-03.** For at least four strings (plain steel f₁ = 110 Hz and 2 kHz; wound bass f₁ = 27.5 Hz; one with B ≈ 10⁻² and one with B ≈ 10⁻⁵), pinned, compile at 48 kHz with f\_max = +∞. Compute f₀ = (1/2L)√(T/ρA) and B = π²EI/(TL²) in the test from the SPEC-STR formulas (your own ρA, EI and T, not the implementation's helpers), and check every compiled `omega[n−1]/2π` against n f₀ √(1 + Bn²) for all kept n. Pass: 10⁻⁹ relative. Add one clamped case checking the Fletcher-corrected values (same tolerance; name it `ORC-03 clamped …`).

**ORC-04.** For the same strings and for f₁ at keys 21, 45, 69, 108 (from your own equal-temperament formula), compute T from the closed form in the test, compile with that f₁, and check (a) `StringModes::tension` against your T and (b) the compiled first partial ω₁/2π against f₁. Pass: 10⁻⁹ relative for both.

**ORC-05.** Compute φ̄\_n = ∫ g(x) φ\_n(x) dx in float64 by adaptive or high-order Gauss–Legendre quadrature in the test (raised-cosine g of full width b and unit area centred at x₀, φ\_n = √(2/L) sin(k\_n x)), and compare with φ\_n(x₀)·`windowFactor(RaisedCosine, k_n, b)` and with `windowedShapes` (float32, compare at float32 rounding: |stored − round₃₂(quadrature)| ≤ 1 ulp). Cases: n = 1 … 60; (L, b, x₀) = (1 m, 10 mm, 0.12 m), (1 m, 50 mm, 0.3 m) — this one has u = k\_n b/2 = π exactly at n = 40, the removable singularity — and (0.05 m, 5 mm, 0.006 m). Choose x₀ so the window stays inside [0, L]. Pass: 10⁻⁹ relative for the float64 values. Where |φ̄\_n| < 10⁻⁶ of max\_n |φ̄\_n| (near a zero of sin(k\_n x₀)), relative error is not meaningful: use the same 10⁻⁹ relative to max\_n |φ̄\_n| and say so in the test comment. Report the worst error and where it occurs.

**A2-T3** (engine/tests/oracles/string/, label `integration`, may be registered only for optimised builds if a fine reference grid needs it, as P0.5's tests do; each case under 60 s):

- Strings: a plain steel string (L = 0.64 m, f₁ ≈ 262 Hz, B ≈ 4 × 10⁻⁴) and a stiffer one (B ≈ 10⁻²), b₁ = 0.5 s⁻¹, **b₂ = 0** (the reference accepts only b₂ = 0), b₃ = 2 × 10⁻⁹ s, pinned, fixed bridge (the reference's rigid end; no bridge coupling in A2).
- Engine: `compileString` at 48 kHz with one registered point (the observation point x\_obs = 0.37 L); run the vertical polarisation's modes in the A1 `ModeBank` (float32 kernel) from a small triangular initial displacement at 0.12 L (q\_n(0) from your own projection integral in float64), observe y(x\_obs) = Σ φ\_n(x\_obs) q\_n for 2 s.
- Reference: the same string and initial condition in P0.5 at a grid fine enough that its own partial-frequency error (from its refinement tool, or Richardson extrapolation over three grids) is below 0.01 cent for the compared partials; same observation point.
- Analysis: `pme_analysis` partial tracking and decay fitting on both signals; compare the first 10 partials (or all below 5 kHz if fewer).
- Pass: |1200 log₂(f\_engine/f\_ref)| ≤ 0.05 cent and |σ\_engine/σ\_ref − 1| ≤ 0.01 for every compared partial. Report the per-partial errors and the reference's own error estimate.
- Link the engine (`pme_string`, `pme_osc`) and the reference in a test target only. If P0.5 exposes a library, link it; if it exposes only the CLI, run it from a CTest `FIXTURES_SETUP` test and read its features JSON. Never include `reference/` sources into engine targets.

**Registry.** Update rows ORC-03, ORC-04 and ORC-05 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/string/<file>`. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-03 to ORC-05 and A2-T3 exist under those names, pass locally in `linux-debug` (A2-T3 in the preset where it is registered) with GCC and Clang, are clang-format and clang-tidy clean, and follow the naming rule (`uv run pytest tools/tests/test_p0_3_r1.py`). If a test fails against the implementation, do not weaken it: report the measured values; the orchestrator routes the fix to the implementer, or the planner files a spec issue.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A2: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A2`, `role` `test-author`, `model` `sonnet`; `requirements` an empty object; `tests` ORC-03, ORC-04, ORC-05 and A2-T3 (`not run`), with the measured worst errors in `notes`; `outputs` your files; `pull_requests` empty.
