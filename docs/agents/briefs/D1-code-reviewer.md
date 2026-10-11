# Brief: D1 CLAP plug-in shell — code-reviewer

Model: sonnet
Branch: review the D1 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the native CLAP instrument that owns the engine, maps host events and parameters, and saves state.

Your job: decide whether the D1 implementer's work meets the D1 card, SPEC-API, the Software guidelines (Real-time rules above all), ADR-0007 and docs/agents/briefs/D1-implementer.md, and return a verdict. Every plug-in format will sit on this shell, so check thread rules and event timing yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **D1 CLAP plug-in shell**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-API — docs/plan/engine-specification.md, "Engine interfaces"
  - PLAN-STACK — docs/plan/implementation-plan.md, "Technology stack"
  - PLAN-ARCH — docs/plan/implementation-plan.md, "Software architecture"
- SPEC-EXEC "Threading contract" and "Instrument lifetime" (docs/plan/engine-specification.md, "Execution order").
- docs/agents/briefs/D1-implementer.md (ownership table, first-commit dependency gating, architecture, translation table, state format, test setups, pr.yml job).
- docs/adr/ADR-0007-host-thread-pool-split.md, ADR-0005, ADR-0003.
- `.claude/agents/code-reviewer.md` — your checklist.
- The implementer's handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `D1: ` (`git log --grep '^D1: ' --name-only`).
- CI evidence: given by the orchestrator after merging (the `plugin (linux|macos|windows)` jobs). Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 (ICR-0001).

## Scope

- May edit: test sources under `plugin/clap/tests/` and `plugin/common/tests/` (or wherever the D1 tests live) — only to add missing or independent tests for D1-T1 to T3 or D1-R1 to R7. Commit with messages starting `D1: `. Do not push.
- Read only: everything else. You MUST NOT change implementation, workflows, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D1-R1 Plug-in factory and descriptor: instrument, stereo output by default, up to 8 output channels.
2. D1-R2 Note ports accepting CLAP note events and MIDI (including CC64 sustain, CC66 sostenuto, CC67 soft); half-pedal from continuous CC64.
3. D1-R3 Parameters from the P0.4 registry: continuous parameters automatable and sample-accurate; structural parameters non-automatable and marked as requiring recompilation.
4. D1-R4 State save and load: instrument reference, parameter overrides, schema and plug-in versions.
5. D1-R5 Latency 0; tail reported as infinite while any string sounds; voice-info reporting keys as voices.
6. D1-R6 Thread-pool extension routed to the C5 host-pool adapter; fallback to the engine's own pool.
7. D1-R7 Offline (render) mode passes host bounce requests to the engine unchanged.

## Tests that must pass

- **D1-T1** clap-validator passes with the stub engine;
- **D1-T2** state save and load round-trips exactly;
- **D1-T3** automation events arrive at the engine at their exact sample offsets.

## Review checklist for D1

**Scope and process**

1. D1 commits touch only D1's paths: `plugin/**`, docs/dev/clap-shell.md, `cmake/dependencies.cmake` and the Options paragraph of docs/dev/build.md, one root `CMakeLists.txt` block, one pr.yml block at the end, appended entries in `required_checks.json` and CONTRIBUTING.md, one docs/dev/ci.md subsection. Nothing under `engine/`. `uv run pytest tools/tests/test_p0_2_r7.py` passes.
2. The dependencies commit is separate and first; every pin (URL, commit, version) is byte-identical to before; nanobind fetched under `PME_FETCH_ALL_DEPS OR PME_BUILD_PYTHON_BINDINGS`, CLAP under `PME_FETCH_ALL_DEPS OR PME_BUILD_PLUGINS`, the rest unchanged; no `CPMAddPackage` duplicated. The default build (both options OFF) configures, builds and passes ctest; `-DPME_FETCH_ALL_DEPS=ON` still configures.

**Real-time rules (blocking if violated)**

3. Read every function reachable from the CLAP `process` callback: no allocation, lock, system call, exception, logging or file I/O; `noexcept`; event buffers preallocated in `activate`; parameter and state changes reach the audio thread through a lock-free SPSC queue. The D1-T3 test runs under `ScopedRealtimeThread` in `linux-rt-safety` without aborting; add a case if any path (MIDI translation, flush, split blocks) is not covered.
4. Thread placement: `Engine::prepare` in `activate` (main thread); `setInstrument` and `collectRetired` only on the main thread (ICR-0001); nothing calls them from `process`.
5. Stub handling: activation succeeds when `prepare` returns `NotImplemented`, fails on `InvalidArgument`; `process` outputs silence.

**Behaviour**

6. D1-R1: descriptor features, stereo default, audio-ports-config with 2, 4, 6, 8 channels; provisional ids in one header, documented as pending D-3.
7. D1-R2: the translation table, row by row, including CC64 continuous (half-pedal), CC66 threshold, CC67, note choke, MIDI note on with velocity 0; events sorted by offset; split blocks re-base offsets.
8. D1-R3: one CLAP parameter per registry entry with the registry's id, range and default; continuous automatable and sample-accurate; structural not automatable and marked; clamping; text conversion.
9. D1-R4 and D1-T2: state JSON fields as briefed; byte-identical save–load–save; exact doubles; newer schema rejected; unknown ids ignored. Independent check: fuzz the loader with 200 random or truncated streams in scratch; it must never crash or accept garbage silently.
10. D1-R5: latency 0; tail logic as documented; voice-info.
11. D1-R6: only the shell side (ADR-0007); task runner runs each index exactly once with a mock host; fallback sets `EngineConfig::threads`; the report marks D1-R6 `deferred` citing ADR-0007.
12. D1-R7: render mode recorded; engine input identical in both modes.
13. D1-T1: run clap-validator yourself on the built `.clap` (Linux at least); record version and results. The workflow pins clap-validator by exact tag and verifies its SHA-256.

**Quality**

14. clang-format and clang-tidy clean on `plugin/`; D1 tests pass in `linux-debug` and `linux-rt-safety` (with `-DPME_BUILD_PLUGINS=ON`) with GCC and Clang; docs/dev/clap-shell.md covers the items the brief lists, including the task-runner contract for C5.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For D1: nothing under engine/src (coverage gate and baselines do not apply). D1-R6's deferral needs ADR-0007 Accepted before D1 is marked complete. There is no pull request; the orchestrator records your verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D1`, `role` `code-reviewer`, `model` `sonnet`; `requirements` D1-R1 to D1-R7 as verified; `tests` D1-T1 to D1-T3 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
