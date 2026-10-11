# SI-0007 Parameter registry: provisional ranges and classes, soft-pedal duplication, output gains, f_max duplication

Status: open · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-API "Parameter classes", "Events" (`Pedal`); SPEC-DMP "Lift fraction", "Timing"; SPEC-HAM "Una corda (grand)"; SPEC-INST "Piano defaults"

## Failing test
None. Found in the P0.4 code review of `params.json` (interfaces-v1 = 2daee85).

## Evidence
1. **Provisional ranges and classes.** SPEC gives defaults but no ranges for: `output.gain` (2001, −96 to +24 dB); `sympathetic.lowPass` (3101, 500 to 20 000 Hz); `damper.tauUp` (4001, 1 to 50 ms), `damper.tau0` (4002, 2 to 100 ms), `damper.kRelease` (4003, 0 to 10); `damper.eta` (4103, 0.5 to 8, class structural); `damper.halfPedalStart/End` (4101, 4102, class structural); `unaCorda.kappaUC` (5003, 0 to 1), `unaCorda.deltaP` (5004, −2 to 2); `longitudinal.kappaL` (6001, 0 to 0.2), `longitudinal.modes` (6002, 0 to 32). `strings.fMax` (6003) has a SPEC range of 12 to 20 kHz but no default; 16 kHz was chosen.
2. **Soft pedal twice.** `pedal.soft` (1003, the `Pedal` event's Soft value) and `unaCorda.shift` (1010, "shift s in [0, 1]") both drive the same s of SPEC-HAM. SPEC-API lists "una corda shift" as a continuous parameter and "Soft" as a pedal, without saying they are one quantity.
3. **Output gains.** SPEC-API lists "output gains" (plural) as continuous. The registry has one master gain; channels depend on the instrument, so a static registry cannot list per-channel entries.
4. **f\_max twice.** `EngineConfig::fMaxStrings` and registry entry `strings.fMax` (structural) hold the same quantity with no stated precedence.

## Proposed correction
1. Confirm every provisional range and class as listed. They are UI and automation bounds, not physics: each contains SPEC's default and typical values. Two class choices stand: η\_d, P₀ and P₁ stay structural (they shape the damper law and are set per instrument, not performed). `strings.fMax` default: 16 kHz, in the middle of SPEC's range; V2 may change it after measuring cost.
2. One quantity, one id. SPEC-API: "The Soft pedal value is the una corda shift s on a grand and the hammer-rail position on an upright." Keep `pedal.soft` (1003); retire `unaCorda.shift` (1010) and reserve its id.
3. SPEC-API: "Output gain is one master gain in dB. Per-channel gains are a structural property of each channel (ChannelDesc)." Add `gainDb` to `ChannelDesc` when D4 or E1 needs a channel balance.
4. SPEC-API: `EngineConfig::fMaxStrings` is authoritative at `prepare`; a `strings.fMax` Param event is a structural request that recompiles the instrument with the new value and updates the engine's copy. Instrument files do not carry f\_max.

None of these changes physics or numerics; the f\_max default (item 1) is a cost choice that V2 revisits.

## Interface impact
- Item 1: none.
- Item 2: removing a registry entry is a breaking change under the interfaces-v1 policy. Route it through the interfaces-v2 ICR (SI-0005, SI-0006). If no v2 bump happens, keep 1010 as a documented alias of 1003 instead (compatible, documentation-only).
- Item 3: SPEC text only now; `ChannelDesc::gainDb` later by a compatible ICR.
- Item 4: SPEC text and a documentation-only comment change in `engine.hpp` and the `strings.fMax` description.

## Recommended resolution
Accept items 1 to 4. The planner updates SPEC-API once accepted; registry edits ride on the interfaces-v2 ICR.

## Resolution
Open. Orchestrator acceptance needed; no human reviewer.

Affected phases: C4, D1, D4, D6, V2.
