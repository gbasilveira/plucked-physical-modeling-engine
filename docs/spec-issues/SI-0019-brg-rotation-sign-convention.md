# SI-0019 SPEC-BRG names R(θ\_b) without its sign convention; A4 and P0.5 differ

Status: resolved · Raised by: A4, implementer (P0.5 discrepancy found by the planner) · Date: 2026-10-12
SPEC section: SPEC-BRG "Polarisation coupling"

## Failing test
None yet. A4 implemented F\_∥ = cos θ\_b F\_v + sin θ\_b F\_h, F\_⊥ = −sin θ\_b F\_v + cos θ\_b F\_h, i.e. R(θ\_b) = [[c, s], [−s, c]], and asked for ratification (docs/dev/bridge.md, "Polarisation rotation (A4-R4)").

## Evidence
- SPEC-BRG writes [F\_∥; F\_⊥] = R(θ\_b)[F\_v; F\_h] and [w\_v; w\_h] = R(θ\_b)ᵀ[w\_∥; w\_⊥] but does not define R.
- The P0.5 reference solver (reference/src/simulation.cpp, bridge assembly "Rows: [w_v; w_h] = R(theta_b)^T [w_par; w_perp]" and the BridgeForce observation cos θ F\_v − sin θ F\_h) uses the standard rotation R(θ) = [[c, −s], [s, c]]. A4 uses its transpose. The two agree at θ\_b = 0 and differ by θ\_b → −θ\_b otherwise, so an A4-T3 or C1-T2 comparison with θ\_b ≠ 0 and asymmetric initial conditions would disagree, and an instrument file's `thetaB` would mean opposite senses in the engine and the reference.
- Physically the sign only mirrors the sense of the vertical–horizontal exchange; either is valid if fixed once.

## Proposed correction
SPEC-BRG "Polarisation coupling", after the formula, add: "R(θ) = [[cos θ, −sin θ], [sin θ, cos θ]] (the standard rotation): F\_∥ = cos θ\_b F\_v − sin θ\_b F\_h, F\_⊥ = sin θ\_b F\_v + cos θ\_b F\_h. The bridge's ∥ axis is the direction (cos θ\_b, −sin θ\_b) in the (v, h) plane, so positive θ\_b turns the ∥ axis from +v away from +h. `BridgePointDesc::thetaB` uses this sense (SI-0019)."

## Resolution
Resolved 2026-10-12 by the orchestrator's default: the standard rotation, which the independent reference already uses. Convention only; no physics or numerics change. SPEC updated in the `PLAN: file SI-0017 to SI-0020 …` commit. Affected phases: **A4 must change** `bridge_point.hpp` (swap the signs of the sin terms in both rotations), its rotation tests and docs/dev/bridge.md "Polarisation rotation"; the A4 test-author's A4-T3 should include one θ\_b ≠ 0 case with asymmetric excitation against P0.5 to pin the convention. P0.5 already conforms. B1 (`thetaB` field description) and E-phases fitting θ\_b follow this sense.
