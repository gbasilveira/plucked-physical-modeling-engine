# SI-0020 SPEC-OSC bandwidth rule rejects free-mass (Ω = 0) and overdamped lumped bridge modes

Status: resolved (owner decision 2026-10-12: overdamped bridge modes accepted by default, selectable) · Raised by: A4, implementer · Date: 2026-10-12
SPEC section: SPEC-OSC "Bandwidth", "Difference form"; SPEC-BRG bridge models; SPEC-BODY "Local bridge model"

## Failing test
None fails. A4's mass–spring–damper bridge maps to one mode Ω = √(k\_b/m), σ = r\_b/(2m), Φ = 1/√m. With k\_b = 0 (a free mass, the "mass-like bridge" of A4-T2) Ω = 0, and SPEC-OSC "Bandwidth" ("A mode MUST be dropped if … σ ≥ ω (overdamped)") rejects it even for σ = 0. A4 special-cases Ω = 0 in `compileBridgeMode` (docs/dev/bridge.md, "Free mass") and refuses σ ≥ Ω > 0 with `Rejected`.

## Evidence
- A free bridge mass is a valid physical model and is what A4-T2 and SPEC-BRG's small-admittance example (Y = 1/(iωm)) use.
- A4's Ω = 0 coefficients are the limit of the SPEC-OSC formulas: ω̂² = 0, σ̂Δt = tanh(σΔt), κ = 0, β = (1 − σ̂Δt)/(1 + σ̂Δt), γ = Δt²/(1 + σ̂Δt); the discrete poles are exactly 1 and e^(−2σΔt), the exact poles of w'' + 2σw' = F/m sampled at Δt.
- For σ ≥ Ω > 0 the SPEC-OSC pole-matching formula continues analytically (cos(ω\_dΔt) → cosh(√(σ² − Ω²)Δt)), but SPEC does not define it and no instrument needs it yet. The overdamped exclusion exists to drop non-physical string and body modes; a lumped bridge damper is a different case.

## Proposed correction
SPEC-OSC "Bandwidth", append: "The overdamped exclusion applies to string and body modes. A lumped bridge mode (SPEC-BRG mass–spring–damper) with Ω = 0 is kept and compiled with the limit coefficients ω̂² = 0, κ = 0, σ̂Δt = tanh(σΔt), β = (1 − σ̂Δt)/(1 + σ̂Δt), γ = Δt²/(1 + σ̂Δt). A lumped bridge mode with σ ≥ Ω > 0 is refused at configure time (`Rejected`) until an instrument needs it (SI-0020)."

## Resolution
Resolved 2026-10-12 by the orchestrator's default, ratifying A4's behaviour as proposed. Numerics (a new coefficient case): flagged for owner review. SPEC updated in the `PLAN: file SI-0017 to SI-0020 …` commit. Affected phases: A4 (already conforms; docs/dev/bridge.md may cite SI-0020), A8 (SIMD kernels keep the Ω = 0 case), B3 (modal-set bridges keep dropping σ ≥ Ω body modes).

## Owner decision (2026-10-12)

The owner asked for a plain explanation and our best decision.

**In plain words:**

- A bridge modelled as a mass on a spring with a damper either rings (oscillates while it settles) or, if the damper is strong enough, creeps back to rest without ringing. The second case is "overdamped".
- The oscillator formulas in SPEC were written for ringing modes, so overdamped bridge modes were refused.
- A free mass (no spring) was already accepted.

**Decision (standing policy D-13: implement alternatives as options):**

- Overdamped lumped bridge modes are now compiled with the exact non-ringing version of the same formulas: two real decay rates instead of a frequency and one decay rate. SPEC-OSC "Bandwidth" gives the cancellation-free formula.
- The planner checked numerically that the resulting discrete poles equal the exact ones, e^((−σ ± μ)Δt), to round-off. The coefficient ω̂²Δt² stays between 0 and 2, so the mode is stable and its energy can only fall.
- The engine setting `numerics.overdampedBridgeModes` selects **Accept** (default) or **Reject** (the previous behaviour).
- Accept is the default because it costs nothing extra and lets more instruments load.
- String and body modes are not affected: an overdamped string or soundboard mode is still dropped, because such modes are not physical there.

Implemented by phase **A9** (`compileBridgeMode` in engine/src/bridge). A8's kernels need no change (the difference form is the same). Interface member: ICR-0002.
