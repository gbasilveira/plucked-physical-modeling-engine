# Brief: A6 Dampers and pedals — code-reviewer

Status: ready once the A6 implementer's and test-author's commits are merged (A6 itself is **ready (pending CI on A2/A3/B1)**).

Model: sonnet
Branch: review the A6 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

damper felt models and the pedal and key logic that drives them.

Your job: decide whether the A6 implementer's and test-author's work meets the A6 card, SPEC-DMP, the Software guidelines and the briefs, and return a verdict.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **A6 Dampers and pedals**; **Agent briefs and handoff**.
- SPEC sections: SPEC-DMP — docs/plan/engine-specification.md, "Dampers and pedals" (with the SI-0016 sostenuto rule); SPEC-OSC — "Time-varying parameters", "Bandwidth"; SPEC-NL — "Discrete scheme", "Explicit low-rank solve"; SPEC-EXEC — per-sample step 10.
- docs/agents/briefs/A6-implementer.md (including its ownership table and shared-file plan) and docs/agents/briefs/A6-test-author.md.
- docs/spec-issues/SI-0016; docs/adr/ADR-0006; docs/dev/osc.md, docs/dev/string.md, docs/dev/nonlinear.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A6: ` (`git log --grep '^A6: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: test sources under `engine/tests/dampers/` and `engine/tests/oracles/dampers/` — only to add missing or independent tests for A6-T1 to T4, ORC-13 or A6-R1 to R5. Commit with messages starting `A6: `; list them in your report. Do not push. Test names start with an ID and a space and contain none of `,` `*` `?` `\` `"` `;`.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A6-R1 Diagonal damper model with ramped coefficient refresh.
2. A6-R2 Rank-one damper as one spring–dashpot pair in the low-rank solve.
3. A6-R3 Lift fraction from key and sustain pedal, half-pedal region, η\_d shaping.
4. A6-R4 τ\_up and τ\_down timing with release velocity.
5. A6-R5 Sostenuto latch and per-key hasDamper flags.

## Tests that must pass

- ORC-13 (A6 test-author)
- **A6-T1** the rank-one damper makes energy decrease monotonically;
- **A6-T2** table-driven tests of key, sustain and sostenuto sequences against expected lift states;
- **A6-T3** damping increases monotonically as pedal depth decreases through the half-pedal region;
- **A6-T4** engagement ramps produce no output step larger than the largest sample-to-sample change of the undamped signal.

## Review checklist for A6

**Scope and process**

1. A6 commits touch only the A6 rows of the ownership table, the root `CMakeLists.txt` block `# ---- A6: … ----`, one line in `engine/tests/oracles/CMakeLists.txt` and the placeholder sentence of docs/dev/interfaces.md. `git diff <base> -- engine/include engine/src/osc engine/src/string engine/src/nonlinear` is empty. Messages start `A6: `.

**Damper models (A6-R1, A6-R2)**

2. Diagonal: σ\_n,d and ω²\_n,d exactly as SPEC-DMP, vertical polarisation only, φ̄\_d from the raised-cosine window at x\_d = xRatio·L in float64; coefficients via `compileOscillatorF64`; changes above 0.1% per sub-block ramped linearly with A1's ramp functions; state kept. Independent check with mpmath: σ\_n,d, ω\_n,d for three modes of one string at e = 0.4.
3. The overdamping check is reported (largest σ\_n,d/ω\_n,d for generic-grand-88 at e = 1); no silent clamping or mode dropping.
4. Rank-one: the Dashpot R = e(R\_d + K\_d Δt) plus the explicit force −e K\_d **c**(**c**ᵀ**z**ⁿ⁻¹) reproduces SPEC's pair **h** = γ⊙e**c**/m, **w** = **c**(K\_d/2 + R\_d/(2Δt)); verify the algebra yourself and that the A6-R2 test compares against a direct `lowRankSolve` of SPEC's pair. No edit to `engine/src/nonlinear/`.

**Pedal logic (A6-R3 to A6-R5)**

5. l = max(l\_key, l\_ped), l\_ped clamp, e\_target = (1 − l)^η\_d with P₀, P₁ from Defaults; τ\_up when lifting, τ\_down = τ₀/(1 + k\_r ν\_rel) when landing; exact one-pole discretisation; SI-0016 thresholds (0.5 for the pedal and for "lifted by its key"); latched keys ignore key events until release; hasDamper = false gives e = 0.
6. A6-T2's expected states are hand-written from SPEC, not computed by the code under test; add at least three sequences of your own (for example sustain and sostenuto interleaved with KeyDepth).

**Tests**

7. A6-T1: constant e, float64, energy including E\_d, monotone at every step over 2 s.
8. A6-T3: monotone in P, strictly inside [P₀, P₁], constant outside.
9. A6-T4: D measured on the undamped string over the same window; diagonal and rank-one, landing and lifting, ν\_rel = 0 and 1; any margin stated and justified.
10. ORC-13: SPEC setup and tolerance verbatim, φ̄ by independent quadrature, expected σ from SPEC formulas; oracles.md row: only status and test location changed.

**Real time and quality**

11. Per-sample and per-sub-block paths `noexcept`, no allocation or locks (RT-safety test present and passing); per-key state preallocated.
12. `clang-format --dry-run --Werror`, `clang-tidy -p build/linux-debug` clean; tests pass in `linux-debug` (GCC and Clang) and the rt-safety preset; `test_p0_3_r1` passes; coverage keeps engine/src at or above 90%; the benchmark is registered with `perf`; docs/dev/dampers.md names SPEC-DMP and documents the API, the rank-one derivation and the per-sample/per-sub-block split.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A6 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A6`, `role` `code-reviewer`, `model` `sonnet`; `requirements` A6-R1 to A6-R5 as verified; `tests` A6-T1 to A6-T4 and ORC-13 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
