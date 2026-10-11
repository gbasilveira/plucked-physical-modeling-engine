# SI-0005 Plucked exciters: Pluck event versus ExciterParams, finger model, initial displacement

Status: open · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-PLK "Exciters"; SPEC-API "Descriptions" (`ExciterParams`), "Events" (`Pluck`)

## Failing test
None yet. Found in the P0.4 code review. P1 (P1-R1 to P1-R5) and P6 cannot implement the Pluck event without an answer.

## Evidence
1. **Two sources for one exciter.** `NoteGroupDesc::exciter` holds one `ExciterParams` per group (with t\_p, F\_p, x\_p, b\_p, θ\_p for a pulse; m\_p, K\_p, v\_d, h\_slip for a plectrum). The `Pluck` event also carries position, force, angle and kind. SPEC does not say which wins, nor what happens when `kind` differs from the group's configured exciter.
2. **`Pluck.force` for plectrum and finger.** Those models are driven by v\_d, not by a force; SPEC gives no mapping. Provisional: "driver force scale (N)".
3. **`Pluck.position` unit.** Provisional: metres along the segment's x axis. SPEC does not say metres or ratio, nor which end is 0.
4. **Finger.** SPEC-PLK lists m\_f, K\_f, α\_f, μ\_f, b\_f and says "as plectrum". The plectrum has a driver spring K\_p and a separate contact K, α; the finger row's K\_f could be either. Driver speed and h\_slip are implied by "as plectrum" but not listed. Provisional: K\_f is the contact stiffness, no driver spring, plus `driverSpeed` and `slipHeight`.
5. **Initial displacement.** SPEC-PLK lists it as an exciter (oracle and presets; P1-R4, ORC-10). `ExciterParams::e` is `std::variant<HammerParams, PluckPulse, Plectrum, Finger>` and `PluckKind` has no matching value.

## Proposed correction (SPEC-PLK and SPEC-API)
1. The group's `ExciterParams` is the model and its parameters; the event chooses only where and how hard. `Pluck` carries segment, position, angle and a normalised strength ν ∈ (0, 1]. `kind` is removed: an instrument is data, and the model is fixed by its description.
2. Strength maps like the hammer's velocity map (SPEC-HAM): pulse F\_p = F\_p,max · ν^γ; plectrum and finger v\_d = v\_d,max · ν^γ, with F\_p,max, v\_d,max and γ in the exciter parameters.
3. Position is metres on the segment's own x axis (0 at the static end, the nut for a fretted string). A position of 0 in the event means "use the description's x\_p".
4. Finger has the plectrum's structure: tip mass m\_f on a driver spring K\_f; flesh contact with stiffness K\_c,f, exponent α\_f, Hunt–Crossley μ\_f over a raised-cosine window b\_f; driver speed v\_d; release threshold h\_slip,f.
5. Add `InitialDisplacement {A, x_p}` to the exciter variant (closed-form start of SPEC-PLK, applied at the event's sample).

Items 2 and 4 change exciter physics inputs and need a human reviewer.

## Interface impact
Breaking ICR (interfaces-v2): `Pluck` loses `kind` and `force` and gains `strength`; `PluckKind` is removed; `ExciterParams::e` gains an alternative (breaks exhaustive visitors); `Finger` gains members (driver spring, contact stiffness split). No implementation consumes these yet, so the cost is a header edit and the P0.4 tests. Batch everything into one ICR.

## Recommended resolution
Accept items 1 to 5 and apply one ICR before P1 starts (P1 depends on G1, so there is time). If the owner prefers to keep interfaces-v1 unbroken, the fallback is: keep `kind`, require it to match the group's exciter (mismatch ignored), document `force` as F\_p for pulses and ignored otherwise, and add `InitialDisplacement` in the v2 bump that SI-0006 may cause anyway.

## Resolution
Open. Orchestrator acceptance and a human reviewer (items 2 and 4) needed.

Affected phases: P1, P5, P6, C4, B1, E4–E8.
