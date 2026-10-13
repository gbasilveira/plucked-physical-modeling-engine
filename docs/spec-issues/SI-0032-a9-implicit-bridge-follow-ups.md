# SI-0032 Implicit bridge (A9): w_ext in the solve, horizontal matched coupling, nonlinear segments, stability-check scope

Status: resolved for items 1, 2 and 4 (2026-10-13, planner; flagged for the owner's review with A9's numerics); item 3 planned as phase A11 · Raised by: A9, implementer · Date: 2026-10-13
SPEC section: SPEC-EXEC "Per-sample loop" step 4′; SPEC-BRG "Bridge scheme" (matched coupling) and "Stability of the explicit order"

## Failing test
None. Four points from the A9 implementation (branch worktree-agent-a7b81483899fb757e, docs/dev/bridge.md), plus two tolerance overruns recorded at the end for the owner.

## Evidence and resolution

**1. Where w\_ext enters the implicit solve (SPEC-EXEC 4′).** SPEC said "solves for wⁿ⁺¹, adds w\_ext". A9 puts w\_extⁿ⁺¹ in the known part w̄ of the solve, so the segments' Fⁿ⁺¹ (and the trapezoidal conductance and averaged bridge-mode terms that use it) see the total motion w\_local + w\_ext. This is what step 3 of the explicit order already does (δ\_tt w and the next F use the total w), and it keeps the trapezoid consistent; adding w\_ext after the solve would leave Fⁿ⁺¹ computed from a w that the strings never see. The two readings are identical when w\_ext = 0. **Ruling:** A9's reading. SPEC-EXEC 4′ reworded (2026-10-13).

**2. Matched coupling of the horizontal polarisation.** g̃\_n = g\_n ω\_n/ω̂\_n needs the undamped ω\_n of the coupled polarisation. `StringModes` (public, engine/include/pme/string.hpp) stores only the vertical `omega`; the horizontal one is `horizontalOmega(omega, detuneH)` inside engine/src/string, and `detuneH` is not in `StringModes`. A9 uses the vertical ω over the horizontal ω̂, as its brief says. That is exact at detuneH = 0. Otherwise g̃\_H is off by the ratio ω\_V/ω\_H, about 5.8·10⁻⁴ relative per cent of detune. Because the same g̃ appears in the modal force and in ã, the scheme stays symmetric and its energy structure is unaffected; only the frequency matching of horizontal modes degrades by that order. **Ruling:** yes, StringModes needs the compiled horizontal frequency. Proposed as an amendment to ICR-0002 (compatible new member `std::vector<double> omegaH`, filled by `compileString`; see ICR-0002 "Amendment proposed 2026-10-13"), pending the orchestrator's approval. Until v3 is applied, A9 keeps the vertical ω (known limit, documented in docs/dev/bridge.md); after v3, A9's follow-up switches to `omegaH` and adds a test at detuneH ≠ 0.

**3. Nonlinear segments on an implicit point.** The point needs per polarisation the affine pair (c, κ) of each segment's Fⁿ⁺¹ = c − κ wⁿ⁺¹. A segment with a low-rank solve obtains it from two solves with the same implicit matrix: one for its right-hand side **a**, one for the bridge column γ ⊙ g̃/Δt² (SPEC-BRG already states this). A3's `savStep` returns one solve. A9 tested the contract with a Sherman–Morrison stand-in against a dense float64 solve (docs/dev/bridge.md "Contract for segments with an active nonlinearity", A9 branch) and did not edit engine/src/nonlinear. **Ruling:** planned as phase **A11** (Nonlinear segments on implicit bridge points): A3's solve exposes the second right-hand side; A5 (hammer), A6 (damper rank-one) and A7 (tension modulation) segments supply (c, κ) to A9's point. A11 is required by G1, because `numerics.unstableBridge` = SwitchToImplicit (the default) can put a hammered note on an implicit point even in Realtime; C1 depends on G1 and so on A11.

**4. Scope of the explicit-mode stability check.** A9-R4 estimates the largest generalised eigenvalue of the (K, M) pencil of strings and bridge modes, as SPEC-BRG states. The first-order conductances G, G\_h and G\_hf (explicit update w\_R += Δt G F) are not in that pencil, so a point whose conductance alone makes the explicit order unstable is not flagged. **Ruling:** recorded as a known limit of the check (SPEC-BRG unchanged for now). Physical conductances are small (Z₀G ≤ 0.05 in the ORC-06 cases); the planner expects the conductance limit far above that, but has not derived it. If a body import (B3, B4) produces a large G, the check is extended in a follow-up.

## Tolerance overruns pending the owner's acceptance (A9 tests)
Recorded, not ruled on by the planner; the owner reviews them with A9's numerics before A9 merges.

| Test | Case | Measured | Bound | Implementer's attribution |
| --- | --- | --- | --- | --- |
| A9-T2 | m = 10⁻⁶ kg, 8 strings, implicit, energy drift | 6.5·10⁻¹⁰ | 10⁻¹⁰ | slow linear drift ≈ 1.6·10⁻¹⁵ per sample: float64 round-off amplified by the light bridge's conditioning |
| A9-T3 | σ = Ω (critically damped, double pole), free response vs exact | 2.7·10⁻¹² | 10⁻¹² | round-off at the double pole of the real-pole continuation |

## Affected phases
A9 (items 1, 2, 4: documentation only; overruns: owner review); ICR-0002 (item 2 amendment); A11 (new, item 3); C1 (via G1).
