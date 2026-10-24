# Brief: ICR-0001 interfaces-v2 — implementer (interface agent)

Model: sonnet
Branch: the branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not push, do not open a pull request, do not create or move tags
Commit messages: start with `ICR-0001: ` (for example `ICR-0001: retire unaCorda.shift (1010)`)
Written by: planner (Opus), from docs/icr/ICR-0001-interfaces-v2.md (approved by the orchestrator on 2026-10-11)

## Objective

Apply the approved interface change request ICR-0001 to the frozen P0.4 interfaces: move `engine/include/pme/` from interfaces-v1 (2daee85) to interfaces-v2, with the matching stubs, registry, `params.json`, interface tests and module note. Change nothing the ICR does not list. The result is the commit the orchestrator tags `interfaces-v2`.

There is no phase card: this is step 3 of the interface change protocol (docs/plan/phasing-plan.md, "Agent briefs and handoff", "Interface change protocol"). The ICR file is your card. Where this brief and the ICR differ, the ICR wins; where the ICR and SPEC differ, SPEC wins and you report it.

## Read first

- **docs/icr/ICR-0001-interfaces-v2.md** — the whole file. Its "Proposed change" section is the exact header-level diff, member by member, with the comments to use; its "Tests that must change" table is your test list.
- The spec issues it resolves, for context only: docs/spec-issues/SI-0003 to SI-0007 (and the SI-0001, SI-0002 follow-up notes).
- docs/plan/phasing-plan.md: **Software guidelines** (code style, documentation rules, real-time rules); the phase card **P0.4 Interface skeleton** (the outputs you are amending).
- SPEC sections, as updated in commit c5cec38 (docs/plan/engine-specification.md):
  - SPEC-API — "Engine interfaces": Descriptions, Real-time interface (`collectRetired`), Event addressing, Events table (`Pluck`), Parameter classes.
  - SPEC-EXEC — "Execution order": Host block step 1, **Instrument lifetime**.
  - SPEC-PLK — "Plucked exciters and barriers": Exciters table and **Exciter parameters and events**.
  - SPEC-BODY — "Soundboard and bodies": Plate model, **Body frame**, **Geometry inputs**.
  - SPEC-STR — "Stiff string model": Tension for pitch (key numbering, f₁ ownership); Contract (point shapes, W = 1).
  - SPEC-INST — "Instrument definitions": Piano defaults (f\_split, string f\_max), Instrument file.
- docs/dev/interfaces.md — the P0.4 module note you update; its compatibility policy defines breaking changes.
- docs/adr/ADR-0006-interface-status-returns.md — signatures and the error model stay as they are.
- docs/agents/report.schema.json.

## Inputs

- Frozen interfaces: **interfaces-v1** = 2daee85 (tag created locally; the owner pushes tags).
- Completed dependency: **P0.4** complete (head 2daee85; CI pr.yml run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38144357509). Its outputs: `engine/include/pme/*.hpp` (17 headers), `engine/src/{osc,string,bridge,body,runtime}/*_stub.cpp` and `engine/src/runtime/params.cpp`, `tools/params/` (`pme_params_json`), `params.json`, `engine/tests/interfaces/`, `docs/dev/interfaces.md`.
- ICR and SPEC: commit c5cec38 `PLAN: ICR-0001 interfaces-v2 and SPEC for SI-0003 to SI-0007` on `ccr-5ef35d73-oqb6fd`.
- ORC-16 owner: commit cb90200 assigned ORC-16 (sample-rate independence) to C1 in the phasing plan; `engine/tests/oracles.md` still says `unassigned (G2)`.
- **Start point.** Another implementer is fixing CI in the root `CMakeLists.txt`, `engine/tests/**` and `docs/dev/**`. Start from the head of `ccr-5ef35d73-oqb6fd` only after the orchestrator tells you those fixes have landed, and rebase onto it again before handing off if it moved.

## Scope

