# Brief: A4 Bridge coupling — test-author

Status: **ready (pending CI on A2/A3/B1)** once the A4 implementer's commits are merged (`git log --grep '^A4: '`). P0.5 is merged (103badf), so A4-T3 can be written now. Write ORC-06 and ORC-07 first.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

strings on a moving bridge, with pluggable bridge models, unison sharing and polarisation rotation.

Your job: write the two SPEC-ORC oracle tests the A4 card names, **ORC-06** and **ORC-07**, and the reference-solver comparison **A4-T3**, independently of the A4 implementer, deriving every expected value from SPEC formulas, closed-form results or the P0.5 reference solver, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase cards **A4 Bridge coupling** and **P0.5 Reference solver**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-BRG — docs/plan/engine-specification.md, "Bridge coupling" ("Small-admittance results", "Unison coupling", "Discrete coupling order")
  - SPEC-CONV — "Scope, conventions and notation" (the e^(+iωt) convention, SI-0012)
  - SPEC-ORC — "Verification oracles", rows "Bridge damping", "Unison coupling"
  - PLAN-VAL — docs/plan/implementation-plan.md, "Validation and testing" (reference-solver comparisons)
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md ("Oracle registry", tolerance macros); docs/dev/signal-analysis.md (partial tracking, decay fitting).
- engine/include/pme/bridge.hpp, engine/include/pme/string.hpp; the A4 internal headers under `engine/src/bridge/include/pme/bridge/`; the A1 `mode_bank.hpp` and A2 `string_model.hpp` internal headers. Read headers only, never `engine/src/bridge/*.cpp`.
- For A4-T3: docs/dev/reference-solver.md (especially "Accuracy of the reference itself" and "Using the reference from later phases"), reference/README.md ("Limits": b₂ = 0 only, no `detuneH`, no `bridgeEnd = At0`), and engine/tests/oracles/string/CMakeLists.txt (how A2-T3 runs `pme_reference_cli` only in optimised builds).

## Inputs

- Completed dependencies and their outputs: A2 (`pme_string`), A1 (`pme_osc`), the A4 implementer's commits, G0 (`pme_test_framework`, `pme_analysis`), **P0.5** (merged at 103badf: `reference/`, `pme_reference_cli`, the `refine` subcommand).
- Frozen interfaces: interfaces-v2 = 300b995.
- **Reference scope (recorded in docs/status/phase-status.md, P0.5 row):** b₂ = 0 only; no `sigmaOverride`, `detuneH` or `bridgeEnd = At0`; a lumped bridge (mass–spring–dashpot or conductance) shared by several strings, both polarisations, with rotation θ\_b; clamped ends only without a bridge. Comparisons at 0.05 cent MUST use a grid and time step fine enough for the compared partials, or the Richardson-extrapolated values that `pme_reference_cli refine` reports (`extrapolated_hz`).
## Concurrent work: A4, A6, B2 and B4 run at the same time

