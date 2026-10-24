# Brief: D3 LV2 plug-in — code-reviewer

Model: sonnet
Branch: review the D3 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

a native LV2 build for Linux hosts, sharing the CLAP shell's controller layer.

Your job: decide whether the D3 implementer's work meets the D3 card, the Software guidelines (Real-time rules above all), the LV2 threading classes and docs/agents/briefs/D3-implementer.md, and return a verdict. Check yourself that nothing on the audio thread allocates, locks or calls a non-audio controller function, and that the Turtle cannot drift from the parameter registry.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **D3 LV2 plug-in**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - PLAN-STACK — docs/plan/implementation-plan.md, "Technology stack"
- SPEC-API "Events" and "Parameter classes", SPEC-EXEC "Threading contract" and "Instrument lifetime" (docs/plan/engine-specification.md).
- docs/agents/briefs/D3-implementer.md (scope, architecture, Turtle table, state, worker, test setups, the `lv2` job).
- docs/dev/clap-shell.md ("Controller layer (for D3)", "Provisional identifiers (D-3)", "State (D1-R4)", "Threads") and `plugin/common/include/pme/plugin/*.hpp`.
- docs/adr/ADR-0003, ADR-0005, ADR-0007.
- `.claude/agents/code-reviewer.md` — your checklist.
- The implementer's handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `D3: ` (`git log --grep '^D3: ' --name-only`).
- CI evidence: given by the orchestrator after merging (the `lv2 (linux)` job, and the `plugin (linux|macos|windows)` jobs, which must stay green). Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 (300b995; ICR-0001).
- D-3 open: identifiers are provisional and must live only in `plugin/common/include/pme/plugin/ids.hpp`.

## Scope

- May edit: test sources under `plugin/lv2/tests/` — only to add missing or independent tests for D3-T1, D3-T2, D3-R1 or D3-R2. Commit with messages starting `D3: `. Do not push.
- Read only: everything else. You MUST NOT change implementation, workflows, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D3-R1 LV2 adapter in plugin/lv2 over the same controller layer as D1.
2. D3-R2 Turtle descriptors for ports, parameters and state; worker extension for recompilation.

## Tests that must pass

- **D3-T1** lv2lint passes;
- **D3-T2** a headless LV2 host loads the plug-in and renders a test scenario in CI.

## Review checklist for D3

**Scope and process**

1. D3 commits touch only: `plugin/lv2/**`, docs/dev/lv2.md, the guarded `lv2` line in `plugin/CMakeLists.txt`, appended identifiers in `ids.hpp` (nothing existing changed), additive `plugin/common/` changes each justified in the report, the LV2 gating in `cmake/dependencies.cmake` and the LV2 consumer cell in docs/dev/build.md, one pr.yml block at the end, appended entries in `required_checks.json` and CONTRIBUTING.md, one docs/dev/ci.md subsection, and at most one root `CMakeLists.txt` block at the end. Nothing under `engine/`, `plugin/clap/`, `tools/` (except `required_checks.json`), `compiler/`, `reference/`. `uv run pytest tools/tests/test_p0_2_r7.py` passes.
2. The dependency commit is separate; the LV2 URL, commit `0bcde338db1c63bbc503b4d1f6d7b55ed43154af` and version 1.18.10 are byte-identical; exactly one LV2 `CPMAddPackage`, fetched under `PME_FETCH_ALL_DEPS OR (PME_BUILD_PLUGINS AND Linux)`. The default build configures, builds and passes ctest; `-DPME_FETCH_ALL_DEPS=ON` still configures; macOS and Windows plug-in builds skip `plugin/lv2/`.
3. Every D1 test passes unchanged (`ctest -R "^D1-"` in `linux-rt-safety` with `PME_BUILD_PLUGINS=ON`); no D1 test was edited.

**Real-time and threads (blocking if violated)**