- May edit:
  - `engine/include/pme/` (the headers the ICR names: `version.hpp`, `engine.hpp`, `events.hpp`, `exciters.hpp`, `instrument.hpp`, `string.hpp`, `diagnostics.hpp`, `types.hpp`, `body.hpp`; no header is added or removed)
  - the stub sources: `engine/src/runtime/runtime_stub.cpp`, `engine/src/runtime/params.cpp` (and any other `engine/src/*/*_stub.cpp` only if a header change stops it compiling)
  - `engine/tests/interfaces/`
  - `tools/params/`
  - `params.json` (regenerate it; never hand-edit)
  - `docs/dev/interfaces.md`
  - `engine/tests/oracles.md` — only the ORC-16 row's "Owning phase" cell (`unassigned (G2)` → `C1`) and the bullet that says ORC-16 has no owning phase (delete it). Nothing else in that file.
- Read only: everything else, in particular the root `CMakeLists.txt`, `cmake/`, `engine/tests/` outside `interfaces/` and `oracles.md`, `docs/dev/` other than `interfaces.md`, and all `*_placeholder.cpp`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The ICR's items, numbered for your report's `notes` (the report schema's requirement keys stay P0.4's, see Report):

1. **I1 Version.** `interfacesVersionMajor = 2`; the `version.hpp` policy comment and docs/dev/interfaces.md say interfaces-v2 and cite ICR-0001.
2. **I2 Engine (SI-0006, SI-0007).** `engine.hpp` exactly as the ICR's diff: threading-contract comment (non-audio calls serialised by the caller), `setInstrument` non-audio only with the null-argument and retire semantics, new `void collectRetired() noexcept;` declared between `setInstrument` and `process`, `popDiagnostic` and `prepare` comments mention the retire queue, `EngineConfig::fMaxStrings` comment, destructor comment. Signatures of existing members unchanged.
3. **I3 Events (SI-0005, SI-0004).** Remove `PluckKind`. `Pluck` is `{std::int32_t segment; double position; double angle; float strength;}` in that order with the ICR's comments. `Bend::segment`, `NoteOn::key`, `NoteOff::key`, `KeyDepth::key` comments per the ICR. `Event::payload` keeps its seven alternatives in order. All events stay trivially copyable.
4. **I4 Exciters (SI-0005).** `PluckPulse`, `Plectrum`, `Finger` with exactly the ICR's members, names, order and defaults (`strengthGamma{1.0}`); new `InitialDisplacement`; `ExciterParams::e` is `std::variant<HammerParams, PluckPulse, Plectrum, Finger, InitialDisplacement>`. `HammerParams`, `UnaCordaParams` untouched.
5. **I5 Instrument and tuning (SI-0004, SI-0003).** `SegmentDesc::unisonDetuneCents{}` appended last; `NoteGroupDesc::key`, `TuningCurve`, `Defaults::localBridgeFSplit` comments per the ICR (drop "Provisional" where the ICR drops it).
6. **I6 Body (SI-0003).** New `BridgeBeamDesc` before `PlateParams`; `PlateParams` appends `outline`, `thicknessNx`, `thicknessNy`, `thicknessGrid`, `bridges`, `rimRotationalStiffness` after `ribs`, in that order; existing members keep their order; comments of `lengthX`, `lengthY`, `thickness`, `rimStiffness`, `ribs`, `RibDesc` and `ChannelDesc` per the ICR.
7. **I7 Comment fixes (SI-0001, SI-0002, SI-0003, SI-0004).** `StringModes::phiBar` and the `compileString` `registeredPoints` comment say point shape values φ\_n(x₀), W = 1; `StringParams::f1Target` comment; `Diagnostic::group` = index into `InstrumentDesc::groups`; `Point` frame comment. No declaration changes in these.
8. **I8 Registry and params.json (SI-0007).** In `params.cpp`: delete entry 1010; update the descriptions of 1003, 6003 and drop "Range provisional." / "Class provisional." from 2001, 3101, 4001, 4002, 4003, 4101, 4102, 4103, 5003, 5004, 6001, 6002, as the ICR's table says. No id, name, unit, range, default or class changes. Regenerate `params.json` with the `update_params_json` target; it must show `"interfaces_version": 2` and no 1010. Change `tools/params/` only if regeneration needs it.
9. **I9 Stubs.** `runtime_stub.cpp`: define `Engine::collectRetired() noexcept` as a no-op; update the `setInstrument` comment. Do not implement the retire queue (C1 does). The stubs still allocate nothing in `process`, `reset`, `popDiagnostic`, `setInstrument` and `collectRetired`.
10. **I10 Tests.** Every row of the ICR's "Tests that must change" table, in `engine/tests/interfaces/`. New cases are named `ICR-0001 <what>`; changed cases keep their P0.4 IDs except the version case, renamed `ICR-0001 interfaces version is 2`. Add `collectRetired()` to the P0.4-T2 rt-safety loop **outside** the `ScopedRealtimeThread` scope (it is a non-audio call), and keep `setInstrument` outside it too.
11. **I11 Module note.** docs/dev/interfaces.md: compatibility policy now interfaces-v2 (ICR-0001); header map adds `BridgeBeamDesc`, `InitialDisplacement`, removes `PluckKind`; Events section describes the new `Pluck` and the flat segment index; registry section lists "Retired ids: 1010 `unaCorda.shift` (ICR-0001; use 1003 `pedal.soft`)" and removes the "Provisional entries" paragraph except the `strings.fMax` default (V2 may revise); "Provisional types" drops what the ICR settled (`Point` frame, `TuningCurve`, `Finger`, `Pluck` units, `PlateParams` uniform thickness) and keeps the rest; Stubs section mentions `collectRetired`; the threading summary says `setInstrument` is non-audio only.
12. **I12 Oracle registry.** `engine/tests/oracles.md` ORC-16 owner C1, as in Scope.

