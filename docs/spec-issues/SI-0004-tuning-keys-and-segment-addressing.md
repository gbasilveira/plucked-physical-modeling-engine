# SI-0004 Tuning curve, key numbering, unison detune and segment addressing

Status: resolved · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-STR "Tension for pitch"; SPEC-API "Descriptions" (`NoteGroupDesc`, `TuningCurve`), "Events" (`Pluck`, `Bend`)

## Failing test
None yet. Found in the P0.4 code review. B1 (schema), C1 (compileInstrument) and C4 (event semantics) need one answer.

## Evidence
1. **Key numbering.** SPEC-API gives `NoteGroupDesc.key` and `NoteOn.key` with no numbering. SPEC-STR fixes pitch as equal temperament with A4 = 440 Hz but not which key number is A4.
2. **TuningCurve.** Named only. Provisional: `referencePitch` (Hz of MIDI key 69) and `stretchCents` indexed by MIDI key.
3. **Unison detune.** SPEC-STR: f₁ = equal temperament + per-key stretch + "the string's unison detune in cents". No description struct holds the per-string detune. `StringParams::f1Target` holds the final f₁, so it is undefined whether the caller or compileInstrument computes it from the tuning curve, and "Tension MUST be recomputed whenever any of these changes" has no owner.
4. **Segment index.** `Pluck.segment` and `Bend.segment` say "segment" with no index space. Provisional: a flat index over all segments in description order (group by group).
5. **Plucked instruments.** A guitar or sitar string is not a key; the meaning of `key` for those groups, and for sympathetic-only groups, is undefined.

## Proposed correction
SPEC-STR "Tension for pitch" and SPEC-API gain:

- `key` is the MIDI note number (0 to 127, A4 = 69) of the group's nominal pitch. Keys are unique among groups that receive NoteOn; groups that never receive NoteOn (plucked strings addressed by segment, sympathetic strings) MAY use −1.
- compileInstrument computes f₁ = f\_ref · 2^((key − 69)/12) · 2^((stretch[key] + d\_s)/1200) for every segment of a group with key ≥ 0, where d\_s is the segment's unison detune in cents; it overwrites `StringParams::f1Target`. For key = −1 the description's f1Target is used as given. A change to the tuning curve or a detune is a structural parameter change and recompiles the affected strings.
- `stretchCents` is indexed by MIDI key; keys beyond its length have 0.
- Segment indices in events are flat: segments numbered from 0 across groups in InstrumentDesc order, then within each group in `segments` order. The index is stable for a given instrument file; a structural edit that adds or removes segments changes it, and events addressing an index that does not exist are ignored.

None of this changes a formula; it assigns ownership and numbering. The defaults it fixes (A4 = 69, overwrite rule) are behaviour, so the orchestrator should accept it before B1 and C1.

## Interface impact
Compatible ICR: add `double unisonDetuneCents{}` at the end of `SegmentDesc`. Documentation-only ICR: comments on `NoteGroupDesc::key`, `StringParams::f1Target`, `Pluck::segment`, `Bend::segment`. No breaking change.

## Recommended resolution
Accept as proposed. Apply the ICR before B1 starts (B1-R1 mirrors `SegmentDesc`).

## Resolution
Accepted by the orchestrator on 2026-10-11 (owner's proceed instruction). Accepted as proposed.

- SPEC-STR "Tension for pitch" (MIDI keys, A4 = 69, f₁ formula computed by compileInstrument, key = −1 rule) and SPEC-API "Event addressing" (flat segment index), plus `unisonDetuneCents` in the SPEC-API `SegmentDesc` declaration.
- Key numbering and the f₁ ownership rule: Orchestrator default under the owner's proceed instruction — flagged for owner review.
- SPEC commit: "PLAN: ICR-0001 interfaces-v2 and SPEC for SI-0003 to SI-0007".
- Route: `SegmentDesc::unisonDetuneCents` and the comment changes in [ICR-0001](../icr/ICR-0001-interfaces-v2.md). Status becomes resolved when interfaces-v2 is tagged.

Affected phases: B1, C1, C4, P5, P6, E1–E8.

Resolved on 2026-10-11: ICR-0001 applied at 300b995 (interfaces-v2; tag created locally, push pending the owner). Code reviewer approved; pr.yml run 38149794842 green on 12/12 jobs.
