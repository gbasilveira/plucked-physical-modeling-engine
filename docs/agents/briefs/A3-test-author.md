# Brief: A3 SAV framework and low-rank solve — test-author

Status: **ready once A1 is complete and the A3 implementer's commits are merged** (A1 merged at 1512985, in review).

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the generic nonlinear update used by hammers, plucks, dampers, barriers and tension modulation.

Your job: write the SPEC-ORC oracle test the A3 card names, **ORC-08 with a rigid-wall test potential**, independently of the A3 implementer, deriving the expected values from the SPEC-HAM closed form, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase card **A3 SAV framework and low-rank solve**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-NL — docs/plan/engine-specification.md, "Nonlinear framework"
  - SPEC-HAM — "Hammer and action", "Oracle" (contact time t\_c and rebound)
  - SPEC-ORC — "Verification oracles", row "Impact time"
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md ("Oracle registry", tolerance macros); docs/dev/signal-analysis.md (sub-sample contact-time measurement).
- The A3 internal headers under `engine/src/nonlinear/include/pme/nonlinear/` (`SavSegment`, `Potential`, `savStep`, `savEnergy`) and docs/dev/nonlinear.md. Read headers only, never `engine/src/nonlinear/*.cpp`.

## Inputs

- Completed dependencies and their outputs: A1 (`pme_osc`), the A3 implementer's commits (`git log --grep '^A3: '`), G0 (P0.3 `pme_test_framework`, `pme_analysis`).
- Frozen interfaces: interfaces-v2 = 300b995 (no public SAV API; you use the internal headers).

## Concurrent work

The ownership table in docs/agents/briefs/A3-implementer.md ("Concurrent work") applies to you. Your rows: `engine/tests/oracles/nonlinear/**` (new); in `engine/tests/oracles.md` the status and test-location cells of row ORC-08 only; one line `add_subdirectory(nonlinear)` appended at the end of `engine/tests/oracles/CMakeLists.txt` (the A2 test-author appends `add_subdirectory(string)`; the orchestrator keeps both). No root `CMakeLists.txt` block.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A3-R1 to A3-R5, see docs/agents/briefs/A3-implementer.md). You verify them through ORC-08.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-08 | Impact time ✓ | Mass 8 g at 3 m/s on rigid felt contact, p = 2.5 | t\_c from the hammer oracle; rebound −v | max(1%, 1 sample); rebound 0.1% (run: 0.4%, exact rebound) |

From the card: ORC-08 **with a rigid-wall test potential** (A5 repeats it later with the hammer potential).

### How to build it

Doctest in `engine/tests/oracles/nonlinear/`, labels `oracle` and `unit`, one `TEST_CASE` per (K, fs, precision) case, names starting `ORC-08` without commas.

- One lumped coordinate z (the mass), m = 8 g, free (ω̂ = σ̂ = 0, so β = 1, κ = 0, γ = Δt²), no modes, no dashpot. Your own `Potential` in the test: V(z) = K/(p+1) [z − z\_w]₊^(p+1) with p = 2.5, ∇V = K [z − z\_w]₊^p; the mass starts 1 mm before the wall moving towards it at 3 m/s.
- K: at least two values from SPEC-HAM's typical range, chosen so t\_c ≈ 1 ms and ≈ 4 ms; state them. Expected t\_c = 2(δ\_m/v)·√π Γ(1 + 1/(p+1)) / Γ(½ + 1/(p+1)), δ\_m = ((p+1) m v² / (2K))^(1/(p+1)) (SPEC-HAM "Oracle"), computed in the test in float64 (`std::tgamma`).
- fs = 48 kHz (the SPEC run) and 96 kHz; the float32 engine instantiation, and the float64 instantiation as a second case.
- Measure the contact time as the duration with V > 0, refined to sub-sample precision by interpolating the crossings of z = z\_w (or with `pme_analysis`'s contact-time measurement), and the rebound speed from the half-step velocity after separation.
- Pass: |t̂\_c − t\_c| ≤ max(0.01 t\_c, 1/fs); |v̂\_out / (−v) − 1| ≤ 0.001. Report the measured errors (SPEC's check reached 0.4% on t\_c and an exact rebound).
- Also record (not a pass criterion) the energy drift during contact and that the reset counter increments exactly once after separation.

**Registry.** Update row ORC-08 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/nonlinear/<file>`. Note in the location cell "rigid wall (A3); hammer potential (A5) pending" so A5 knows to add its case. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-08 exists under that name, passes locally in `linux-debug` and `linux-rt-safety` with GCC and Clang, is clang-format and clang-tidy clean, and follows the naming rule (`uv run pytest tools/tests/test_p0_3_r1.py`). If it fails against the implementation, do not weaken it: report the measured values; the orchestrator routes the fix to the implementer, or the planner files a spec issue.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A3: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A3`, `role` `test-author`, `model` `sonnet`; `requirements` an empty object; `tests` ORC-08 (`not run`), with the measured t\_c and rebound errors per case in `notes`; `outputs` your files; `pull_requests` empty.