A4, A6, B2 and B4 (each with its test-author where briefed) run in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. A CI fixer and the D3 lv2lint follow-up may also be committing (`.github/workflows/pr.yml`, `docs/dev/ci.md`, `docs/dev/lv2.md`, `plugin/lv2/**`). Edits must not overlap. This table is identical in every A4, A6, B2 and B4 brief.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/bridge/**` (replaces `bridge_stub.cpp`), `engine/tests/bridge/**`, `docs/dev/bridge.md`; in `engine/tests/interfaces/test_stubs.cpp` the `compileBridgeTerms` SUBCASE only | A4 implementer | read only |
| `engine/tests/oracles/bridge/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-06, ORC-07 only | A4 test-author | read only |
| `engine/src/dampers/**` (replaces `dampers_placeholder.cpp`), `engine/tests/dampers/**`, `docs/dev/dampers.md` | A6 implementer | read only |
| `engine/tests/oracles/dampers/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-13 only | A6 test-author | read only |
| `compiler/pme_compiler/body/**` (including the shared `body/modal_blob.py`), `compiler/pme_compiler/materials/**`, `compiler/tests/test_b2_*.py` except `test_b2_t4_*.py` and `test_b2_r5_reference_*.py`, `compiler/tests/data/b2/**`, `docs/dev/body-compiler.md` | B2 implementer | B4 imports `pme_compiler.body.modal_blob` (read only) |
| `compiler/tests/test_b2_t4_*.py`, `compiler/tests/test_b2_r5_reference_*.py`, `compiler/tests/data/b2_t4/**`; in `.github/workflows/pr.yml` the one `uv run pytest tools/tests/test_p0_5_*.py …` line of the `reference-python` job (append one path, nothing else) | B2 test-author | read only |
| `compiler/pme_compiler/importers/**`, `compiler/tests/test_b4_*.py`, `compiler/tests/data/b4/**`, `docs/dev/body-import.md` | B4 implementer | read only |
| `engine/src/string/**`, `engine/src/nonlinear/**`, `engine/src/osc/**`, `compiler/pme_compiler/schema/**`, `engine/src/runtime/loader/**`, `instruments/examples/**`, `reference/**`, `tools/reference-cli/**` and their tests and module notes | A2, A3, A1, B1, P0.5 (complete or pending CI; follow-ups only when the orchestrator routes them) | read only; call their internal headers and Python APIs |
| `engine/include/pme/**`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime`, `exciters`, `barriers`, `sched`, `simd`), `engine/tests/interfaces/**` (except the A4 SUBCASE), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2 = 300b995; changes only by ICR) | read only |
| `.github/workflows/**`, `tools/ci/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases (CI fixer and D3 only), except the B2 test-author's one pr.yml line above | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in this order: A4, A6, B2, B4, then the B2 test-author's pr.yml line.** The second and later merges rebase onto the earlier ones; keep every block self-contained so the order does not change behaviour.

| Shared file | A4 | A6 | B2 | B4 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A4: bridge coupling ----` … `# ---- end A4 ----` appended after the last line | one block `# ---- A6: dampers and pedals ----` … `# ---- end A6 ----` appended after the last line | none | none |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends one line `add_subdirectory(bridge)` | test-author appends one line `add_subdirectory(dampers)` | none | none |
| `docs/dev/interfaces.md`, sentence under "Stubs (P0.4-R3)" | move `bridge` out of the stub list, add "`bridge`: implemented by A4" next to the existing "implemented by" notes | move `dampers` out of the placeholder list, add "`dampers`: implemented by A6" | none | none |
| `pyproject.toml`, `uv.lock` | none | none | none expected (scikit-fem, gmsh and meshio are already in the `fem` extra; CI installs `--all-extras`) | at most one new line in an existing extra, in its own commit, then `uv lock`; if B2 also changed the file, B4 rebases and reruns `uv lock` (never hand-merge `uv.lock`) |
| `cmake/dependencies.cmake` | none | none | none | none |
| `.github/workflows/pr.yml` | none | none | test-author: one line (see table), after the CI fix and D3 have merged | none |

**Rules for every agent in these phases.**

- Test names start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;` (they break ctest filters). Check with `uv run pytest tools/tests/test_p0_3_r1.py`.
- In any GitHub Actions workflow you are allowed to touch, a step `name:` that contains `: ` must be quoted, and you run `actionlint` on the edited workflow before committing. Phases other than the B2 test-author do not edit workflows.
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.

Your rows: `engine/tests/oracles/bridge/**` (new); in `engine/tests/oracles.md` the status and test-location cells of rows ORC-06 and ORC-07 only; one line `add_subdirectory(bridge)` appended at the end of `engine/tests/oracles/CMakeLists.txt`. No root `CMakeLists.txt` block.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/` or `reference/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A4-R1 to A4-R5, see docs/agents/briefs/A4-implementer.md). You verify them through ORC-06, ORC-07 and A4-T3.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-06 | Bridge damping ✓ | Flexible string on a pure conductance G, every kept mode | σ = −f₀ ln\[(1 − Z₀G)/(1 + Z₀G)\] | 0.1% (run: 10⁻⁴) |
| ORC-07 | Unison coupling ✓ | Two identical strings, one conductance | In-phase 4 f₀ Z₀ G; anti-phase intrinsic | 0.1% |

From the card: **A4-T3** against P0.5, two strings on a mass–spring–damper bridge match partial frequencies within 0.05 cent and decay rates within 2%.

### How to build them

All doctest, one `TEST_CASE` per case, names start with the ID and a space and contain none of `,` `*` `?` `\` `"` `;`. Labels `oracle` and `unit` (A4-T3: `integration`).

**ORC-06.** A flexible string: plain steel with a core so thin that B = π²EI/(TL²) < 10⁻⁸ (state B in the test), lossless (b₁ = b₂ = b₃ = 0), pinned, compiled at 48 kHz, attached (`AtL`) to a `BridgePoint` whose ∥ model is a pure conductance G, θ\_b = 0, G\_h = 0. Z₀ = √(TρA) from your own T and ρA. Two cases: Z₀G = 0.01 and Z₀G = 0.05. Measure the decay rate of **every kept mode**: excite one mode at a time (q\_n(0) = small, others 0) or excite all and separate them, run in float32 (the engine kernel) long enough for a precise fit, and fit σ with `pme_analysis`; or, equivalently, compute σ\_n exactly from the eigenvalues of the coupled one-step linear map if you implement a small eigen-solver in the test (no new dependency). Expected: σ = −f₀ ln[(1 − Z₀G)/(1 + Z₀G)] with f₀ from your own formula. Pass: |σ\_n/σ − 1| ≤ 10⁻³ for every kept mode. Report the worst mode and its error.

**ORC-07.** Two identical flexible strings (as ORC-06, with intrinsic loss b₁ > 0, b₃ = 0 so the intrinsic rate is b₁ for every mode) on one conductance, Z₀G = 0.01. Start them in phase (same initial displacement) and anti-phase (opposite). Expected: in-phase decay 4 f₀ Z₀ G + b₁ (state in the test comment that 4 f₀ Z₀ G is SPEC's small-admittance value; its relative difference from the exact −f₀ ln[(1 − 2Z₀G)/(1 + 2Z₀G)] is (2Z₀G)²/3 ≈ 1.3 × 10⁻⁴ at Z₀G = 0.01, below the tolerance); anti-phase decay b₁. Pass: 0.1% relative for both, for the first 10 modes at least. If SPEC's "4 f₀ Z₀ G" is meant to exclude the intrinsic rate, compare the measured rate minus b₁; say which in the comment.

**A4-T3** (engine/tests/oracles/bridge/, label `integration`, reference part compiled only into optimised builds as A2-T3 does; each case under 60 s in its preset):

- Two plain steel strings with clearly separated partials (for example f₁ ≈ 196 Hz and 262 Hz, B ≈ 10⁻⁴ to 10⁻³), b₁ = 0.5 s⁻¹, **b₂ = 0**, b₃ = 2 × 10⁻⁹ s, pinned, `AtL`, no `detuneH`, on one mass–spring–damper bridge (m, k\_b, r\_b), θ\_b = 0 and G\_h large enough that the horizontal axis is irrelevant, or both polarisations matched in the reference. Choose m, k\_b, r\_b so that the bridge-induced decay of the first partial of each string is at least three times b₁ and Z₀|Y| < 0.1 at every compared partial; state the values and the resulting bridge resonance.
- Engine: `compileString` (48 kHz), `compileBridgeTerms`, the A4 `BridgePoint` with the mass–spring–damper model, the A1 `ModeBank` in float32; both strings released from small triangular displacements at 0.12 L (q\_n(0) from your own projection in float64); observe each string at 0.37 L for 2 s.
- Reference: the same system in P0.5 (`bridge` with `m`, `k_b`, `r_b`, two strings), on a grid fine enough that its own partial-frequency error is below 0.01 cent for the compared partials, or the `refine` extrapolated values (three grids, constant ratio). Report the reference's own error estimate.
- Analysis: `pme_analysis` partial tracking and decay fitting with identical settings on both; compare the first 8 partials of each string (or all below 4 kHz if fewer).
- Pass: |1200 log₂(f\_engine/f\_ref)| ≤ 0.05 cent and |σ\_engine/σ\_ref − 1| ≤ 0.02 for every compared partial. Report per-partial errors.
- Link engine targets (`pme_bridge`, `pme_string`, `pme_osc`, `pme_analysis`) and run the reference CLI from the test (as A2-T3 does), in a test target only. Never make an engine target depend on `reference/`.

**Registry.** Update rows ORC-06 and ORC-07 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/bridge/<file>`. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-06, ORC-07 and A4-T3 exist under those names, pass locally (A4-T3 in an optimised preset) with GCC and Clang, are clang-format and clang-tidy clean, and follow the naming rule. If a test fails against the implementation, do not weaken it: report the measured values; the orchestrator routes the fix to the implementer, or the planner files a spec issue.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A4: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A4`, `role` `test-author`, `model` `sonnet`; `requirements` an empty object; `tests` ORC-06, ORC-07 and A4-T3 (`not run`), with the measured worst errors and the reference's own error estimate in `notes`; `outputs` your files; `pull_requests` empty.
