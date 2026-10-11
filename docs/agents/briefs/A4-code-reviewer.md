# Brief: A4 Bridge coupling — code-reviewer

Status: ready once the A4 implementer's and test-author's commits are merged (A4 itself is **ready (pending CI on A2/A3/B1)**).

Model: sonnet
Branch: review the A4 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

strings on a moving bridge, with pluggable bridge models, unison sharing and polarisation rotation.

Your job: decide whether the A4 implementer's and test-author's work meets the A4 card, SPEC-BRG, the Software guidelines and the briefs, and return a verdict. Every note group in every instrument couples through this code, so recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **A4 Bridge coupling**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-BRG — docs/plan/engine-specification.md, "Bridge coupling"
  - SPEC-CONV — "Scope, conventions and notation" (e^(+iωt), SI-0012)
  - SPEC-OSC — "Difference form", "Discrete energy"
  - SPEC-BODY — "Local bridge model", "Sympathetic path"; SPEC-EXEC — per-sample steps 2 to 5; SPEC-GEO — "Transmission to the body"
- docs/agents/briefs/A4-implementer.md (including its ownership table and shared-file plan) and docs/agents/briefs/A4-test-author.md.
- docs/spec-issues/SI-0010, SI-0012; docs/adr/ADR-0006; docs/dev/osc.md, docs/dev/string.md, docs/dev/reference-solver.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A4: ` (`git log --grep '^A4: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: test sources under `engine/tests/bridge/` and `engine/tests/oracles/bridge/` — only to add missing or independent tests for A4-T1 to T3, ORC-06, ORC-07 or A4-R1 to R5. Commit with messages starting `A4: `; list them in your report. Do not push. Test names start with an ID and a space and contain none of `,` `*` `?` `\` `"` `;`.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A4-R1 `compileBridgeTerms` for both bridge ends; `bridgeForce` as one dot product plus the T/L term.
2. A4-R2 `BridgePoint` component implementing the SPEC-BRG coupling order, with pluggable models: conductance, mass–spring–damper, and the modal-set interface used by B3.
3. A4-R3 Unison sharing of one bridge point by several segments.
4. A4-R4 Polarisation rotation θ\_b and horizontal conductance G\_h.
5. A4-R5 Hooks for w\_ext (sympathetic input) and for κ\_L, κ\_T contributions (wired in A7).

## Tests that must pass

- ORC-06; ORC-07 (A4 test-author)
- **A4-T1** strings plus a lossless mass–spring bridge show no secular energy growth over 10⁶ samples and relative deviation below 10⁻⁶;
- **A4-T2** a mass-like bridge shifts partials as Δω ≈ −2 f₀ Z₀ Im Y within 2% for Z₀|Y| < 0.01;
- **A4-T3** against P0.5, two strings on a mass–spring–damper bridge match partial frequencies within 0.05 cent and decay rates within 2% (A4 test-author).

## Review checklist for A4

**Scope and process**

1. A4 commits touch only the A4 rows of the ownership table, the root `CMakeLists.txt` block `# ---- A4: … ----`, one line in `engine/tests/oracles/CMakeLists.txt`, the `compileBridgeTerms` SUBCASE of `engine/tests/interfaces/test_stubs.cpp` and the stub sentence of docs/dev/interfaces.md. `git diff <base> -- engine/include engine/src/osc engine/src/string engine/src/nonlinear` is empty. Messages start `A4: `.

**Terms and force (A4-R1)**

2. g\_n for `AtL` and `At0` match SPEC-BRG including the (−1)^(n+1) sign; a\_n uses the discrete `oscV[n].omegaHat2`, and the horizontal force uses `oscH[n].omegaHat2` (not the vertical ω̂²). Independent check with `uv run --no-project --with mpmath python`: g\_n, a\_n and T/L for n = 1, 2, N of two strings at 30 digits against the compiled float32 values (correct rounding).
3. `bridgeForce` = Σ a\_n q\_n − (T/L) w\_b, one fixed-order dot product, `noexcept`, no allocation; the static downbearing load is not included.

**Coupling order and models (A4-R2 to A4-R4)**

4. Per sample: forces summed in segment order → rotation to the bridge frame → model advance with Fⁿ (explicit) → integrated w\_ext added → rotation back → δ\_tt wⁿ = (wⁿ⁺¹ − 2wⁿ + wⁿ⁻¹)/Δt² → segments apply −g\_n δ\_tt wⁿ. Nothing in the per-sample path allocates, locks or throws (RT-safety test present and passing).
5. Conductance w\_Rⁿ⁺¹ = w\_Rⁿ + Δt G Fⁿ; mass–spring–damper as one mode with Ω = √(k\_b/m), σ = r\_b/(2m), Φ = 1/√m (k\_b = 0 handled and documented); modal set built from `pme::LocalBridge` with coefficients at the engine's fs; ⊥ axis a pure conductance G\_h; R(θ\_b) as SPEC-BRG writes it, θ\_b = 0 bit-identical to independent axes.
6. Several `AtL` and `At0` segments share one w\_b; tests that use `applyMute` pass `params.detuneH` as its 4th argument (carried A2 follow-up).
7. Hooks: w\_ext integrates v\_ext with Δt on the ∥ axis; the extra vertical force enters F\_b,v before the model advance (SPEC-EXEC step 2). Both inert by default (bit-identical to no hook).

**Tests**

8. A4-T1: 10⁶ samples, double states, the energy expression documented and justified for the partitioned scheme; deviation below 10⁻⁶ and no secular trend; both θ\_b cases. If the implementer reports an instability, reproduce it and include the parameters in your report.
9. A4-T2: Y = 1/(iωm) under e^(+iωt), so the predicted shift is positive; Z₀|Y| < 0.01 at every compared partial; the estimator's own error checked; 2% per partial.
10. ORC-06, ORC-07: SPEC setups and tolerances verbatim, every kept mode for ORC-06, expected values computed independently (own T, ρA, Z₀, f₀); oracles.md rows: only status and test location changed.
11. A4-T3: b₂ = 0, no `detuneH`, no `At0` (reference scope), separated partials, bridge decay ≥ 3 b₁, the reference's own error below 0.01 cent or `refine` extrapolated values used, same `pme_analysis` settings on both sides, 0.05 cent and 2% per partial; no engine target depends on `reference/`.

**Quality**

12. `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` clean; all tests pass in `linux-debug` with GCC and Clang and in the rt-safety preset; `test_p0_3_r1` passes; coverage of `engine/src/bridge` keeps engine/src at or above 90%; the benchmark is registered with `perf`.
13. docs/dev/bridge.md names SPEC-BRG (and the SPEC-BODY, SPEC-EXEC, SPEC-GEO parts), documents the internal API, the model kinds, the hooks, the A4-T1 energy and the time convention.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A4 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A4`, `role` `code-reviewer`, `model` `sonnet`; `requirements` A4-R1 to A4-R5 as verified; `tests` A4-T1 to A4-T3, ORC-06, ORC-07 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
