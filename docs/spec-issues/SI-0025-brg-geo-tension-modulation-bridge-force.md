# SI-0025 The bridge force omits the transverse component of the modulated tension

Status: open (planner recommendation below; physics, needs owner review before SPEC changes) · Raised by: A7, implementer · Date: 2026-10-12
SPEC section: SPEC-BRG "Bridge force"; SPEC-GEO "Tension modulation (Kirchhoff–Carrier)", "Transmission to the body", "Energy balance of the default tier"

## Failing test
No card test fails yet; found by A7's diagnostic energy test. With tension modulation on a movable bridge (mass–spring bridge, 0.3 kg, 300 Hz; 5 mm pluck), the transverse energy (modes, bridge, ψ including V\_KC), which SPEC-GEO says "obeys the usual balance", drifts by 3.7 × 10⁻³ relative over 2 × 10⁴ samples. Adding −ΔT · y\_x(L) to the vertical bridge force (in a test only) lowers the drift to 5.2 × 10⁻⁵. On a rigid bridge the balance holds to 2 × 10⁻¹³.

## Evidence
SPEC-BRG's bridge force F\_b = ρA Σ ω̂ₙ² gₙ qₙ − (T/L) w\_b contains only the static tension T. Its first term is the reciprocal of the inertial coupling −gₙ ẅ\_b in the modal equation, with the mode's stiffness ρA ωₙ². Tension modulation adds ΔT kₙ² to each mode's stiffness (∂V\_KC/∂qₙ = ΔT kₙ² qₙ), so the reciprocal force must gain ΔT Σ kₙ² gₙ qₙ. For ψ = x/L, kₙ² gₙ = √(2/L) (−1)ⁿ⁺¹ kₙ = −φₙ′(L), so this is −ΔT y\_x,modal(L). Likewise the support term −(T/L) w\_b gains −(ΔT/L) w\_b, which corresponds to the support-motion part w\_b²/L of S = ∫ y\_x² dx that SPEC's S = Σ kₙ² qₙ² leaves out (the cross term vanishes because ∫ φₙ′ dx = 0). Together:

  F\_b += −ΔT · y\_x(L), with y\_x(L) the full slope at the bridge (modes plus the lifting slope w\_b/L),

and for a bridge at x = 0 (ψ = 1 − x/L) the same derivation gives F\_b += +ΔT · y\_x(0). Each polarisation uses its own slope. This is the transverse component of the extra tension pulling on a moving support, which SPEC's κ\_T ΔT term (termination geometry, a different path) does not provide. A7's measured residual 5.2 × 10⁻⁵ after adding only −ΔT y\_x,modal(L) is consistent with the missing w\_b²/L part of S.

## Proposed correction (planner recommendation)
1. SPEC-GEO, "Tension modulation": S = Σ\_n kₙ² (q\_v,n² + q\_h,n²) + (w\_v² + w\_h²)/L for a segment on a movable bridge (w the bridge displacement per polarisation axis), so V\_KC is the energy of the whole string's stretch.
2. SPEC-GEO, "Transmission to the body": "On a movable bridge the modulated tension also pulls transversely: each polarisation's bridge force gains −ΔT y\_x(L) (bridge at x = L) or +ΔT y\_x(0) (bridge at x = 0), with y\_x the full slope including the lifting term, evaluated with the same discrete ΔT as the segment's SAV gradient so that the transverse energy balance holds."
3. Not a D-13 option: it is required for the energy balance SPEC-GEO already states, and costs one dot product per polarisation per sample (the slope at the bridge), small next to the pair sums.
4. Placement: A7 follow-up (string side). The term is a property of the string and its tension, computed where A7 already computes `verticalBridgeExtra()` (and a horizontal counterpart); V\_KC's S gains the w²/L term in A7's `TensionModulationPotential`. A9 (engine/src/bridge) is unchanged: the bridge receives F\_b as input. C1 composes it. Test: transverse energy balance on a movable mass–spring bridge with tension modulation, explicit and implicit bridge schemes, to the default tier's tolerance (to be set from the follow-up's measurement, not loosened afterwards).

## Resolution
Pending the owner's review (physics). Until then SPEC stands and A7 merges as is, with the movable-bridge energy deviation recorded as an open risk. Affected phases: A7 (follow-up), A10 (the exact tier must include the same coupling), C1, B5 (fitted κ\_T must not absorb this term).
