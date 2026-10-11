# SI-0010 Rear and duplex segments: tension source and the undefined σ_felt

Status: resolved · Raised by: planner, while briefing A2 and B1 · Date: 2026-10-11
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
Resolved 2026-10-11 by the orchestrator's defaults under the owner's proceed instruction; both items as proposed above.

1. **Tension of At0 segments** (consistency fix, no human reviewer needed): the tuning rule applies to speaking segments (`bridgeEnd = AtL`) only; a rear or duplex segment takes the compiled T of the nearest preceding speaking segment in `segments` order, and its f₁ follows from inverting the closed-form tension on L\_d. A group whose first segment is At0 is invalid. SPEC-STR "Tension for pitch" and "Duplex and rear segments" updated.
2. **σ\_felt** = 20 s⁻¹, instrument-wide, provisional (T60 ≈ 0.35 s at m\_d = 1). This is a physical default chosen by the planner, not a measured value: **flagged for owner review**; E1 and E3 fit it. SPEC-STR "Duplex and rear segments" defines σ\_felt and SPEC-INST "Piano defaults" lists it. `Defaults::muteFeltSigma{20.0}` is on the next-ICR list (docs/icr/README.md); until that ICR is applied, compileInstrument uses the constant 20 s⁻¹.

Consequences for implementers:

- A2 keeps `applyMute(modes, muteRatio, sigmaFelt, detuneHCents)` with no default inside the string module (engine/src/string/include/pme/string/string_model.hpp).
- C1 (compileInstrument) passes σ\_felt from `Defaults` (or 20 s⁻¹ before the ICR) as the 3rd argument and **the string's `detuneH` as the 4th argument** of `applyMute`; `StringModes` does not store `detuneH`, and the horizontal oscillator coefficients need it.
- B1 keeps the At0-after-AtL validation; generic-grand-88 may now include rear segments with `muteRatio` (an optional follow-up, not required for B1).

SPEC commit: the `PLAN: file SI-0011 …` commit (same change set). Affected phases: A2 (none), B1 (optional example), C1 (compileInstrument rear segments and mute; may now be briefed on them), E1, E3 (fitted values).
