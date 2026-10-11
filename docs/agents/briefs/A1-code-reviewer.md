# Brief: A1 Modal oscillator — code-reviewer

Model: sonnet
Branch: review the A1 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the discrete oscillator every mode in the engine uses, with float64 coefficient compilation and a structure-of-arrays mode bank.

Your job: decide whether the A1 implementer's and test-author's work meets the A1 card, SPEC-OSC, the Software guidelines and the briefs, and return a verdict. Every mode in the engine runs through this code, so check the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **A1 Modal oscillator**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-OSC — docs/plan/engine-specification.md, "The modal oscillator"
  - SPEC-CONV — docs/plan/engine-specification.md, "Scope, conventions and notation"
- docs/agents/briefs/A1-implementer.md (including its ownership table) and docs/agents/briefs/A1-test-author.md.
- docs/spec-issues/SI-0008-oscillator-coefficient-cancellation.md; docs/adr/ADR-0006-interface-status-returns.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A1: ` (`git log --grep '^A1: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 (ICR-0001); osc.hpp unchanged since interfaces-v1 (2daee85).

## Scope

- May edit: test sources under `engine/tests/osc/` and `engine/tests/oracles/osc/` — only to add missing or independent tests for A1-T1 to T3, ORC-01, ORC-02 or A1-R1 to R6. Commit with messages starting `A1: `; list them in your report. Do not push.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A1-R1 `compileOscillator` exactly as SPEC-OSC: σ̂Δt = tanh(σΔt) and the cancellation-free ω̂²Δt² formula, in float64.
2. A1-R2 Bandwidth rule: reject modes with ω\_dΔt > 0.95π, above f\_max, or overdamped.
3. A1-R3 `ModeBank`: 64-byte-aligned arrays q, u, β, κ, γ (float32) and ω̂², σ̂ (float64), with a scalar difference-form update taking an explicit force vector.
4. A1-R4 Derived quantities: half-step velocity, centred velocity, centred acceleration.
5. A1-R5 Discrete energy and energy balance in float64.
6. A1-R6 Coefficient ramp utility implementing the 0.1%-per-sub-block rule.

## Tests that must pass

- ORC-01; ORC-02 (A1 test-author)
- **A1-T1** coefficients agree with a 50-digit reference within 10⁻¹⁴ relative for ωΔt from 10⁻⁴ to 0.95π and σΔt from 0 to 0.1;
- **A1-T2** bandwidth rejections at 44.1, 48 and 96 kHz;
- **A1-T3** a 1% frequency ramp over one sub-block tracks the analytic chirp phase within 0.1%.

## Review checklist for A1

**Scope and process**

1. A1 commits touch only the A1 implementer's and test-author's paths in the ownership table, two root `CMakeLists.txt` blocks (`# ---- A1: … ----`, `# ---- A1 (test-author): … ----`), the one SUBCASE of `engine/tests/interfaces/test_stubs.cpp` and the one sentence of docs/dev/interfaces.md. No public header changed (`git diff <base> -- engine/include` is empty). Messages start `A1: `.

**A1-R1 and A1-T1**

2. The ω̂²Δt² evaluation is the SI-0008 form; ω\_d uses √((ω − σ)(ω + σ)); β, κ, γ follow SPEC-OSC; float32 fields are the round-to-nearest of the float64 values. Comments cite `SPEC-OSC` and SI-0008.
3. Independent check: with `uv run --no-project --with mpmath python`, compute 50-digit values for 20 (ω, σ, fs) triples **not in the committed table** (include fs = 44 100 and 96 000, σ/ω near 0.9, ωΔt near 10⁻⁴ and near 0.95π) and compare with the implementation (a small C++ driver in scratch, or a committed test). All within 10⁻¹⁴ relative.
4. The committed reference table was produced by the committed script: rerun it and diff (it must be reproducible with the mpmath version it names). The grid covers the card's domain (ωΔt 10⁻⁴ to 0.95π; σΔt 0 to 0.1) with rejected pairs tested for `Rejected` and unchanged `out`.
5. `InvalidArgument` for fs ≤ 0, negative or non-finite inputs; no exceptions, no allocation.

**A1-R2 and A1-T2**

6. Boundaries at 44.1, 48 and 96 kHz: ω\_dΔt = 0.95π just inside/outside; σ = ω rejected; f\_max via the internal function; `compileOscillator` agrees with it at f\_max = +∞.

**A1-R3 to A1-R5**

7. Arrays 64-byte aligned and padded; padding lanes stay zero; the kernel computes exactly uⁿ⁺¹ = βuⁿ − κqⁿ + γfⁿ, qⁿ⁺¹ = qⁿ + uⁿ⁺¹ in that order, stores no qⁿ⁻¹, is `noexcept`, allocation-free, and is tested under `ScopedRealtimeThread` with the `rt` label (passes in `linux-rt-safety`).
8. Energy and balance in float64 per SPEC-OSC "Discrete energy"; bank sums in fixed index order.

**A1-R6 and A1-T3**

9. The ramp rule matches the brief (|Δω| > 10⁻³ω or |Δσ| > 10⁻³σ ramps; linear over S samples; ends exactly on target; state kept). A1-T3 compares against an analytic linear chirp and passes at 0.1% of the ramp's phase advance for S = 16 and 32. Rerun it and record the errors.

**Oracles (test-author)**

10. ORC-01 and ORC-02 use SPEC's setups and tolerances verbatim, derive expected values from SPEC (not from implementation output), run the engine's float32 kernel for ORC-01 and the float32 case of ORC-02, include an independent float64 recursion for ORC-02, and pass tolerances explicitly (not the golden default 10⁻⁹). engine/tests/oracles.md rows ORC-01 and ORC-02: only status and test location changed.

**Quality**

11. `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` clean; all tests pass in `linux-debug` and `linux-rt-safety` with GCC and Clang; `test_p0_3_r1` passes; coverage of `engine/src/osc` (run `linux-coverage` locally or read the CI coverage report) keeps engine/src at or above 90%.
12. docs/dev/osc.md names SPEC-OSC and SPEC-CONV, documents SI-0008, the f\_max interpretation, the ramp rule and the internal API. The benchmark is registered with `perf`.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A1 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist. SI-0008 is resolved; SPEC-OSC prints the form the code must use.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A1`, `role` `code-reviewer`, `model` `sonnet`; `requirements` A1-R1 to A1-R6 as verified; `tests` A1-T1 to A1-T3, ORC-01, ORC-02 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
