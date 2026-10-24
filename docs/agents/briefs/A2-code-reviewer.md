# Brief: A2 Stiff string compiler — code-reviewer

Status: ready once the A2 implementer's and test-author's commits are merged (A2 itself waits for A1 to be complete).

Model: sonnet
Branch: review the A2 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

turns string geometry, materials, tuning and losses into a mode table, oscillator coefficients and shape vectors.

Your job: decide whether the A2 implementer's and test-author's work meets the A2 card, SPEC-STR, the Software guidelines and the briefs, and return a verdict. Every string in every instrument is compiled here, so recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **A2 Stiff string compiler**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-STR — docs/plan/engine-specification.md, "Stiff string model"
  - SPEC-OSC — "The modal oscillator" ("Bandwidth")
- docs/agents/briefs/A2-implementer.md (including its ownership table) and docs/agents/briefs/A2-test-author.md.
- docs/spec-issues/SI-0001, SI-0004, SI-0010 (open); docs/adr/ADR-0006; docs/dev/osc.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A2: ` (`git log --grep '^A2: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: test sources under `engine/tests/string/` and `engine/tests/oracles/string/` — only to add missing or independent tests for A2-T1 to T3, ORC-03 to ORC-05 or A2-R1 to R7. Commit with messages starting `A2: `; list them in your report. Do not push.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A2-R1 Wound-string ρA and EI formulas, including packing factor and ε\_w.
2. A2-R2 Closed-form tension for pitch; tuning curve = equal temperament plus stretch plus unison detune.
3. A2-R3 Pinned frequencies, and clamped ends with Fletcher's correction.
4. A2-R4 Losses b₁, b₂, b₃ with an optional per-partial override table.
5. A2-R5 Truncation by the bandwidth rule; two polarisations with horizontal detune.
6. A2-R6 Shape vectors φ̄ for point, rectangular and raised-cosine windows at registered points, computed in float64 and stored float32.
7. A2-R7 Rear and duplex segments (BridgeEnd At0) with mute ratio.

## Tests that must pass

- ORC-03; ORC-04; ORC-05 (A2 test-author)
- **A2-T1** wound-string mass and stiffness unit tests;
- **A2-T2** mode counts match the bandwidth rule at 44.1, 48 and 96 kHz;
- **A2-T3** against P0.5, a freely vibrating stiff string matches partial frequencies within 0.05 cent and decay rates within 1% (A2 test-author; needs P0.5 merged).

## Review checklist for A2

**Scope and process**

1. A2 commits touch only the A2 implementer's and test-author's rows of the ownership table, the root `CMakeLists.txt` block `# ---- A2: … ----`, one line in `engine/tests/oracles/CMakeLists.txt`, the `compileString` SUBCASE of `engine/tests/interfaces/test_stubs.cpp` and the one stub sentence of docs/dev/interfaces.md. `git diff <base> -- engine/include engine/src/osc` is empty. Messages start `A2: `.

**Formulas (A2-R1 to A2-R4)**

2. ρA, EI, T, B, k\_n, ω\_n, σ\_n follow SPEC-STR exactly, in float64, each with a `// SPEC-STR …` comment; plain strings ignore the winding; Fletcher's factor multiplies every clamped ω\_n; the override rule matches the brief.
3. Independent check: with `uv run --no-project --with mpmath python` compute, at 30 digits, T, f₀, B and ω\_n for n = 1, 10, N of three strings not used by the tests (one wound, one with B ≈ 10⁻², one with `sigmaOverride`), and compare with the compiled tables through a scratch driver. ω within 10⁻¹² relative, σ within 10⁻¹².
4. `tuningF1` and `f1ForTension` match the SPEC-STR formulas (SI-0004) and invert each other.

**Truncation and polarisations (A2-R5, A2-T2)**

5. N is the largest n with every mode 1 … n passing `pme::osc::passesBandwidthRule` for both polarisations (the A1 function, not a copy); `oscH` uses ω\_n·2^(Δ\_h/1200) with the same σ\_n; `k`, `omega`, `sigma`, `oscV`, `oscH` have length N. A2-T2 computes its expected N independently of `modeCount`.

**Shapes (A2-R6, ORC-05)**

6. `phiBar` holds point values φ\_n(x₀) (W = 1) in registered-point order, float64 then rounded; exact zeros at x₀ = 0 and L. `windowFactor` handles u → 0 and u → π (raised cosine, W(π) = 1/2): probe u = π ± 10⁻¹², ± 10⁻⁸, ± 10⁻⁴ and u = 10⁻¹⁰ against mpmath; relative error ≤ 10⁻¹². `windowedShapes` computes in float64 from L, k\_n, x₀, not from the float32 table.

**Rear segments (A2-R7)**

7. At0 segments compile through the same path; `applyMute` takes σ\_felt as an argument with no default (SI-0010 item 2 is open) and validates m\_d ∈ [0, 1]; m\_d = 0 is bit-identical to no mute.

**Status and robustness**

8. Every invalid input listed in the implementer brief returns `InvalidArgument` with `out` unchanged; mode 1 failing the bandwidth rule returns `Rejected`; no exceptions escape (`std::vector` allocation failure aside).

**Oracles and comparison (test-author)**

9. ORC-03 to ORC-05 use SPEC's setups and tolerances verbatim, compute expected values independently (own ρA, EI, T; own quadrature), include the u = π case at n = 40 of (1 m, 50 mm), and pass tolerances explicitly. engine/tests/oracles.md rows ORC-03 to ORC-05: only status and test location changed.
10. A2-T3 (when P0.5 is merged): b₂ = 0, the reference grid's own error estimate is below 0.01 cent, both signals analysed with the same `pme_analysis` settings, 0.05 cent and 1% applied per partial; the test target links engine and reference without making any engine target depend on `reference/`.

**Quality**

11. `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` clean; all tests pass in `linux-debug` with GCC and Clang; `test_p0_3_r1` passes; coverage of `engine/src/string` keeps engine/src at or above 90%.
12. docs/dev/string.md names SPEC-STR (and SPEC-OSC for the bandwidth rule), documents the internal API, the ω\_n evaluation form, the override and truncation rules, SI-0010's status and N per A2-T2 case. The benchmark is registered with `perf`.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A2 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist. If P0.5 is not merged when you review, A2-T3 is `not run` and A2 cannot be marked complete; say so in the verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A2`, `role` `code-reviewer`, `model` `sonnet`; `requirements` A2-R1 to A2-R7 as verified; `tests` A2-T1 to A2-T3, ORC-03 to ORC-05 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