4. Read every function reachable from LV2 `run` and `work_response`: no allocation, lock, system call, exception, logging or file I/O; `noexcept`; buffers preallocated in `instantiate`/`activate`. `run` executes under `ScopedRealtimeThread` in the rt-safety build and the `rt`-labelled tests do not abort; add a case if a path (patch:Set, instrument job, tick scheduling, control-port re-send) is not covered.
5. No controller main-thread function (`activate`, `deactivate`, `applyState`, `captureState`, `mainThreadTick`, `setInstrumentReference`) is reachable from `run` or `work_response`. Non-audio calls (instantiate/activate/deactivate, save/restore, `work`) are serialised by one mutex that the audio thread never takes; the parameter SPSC queue has a single producer at a time.
6. The worker job is a fixed-size, trivially copyable value with no pointer into audio-thread memory; long instrument paths are rejected, not truncated silently; at most one tick is outstanding; the plug-in instantiates and runs without `work:schedule` as documented.

**Behaviour**

7. D3-R1: MIDI goes through `EventTranslator::midi` (D1 table unchanged); structural `patch:Set` → `paramEvent` at its frame; continuous control ports → `paramEvent` at offset 0 only when changed, and all re-sent after activate and restore; events non-decreasing; stereo output; silence with the stub; `InvalidArgument` fails activation.
8. D3-R2 Turtle: generated at build time from `ids.hpp` and `pme::paramRegistry()`; no hand-written registry content checked in; every registry entry exactly once with the registry's min, max and default; continuous entries as `lv2:ControlPort` inputs, structural as `patch:writable` `lv2:Parameter`s, the instrument as an `atom:Path` parameter; port indices from one shared header; `state:interface` and `work:interface` listed as extension data; `urid:map` required. Independently compare the generated `.ttl` against `params.json` with a scratch script.
9. D3-R2 state: one `atom:String` property holding D1's JSON byte for byte; save → restore → save byte-identical; newer schema and garbage rejected without changing the plug-in; unknown ids ignored. Independent check: restore 200 random or truncated strings in scratch; it must never crash or accept garbage silently.
10. D3-R2 worker: instrument and tick jobs run on a non-audio thread; `work` calls `setInstrumentReference` and `mainThreadTick` under the mutex; the D6 hook is marked; structural changes are not routed through a second recompilation path.
11. Identifiers (D-3): the plug-in URI, bundle name and binary name appear only in ids.hpp (`git grep` the literal strings across the repository, including the generated-file templates and CMake); docs/dev/lv2.md says they are provisional.

**Tests and CI**

12. D3-T1: run lv2lint yourself on the built bundle; record version, output and any disabled checks; no error-level check disabled.
13. D3-T2: run the lilv host yourself; the scenario matches the brief (note on 100, CC64 1000, control change in block 3, structural patch:Set 2000, instrument 3000, note off 24000, CC64 0 at 30000); finite and zero output with the stub; worker ran; state byte-identical. Run it under `linux-asan` (or the sanitizer preset available) without leaks.
14. `PME_REQUIRE_LV2_TOOLS`: without the tools the tests skip, not fail, and the D1 `plugin (linux)` job still builds `plugin/`; with the option ON a missing tool fails configuration. The `lv2 (linux)` job sets it ON, prints the apt package versions, pins actions by SHA, and runs clang-tidy over `plugin/lv2/`. If lv2lint was built from source, the commit is a full SHA.

**Quality**

15. clang-format and clang-tidy clean on `plugin/lv2/`; D3 tests pass in `linux-debug` and `linux-rt-safety` with GCC and Clang; docs/dev/lv2.md covers every item the implementer brief lists (port table, index policy, parameter mapping, state, worker and D6 hook, threads, identifiers, what does not apply, build and test commands).

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For D3: D3-T1 and D3-T2 run on Linux only (the card scopes LV2 to Linux hosts); on macOS and Windows the `plugin` jobs and default builds must stay green. Nothing under engine/src (coverage gate and baselines do not apply). There is no pull request; the orchestrator records your verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D3`, `role` `code-reviewer`, `model` `sonnet`; `requirements` D3-R1 and D3-R2 as verified; `tests` D3-T1 and D3-T2 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
