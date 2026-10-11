# SI-0010 Rear and duplex segments: tension source and the undefined σ_felt

Status: open · Raised by: planner, while briefing A2 and B1 · Date: 2026-10-11
SPEC section: SPEC-STR "Tension for pitch", "Duplex and rear segments"; SPEC-DMP "Other felts"; SPEC-API `SegmentDesc`, `Defaults`

## Failing test
None yet. Found while writing the A2 (A2-R7) and B1 (B1-R6 generic-grand-88) briefs. A2-R7 asks for rear and duplex segments "with mute ratio"; C1's compileInstrument must give those segments a tension and a mute damping; neither is fully defined.

## Evidence

| Item | SPEC says | Problem |
| --- | --- | --- |
| 1. Tension of an At0 segment | "Duplex and rear segments": a separate short string with its own L\_d and **the same T, ρA and EI** as the speaking string. "Tension for pitch" (SI-0004): for **every segment** of a group with key ≥ 0, compileInstrument computes f₁ from the tuning curve and uses it in place of the description's f₁. | Contradiction. Applied to a rear segment of length L\_d ≠ L, the tuning rule gives T\_d = 4L\_d²ρA f₁² − π²EI/L\_d² ≠ T. SPEC also does not say which speaking string a rear segment continues: `SegmentDesc` has no link, and a trichord has three speaking strings. |
| 2. σ\_felt | σ\_mute = m\_d · σ\_felt (SPEC-STR); "Rear-segment mutes use the constant σ\_mute" (SPEC-DMP "Other felts"). | σ\_felt has no definition, unit, default or description field. `Defaults` (instrument.hpp) and `SegmentDesc` hold only `muteRatio`. |

## Proposed correction

1. **Tension of At0 segments** (SPEC-STR "Tension for pitch" and "Duplex and rear segments"; consistency fix, no physics change). Replace the tuning sentence "For every segment of a note group with key ≥ 0" by "For every speaking segment (`bridgeEnd = AtL`) of a note group with key ≥ 0". Add: "A rear or duplex segment (`bridgeEnd = At0`) continues the nearest speaking segment that precedes it in the group's `segments` order and takes that segment's compiled T; compileInstrument sets its f₁ to the value for which the closed-form tension equals T, f₁ = √((T + π²EI/L\_d²) / (4 L\_d² ρA)). A group whose first segment is At0 is invalid." This needs no interface change: the order of `segments` carries the link, and A2 provides the inverse of the tension formula.
2. **σ\_felt** (SPEC-STR, SPEC-DMP, SPEC-INST "Piano defaults"): define σ\_felt as the decay rate (1/s) a fully muted (m\_d = 1) rear segment receives on every mode, an instrument-wide value. Add to the piano defaults table: "Rear-segment felt damping σ\_felt | 20 s⁻¹ (T60 ≈ 0.35 s at m\_d = 1), provisional; E1 fits | Default". Add `double muteFeltSigma{20.0}` to `Defaults` in a later compatible ICR (a new member with a default keeps source compatibility; it can be batched with the documentation items already carried to the next ICR, docs/icr/README.md).

Until resolved: A2 implements σ\_mute with σ\_felt as an explicit argument (no default inside the string module); B1 writes rear segments with `muteRatio` only and enforces the segment-order rule of item 1 as a validation; C1 must not be briefed on rear-segment tension or mute until this issue is resolved.

## Resolution
Open. Item 1 is a consistency fix (no human reviewer needed). Item 2 sets a physical default: orchestrator decision, flagged for owner review under the proceed instruction; 20 s⁻¹ is the planner's recommended default, not a measured value.

Affected phases: A2 (A2-R7 helper signature only), B1 (validation rule, examples), C1 (compileInstrument), E1, E3 (fitted values).
