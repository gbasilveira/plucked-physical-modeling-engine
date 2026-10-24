# SI-0024 Starting a mode from rest under a non-zero force puts a longitudinal front Δt/2 early

Status: resolved provisionally (2026-10-12, orchestrator default; numerics, flagged for owner review) · Raised by: A7, test-author · Date: 2026-10-12
SPEC section: SPEC-EXEC (new paragraph "Starting a forced mode from rest"); SPEC-PLK "Initial displacement"; SPEC-OSC "Difference form"

## Failing test
A7-T3: "against P0.5, the longitudinal precursor reaches the bridge at the predicted time within 1%". With the naive start q⁰ = 0, u⁰ = 0 of a longitudinal mode whose pair-sum forcing f⁰ is already non-zero (an initial displacement sets the transverse shape at t = 0), the front arrives Δt/2 early, which consumes half of the 1% budget on the test ladder.

## Evidence
For a mode at rest under a constant force f, the exact displacement after one step is ½ f Δt². The difference form with u⁰ = 0 gives q¹ = γ f⁰ ≈ f⁰ Δt², twice that: equivalent to starting the force Δt/2 earlier. Setting u⁰ = q⁰ − q⁻¹ = −γ f⁰ / 2 (that is, q⁻¹ = γ f⁰ / 2) gives q¹ = u¹ = −βγf⁰/2 + γ f⁰ ≈ ½ γ f⁰, the second-order Taylor start. The A7 test-author measured the Δt/2 shift; the A7 implementer has implemented the Taylor start.

SPEC was silent on two points: how a forced mode starts from rest, and whether an initial-displacement event also restarts the segment's longitudinal modes (SPEC-PLK says only that the transverse modal state is replaced "so the string starts at rest").

## Proposed correction
1. SPEC-EXEC, after the per-sample loop: "**Starting a forced mode from rest.** When a mode starts from rest (q⁰ = 0) while its explicit force f⁰ at the first step is not zero, as the longitudinal modes do at an initial displacement, its state is set to q⁰ = 0, u⁰ = −γ f⁰ / 2. Then q¹ ≈ ½ f⁰ Δt², the second-order Taylor value, instead of f⁰ Δt², which would make a longitudinal front arrive Δt/2 early. A mode whose force is zero at its first step starts with u⁰ = 0 as before."
2. SPEC-PLK "Initial displacement": "The segment's longitudinal modes restart from rest with the Taylor start of Execution order, 'Starting a forced mode from rest'."

## Resolution
Applied to SPEC on 2026-10-12 as the orchestrator's default, because it fills a gap rather than changing stated behaviour and A7 already implements it; it changes numerics, so it is **flagged for owner review** (listed with the other D-13 items for the owner's final approval). Affected phases: A7 (implemented), A10 (the exact tier starts the same way), C1 (composition), P0.5 (the reference's longitudinal start should match; checked in the P0.5-newton follow-up).
