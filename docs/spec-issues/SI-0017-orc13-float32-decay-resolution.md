# SI-0017 ORC-13's 0.1% decay criterion is below float32 resolution for slowly decaying modes

Status: resolved · Raised by: A6, test-author · Date: 2026-10-12
SPEC section: SPEC-ORC "Verification oracles", row "Damper (diagonal)" (ORC-13); SPEC-OSC "Difference form"

## Failing test
ORC-13 (A6 test-author): e = 1, single mode; the measured decay rate must equal σ\_n + e R\_d φ̄² / (2ρA) within 0.1%. On a bass wound string (27.5 Hz, n = 1, σ = 0.46 s⁻¹ at 48 kHz, σΔt = 9.6 × 10⁻⁶) the float32 run measures a relative decay error of −1.430 × 10⁻³ (the test-author's independent prediction −1.445 × 10⁻³). The float64 check of the damped σ against the formula passes at 10⁻⁹. The failure blocks merging the ORC-13 test.

## Evidence
The difference form stores β = (1 − σ̂Δt)/(1 + σ̂Δt) ≈ 1 − 2σΔt in float32. Below 1 the float32 spacing is 2⁻²⁴ = 5.96 × 10⁻⁸, so β is rounded by up to 2⁻²⁵. The amplitude decays as β^(n/2), so σ\_eff = −ln β / (2Δt) and the relative error of the realised decay rate is bounded by

  |Δσ / σ| ≤ 2⁻²⁵ / (2σΔt).

Planner check (Python, NumPy float32 round-to-nearest of the exact β, σ\_eff from −ln β\_32 / (2Δt), fs = 48 kHz):

| σ (s⁻¹) | σΔt | realised error | bound 2⁻²⁵/(2σΔt) |
| --- | --- | --- | --- |
| 0.46 | 9.6 × 10⁻⁶ | +1.37 × 10⁻³ | 1.55 × 10⁻³ |
| 1.0 | 2.1 × 10⁻⁵ | −5.2 × 10⁻⁵ | 7.2 × 10⁻⁴ |
| 4.8 | 1.0 × 10⁻⁴ | −3.2 × 10⁻⁵ | 1.5 × 10⁻⁴ |
| 9.6 | 2.0 × 10⁻⁴ | +6.8 × 10⁻⁵ | 7.5 × 10⁻⁵ |

The error is deterministic quantisation of one coefficient, not drift; its sign depends on where the exact β falls between two float32 values (the test-author's slightly different σ gives the negative sign). The 0.1% bound can only hold in float32 when σΔt ≳ 1.6 × 10⁻⁵, and a margin for the decay fit needs σΔt ≳ 2 × 10⁻⁴. ORC-01 is unaffected: T60 = 20 s gives σΔt = 7.2 × 10⁻⁶, bound 0.21%, within its 0.5% criterion.

## Proposed correction
SPEC-ORC row "Damper (diagonal)", tolerance cell, replace "0.1%" by:

"σ passed to the oscillator compiler: 10⁻⁹ relative (float64). Measured decay rate: 0.1% in float64; in float32 0.1% + 2⁻²⁴/(2σΔt), the β quantisation bound with a factor-2 margin (SI-0017)."

SPEC-OSC "Difference form", after the β, κ, γ formulas, add: "Stored in float32, β resolves the decay rate only to a relative 2⁻²⁵/(2σΔt) (1.6 × 10⁻³ at σΔt = 10⁻⁵); tests of measured decay in float32 MUST allow for it (SI-0017)."

## Resolution
Resolved 2026-10-12 by the orchestrator's default, as proposed (the float32-aware tolerance rather than restricting the oracle to σΔt ≥ 2 × 10⁻⁴, so the bass case stays covered). Test criterion and documentation only; engine behaviour is unchanged. Flagged for owner review because it widens an oracle tolerance. SPEC updated in the `PLAN: file SI-0017 to SI-0020 …` commit. Affected phases: A6 (ORC-13 test-author: apply the float32 term; keep the float64 check), V1 (registry text). engine/tests/oracles.md is updated by the A6 test-author.