## Tests that must pass

- **P0.4-T1** each header compiles on its own with all compilers (header count unchanged at 17).
- **P0.4-T2** the stub Engine processes silence in the rt-safety build without allocating.
- **P0.4-T3** parameter IDs are unique, units are non-empty and ranges are valid.
- Every other existing case in `pme_interface_tests` (P0.4-R3, P0.4-R4 including `P0.4-R4_params_json_matches_registry`, P0.4-R5), as changed by I10.
- The new `ICR-0001 …` cases.
- The whole PR CI (`pr.yml`) on Windows, macOS and Linux, including clang-format, clang-tidy, SPDX and the rt-safety jobs. No ORC row is in scope.

## Definition of done

- [ ] Every item I1 to I12 is applied exactly as the ICR states; `git diff 2daee85 -- engine/include/pme/` shows nothing the ICR does not list.
- [ ] Every test above passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests and lint pass; the coverage rule holds (`collectRetired` is called by a test).
- [ ] docs/dev/interfaces.md updated, naming the SPEC sections and ICR-0001.
- [ ] No interface change outside ICR-0001. If an item cannot be applied as written (for example it does not compile or contradicts SPEC), stop that item and report it under `icrs` with the reason; do not improvise a different declaration.
- [ ] Reviewer approval recorded (the ICR-0001 code-reviewer).
- [ ] Handoff report submitted and consistent with CI results.

## Working rules

- Commit in small steps with `ICR-0001: ` messages. Do not squash other agents' commits.
- After the headers compile, run `cmake --build --preset linux-debug --target update_params_json` and commit the regenerated `params.json` in the same commit as the registry edit.
- Grep the whole repository (outside docs/plan, docs/icr, docs/spec-issues) for `PluckKind`, `.force`, `peakForce`, `driverSpeed`, `Finger{`, `stiffness`, `1010` and `unaCorda.shift` and fix every use inside your May-edit paths; report any use outside them.
- The tag: you do not create it. In your report, give the full SHA of your final commit; the orchestrator tags it `interfaces-v2` after review and merge, the owner pushes the tag, and the planner records the SHA in the ICR and in docs/status/phase-status.md.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json:

- `phase` `P0.4` (the schema has no ICR phase; this ICR amends P0.4's outputs), `role` `implementer`, `model` `sonnet`.
- `requirements`: P0.4-R1 to P0.4-R6 re-verified against interfaces-v2 (R6 `not met` until the tag exists).
- `tests`: P0.4-T1 to P0.4-T3 with the CI run link, or `not run`.
- `outputs`: every changed path. `icrs`: `["docs/icr/ICR-0001-interfaces-v2.md"]` plus any item you could not apply.
- `notes`: start with `ICR-0001 final commit: <full SHA>`; then one line per item I1 to I12 with `applied` or `not applied: <reason>`; then the names of the new and renamed test cases.
