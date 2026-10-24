# Brief: ICR-0001 interfaces-v2 — code-reviewer

Model: sonnet
Branch: review the `ICR-0001: ` commits on the branch the orchestrator names in its launch message; do not create a branch, do not push, do not open a pull request, do not create tags
Written by: planner (Opus), from docs/icr/ICR-0001-interfaces-v2.md (approved by the orchestrator on 2026-10-11)

## Objective

Decide whether the interface agent applied ICR-0001 exactly: interfaces-v1 (2daee85) → interfaces-v2, with matching stubs, registry, `params.json`, interface tests and module note, and nothing else. The orchestrator tags the accepted head `interfaces-v2`, and every later phase builds against it, so compare each header line by line with the ICR's diff.

## Read first

- **docs/icr/ICR-0001-interfaces-v2.md** — the authority for what must and must not change.
- docs/agents/briefs/ICR-0001-implementer.md — items I1 to I12, scope and test names.
- SPEC sections as updated in commit c5cec38 (docs/plan/engine-specification.md): SPEC-API "Engine interfaces"; SPEC-EXEC "Execution order" (Instrument lifetime); SPEC-PLK "Plucked exciters and barriers" (Exciter parameters and events); SPEC-BODY "Soundboard and bodies" (Body frame, Geometry inputs); SPEC-STR "Tension for pitch" and "Contract"; SPEC-INST "Piano defaults", "Instrument file".
- docs/dev/interfaces.md (compatibility policy), docs/adr/ADR-0006-interface-status-returns.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The implementer's handoff report, as given in the orchestrator's launch message.

## Inputs

- Baseline: interfaces-v1 = 2daee85. Diff under review: `git log --grep '^ICR-0001: '` and `git diff 2daee85 -- engine/include/pme engine/src tools/params params.json engine/tests/interfaces engine/tests/oracles.md docs/dev/interfaces.md`.
- CI evidence: the pr.yml run the orchestrator gives after merging. Without a link a test is `not run`.

## Scope

- May edit: `engine/tests/interfaces/` — only to add missing tests for the ICR's "Tests that must change" rows. Commit with `ICR-0001: ` messages; list them in your report. Do not push.
- Scratch work in a separate worktree or your scratchpad directory, never committed.
- Read only: everything else. Describe needed header, stub or doc changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The implementer brief's items I1 to I12 (version, engine, events, exciters, instrument and tuning, body, comment fixes, registry and params.json, stubs, tests, module note, oracle registry), each as the ICR states.

## Tests that must pass

- **P0.4-T1** each header compiles on its own with all compilers;
- **P0.4-T2** the stub Engine processes silence in the rt-safety build without allocating;
- **P0.4-T3** parameter IDs are unique, units are non-empty and ranges are valid;
- every other `pme_interface_tests` case, and the new `ICR-0001 …` cases.

## Review checklist

**Scope**

1. Only May-edit paths of the implementer brief changed; `engine/tests/oracles.md` changed only in the ORC-16 owner cell and the removed "no owning phase" bullet. Messages start `ICR-0001: `. No tag created.
2. `git diff 2daee85 -- engine/include/pme/` contains every change of the ICR's "Proposed change" and nothing else: list any extra or missing member, renamed identifier, reordered member, changed default or changed signature. No header added or removed (17).

**Headers**

3. `version.hpp`: `interfacesVersionMajor == 2`; policy text names interfaces-v2 and ICR-0001.
4. `engine.hpp`: `collectRetired()` is `void`, `noexcept`, non-static, public, declared after `setInstrument`; comments state non-audio-only for `setInstrument` and `collectRetired`, caller serialisation, null-argument semantics, which calls drain the retire queue; existing signatures unchanged (ADR-0006).
5. `events.hpp`: `PluckKind` gone (grep the repository); `Pluck` members are exactly `segment` (int32), `position` (double), `angle` (double), `strength` (float), in that order; comments give units, the 0-position rule, the clamp rule and the flat segment index; seven `Event` alternatives in the original order; trivially-copyable static assertions still present.
6. `exciters.hpp`: `PluckPulse`, `Plectrum`, `Finger`, `InitialDisplacement` member lists, order, units and `strengthGamma{1.0}` defaults match the ICR; variant order `HammerParams, PluckPulse, Plectrum, Finger, InitialDisplacement`; default `ExciterParams` holds `HammerParams`.
7. `instrument.hpp`, `body.hpp`: `unisonDetuneCents` is last in `SegmentDesc`; `BridgeBeamDesc` and the six new `PlateParams` members appended after `ribs` in ICR order, all default-initialised; existing members keep order; comments match SPEC-BODY "Body frame" and "Geometry inputs" (k\_rim in N/m²).
8. Comment-only items (`phiBar`, `registeredPoints`, `f1Target`, `Diagnostic::group`, `Point`, `NoteGroupDesc::key`, `TuningCurve`, `Defaults::localBridgeFSplit`, `ChannelDesc`) say what the ICR says and cite the SI; no declaration changed in them.

**Stubs and registry**

9. `collectRetired` stub is a no-op; `setInstrument` stub comment updated; no retire queue implemented; the rt-safety P0.4-T2 test still passes and calls neither `setInstrument` nor `collectRetired` inside the real-time scope. Negative check in scratch: call `collectRetired()` inside the RT scope after making the stub allocate; T2 must fail.
10. `params.cpp`: 1010 removed; only the descriptions the ICR's table names changed; ids, names, units, ranges, defaults and classes otherwise byte-identical to 2daee85 (`git diff` the table). `params.json` regenerated (run the generator twice; byte-identical; `interfaces_version` 2; no 1010); `P0.4-R4_params_json_matches_registry` passes.

**Tests and docs**

11. Every row of the ICR's "Tests that must change" table is implemented with the stated names; `findParam(1010) == nullptr` is tested; variant size 5 is tested; `collectRetired` is called by a test (coverage ≥ 90% on `engine/src/` minus placeholders with the `linux-coverage` preset).
12. docs/dev/interfaces.md reflects interfaces-v2: policy, header map, events, retired ids, provisional lists, stubs, threading summary.
13. `clang-format --dry-run --Werror`, `clang-tidy -p build/linux-debug` and `check_spdx.py` pass on changed files; `linux-debug` and `linux-rt-safety` build and test with GCC and Clang locally; with the orchestrator's CI link, all matrix jobs green.

## Definition of done

- [ ] Every ICR-0001 item verified, or a blocking review item filed against it.
- [ ] P0.4-T1 to P0.4-T3 and the ICR-0001 cases pass in CI on Windows, macOS and Linux.
- [ ] Lint, SPDX, RT-safety tests and coverage hold.
- [ ] No interface change outside ICR-0001.
- [ ] Verdict recorded; handoff report consistent with CI results.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately. Any disagreement between the ICR and SPEC: list it for the planner; do not resolve it.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `P0.4`, `role` `code-reviewer`, `model` `sonnet`; `requirements` P0.4-R1 to P0.4-R6 as verified against interfaces-v2 (R6 `not met` until tagged); `tests` P0.4-T1 to P0.4-T3 with the CI run or `not run`; `outputs` listing tests you added; `pull_requests` empty; `icrs` `["docs/icr/ICR-0001-interfaces-v2.md"]`. Start `notes` with `Verdict: approve` or `Verdict: request changes` and the reviewed head SHA, then blocking items, then each checklist number with its command and result.
