# SI-0016 SPEC-DMP sostenuto: pedal-down threshold and "lifted by its key" are undefined

Status: resolved (orchestrator default; control logic, no physics) · Raised by: planner, while briefing A6 · Date: 2026-10-11
SPEC section: SPEC-DMP "Sostenuto", "Lift fraction"; SPEC-API "Events" (`Pedal`, `KeyDepth`)

## Failing test
None yet. A6-T2 ("table-driven tests of key, sustain and sostenuto sequences against expected lift states") needs a defined rule.

## Evidence
- SPEC-DMP: "When the sostenuto pedal goes down, every key whose damper is lifted by its key at that instant is latched with l\_key = 1 until the pedal is released."
- The `Pedal` event carries a continuous depth in [0, 1] (`pedal.sostenuto`, params.json); SPEC does not say at which depth the pedal is "down" or "released".
- With `KeyDepth` events, l\_key is continuous; SPEC does not say when a damper counts as "lifted by its key".

## Proposed correction
SPEC-DMP "Sostenuto", add: "The sostenuto pedal is down while its depth is ≥ 0.5 and released when it falls below 0.5 (no hysteresis). At the sample where it goes down, a key is latched when its l\_key ≥ 0.5 (a held key has l\_key = 1); latched keys keep l\_key = 1 until the pedal is released, whatever their key events. Keys without a damper (hasDamper = false) are not affected."

## Resolution
Resolved 2026-10-11 by the orchestrator's default under the owner's proceed instruction, as proposed. Control logic only (no physics or numerics); the owner may change the thresholds. SPEC-DMP updated in the `A6: …` briefing commit. Affected phases: A6 (A6-R5, A6-T2), C4 (event handling).
