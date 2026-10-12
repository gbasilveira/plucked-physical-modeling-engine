# Brief: A9 Implicit bridge step and bridge numerics options — code-reviewer

Status: ready once the A9 implementer's and test-author's commits are merged.

Model: sonnet
Branch: review the A9 commits on the branch the orchestrator names (normally `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the second bridge scheme required by decision D-13 (owner answers on SI-0018 and SI-0021): an implicit bridge step with matched coupling, selectable against A4's explicit order; the explicit-mode stability check with its policy; and overdamped lumped bridge modes (SI-0020), each behind an engine setting.

Your job: decide whether the A9 work meets the card, SPEC-BRG "Bridge scheme" and "Stability of the explicit order", SPEC-OSC "Bandwidth", the Software guidelines and the briefs. **Re-derive the energy balance yourself**: the scheme is new numerics and the owner reviews it on the strength of your and the implementer's derivations.

## Read first

- docs/plan/phasing-plan.md: Software guidelines; the phase card **A9**; **Agent briefs and handoff**.
- SPEC: SPEC-BRG (all), SPEC-OSC "Bandwidth", "Discrete energy"; SPEC-EXEC steps 3′ to 5′; SPEC-NL "Explicit low-rank solve"; SPEC-CONV "Quality and numerical options"; SPEC-ORC "Bridge damping".
- docs/spec-issues/SI-0018, SI-0020, SI-0021 (owner decisions, planner's numbers), SI-0019; ICR-0002.
- docs/agents/briefs/A9-implementer.md, A9-test-author.md; docs/dev/bridge.md; `.claude/agents/code-reviewer.md`.

## Inputs

- Diff under review: commits whose messages start `A9: `. CI evidence from the orchestrator; without a link, `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: new test files under engine/tests/bridge/ and engine/tests/oracles/bridge/ named `…a9…`, only to add missing or independent tests for A9-R1 to R6, A9-T1 to T5 or ORC-06 (implicit). Commit with `A9: `; do not push.
- Scratch work in your scratchpad; one build tree at a time; delete scratch builds.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A9-R1 Bridge scheme selector Explicit or Implicit per bridge point (internal enum mirroring ICR-0002 `BridgeScheme`); Explicit stays bit-identical to A4.
2. A9-R2 Implicit scheme exactly as SPEC-BRG "Bridge scheme": matched coupling g̃\_n = g\_n ω\_n/ω̂\_n in the modal force and in a\_n, trapezoidal conductances, averaged bridge-mode force (Fⁿ⁺¹ + 2Fⁿ + Fⁿ⁻¹)/4, affine segment response and one solve of size 1 or 2 per bridge point, including segments whose response comes from a low-rank solve (two right-hand sides, SPEC-EXEC steps 3′ to 5′).
3. A9-R3 Discrete energy of the implicit scheme derived, documented in docs/dev/bridge.md and computed in float64 for tests.
4. A9-R4 Explicit-mode stability check in `configure` (largest generalised eigenvalue against (1 − 10⁻³)·4/Δt²) with the policy SwitchToImplicit (default) or Reject, and a record of switched points for C1's diagnostic.
5. A9-R5 Overdamped lumped bridge modes (σ ≥ Ω > 0) compiled with the real-pole continuation of SPEC-OSC "Bandwidth" under Accept (default); Reject keeps A4's `Rejected`.
6. A9-R6 Cost of both schemes measured on A4's benchmark configuration and reported.

## Tests that must pass

ORC-06 in the implicit scheme (test-author); A9-T1; A9-T2; A9-T3; A9-T4; A9-T5 (test-author), with the card's criteria.

## Review checklist for A9

1. Scope: only engine/src/bridge/**, engine/tests/bridge/** (new files), new `test_a9_*` oracle files, docs/dev/bridge.md, and the ORC-06 location note. A4's existing tests and oracle files are unchanged and still pass. Messages `A9: `.
2. Explicit scheme bit-identical to A4 (check the test and run an A4 test binary from before the A9 commits against the new code).
3. Matched coupling: g̃ uses the undamped ω\_n and the coupled polarisation's compiled ω̂², in **both** the modal force and ã. Check by hand for two modes.
4. Trapezoidal conductances; averaged force (Fⁿ⁺¹ + 2Fⁿ + Fⁿ⁻¹)/4 on bridge modes; the 2 × 2 solve with the SI-0019 rotation; w\_ext after the solve; the SPEC-GEO hook at step n; order 3′ to 5′.
5. Energy: the derivation in docs/dev/bridge.md is correct. Reproduce it on paper or with a symbolic or numeric check of the one-step map. A9-T1 at 10⁻¹⁰.
6. Stability check: power iteration correct, fixed start, threshold (1 − 10⁻³)·4/Δt², policy behaviour, switched-point record. A9-T2 matches SI-0018's table exactly.
7. Overdamped modes: the cancellation-free formula; poles e^((−σ ± μ)Δt) verified independently (mpmath); Accept is the default; Reject keeps `Rejected`; Ω = 0 unchanged.
8. Nonlinear-segment contract (affine pair) documented. Any needed A3 change is reported, not made.
9. ORC-06 implicit: the test-author's one-step map is independent; the criteria are verbatim; per-mode report. A9-T5: A4-T3 setup, reference scope, 0.05 cent and 2%.
10. RT-safety, `noexcept`, no allocation in the per-sample path; float64 solve; fixed order; no fast-math.
11. Benchmark: implicit vs explicit medians reported. clang-format, clang-tidy, ctest (debug GCC and Clang, rt-safety, an optimised preset for A9-T5) and `test_p0_3_r1` pass. Coverage of engine/src at or above 90%. Every requirement and test ID has a named test.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

## Output and report

- Verdict: **approve** or **request changes**, with each blocking item tied to a checklist number and a file and line; non-blocking items separately.
- Return the handoff report as JSON matching docs/agents/report.schema.json, with:
  - `phase` `A9`, `role` `code-reviewer`, `model` `sonnet`;
  - `requirements` A9-R1 to A9-R6 as verified;
  - `tests` with `ci_run` or `not run`;
  - `outputs` the tests you added.

  Start `notes` with `Verdict: …` and include your energy re-derivation in a few lines: the orchestrator forwards it to the owner's numerics review.
