# Brief: D1 CLAP plug-in shell — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/D1-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the native CLAP instrument that owns the engine, maps host events and parameters, and saves state.

D1 runs against the **stub engine** (silence; `Engine::prepare` returns `NotImplemented` after validating its argument), so that the shell is ready when the engine is (Track D introduction). D6 later swaps in the real engine.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Real-time rules, Code style, Testing policy, CI, Licensing); the Track D introduction and the phase card **D1 CLAP plug-in shell**; for context **D2**, **D3** (they reuse your controller layer), **D6** and **C5** (C5-R5).
- SPEC and PLAN sections:
  - SPEC-API — docs/plan/engine-specification.md, "Engine interfaces" (real-time interface, event addressing, events table, parameter classes, diagnostics)
  - PLAN-STACK — docs/plan/implementation-plan.md, "Technology stack"
  - PLAN-ARCH — docs/plan/implementation-plan.md, "Software architecture"
- SPEC-EXEC — docs/plan/engine-specification.md, "Execution order" ("Threading contract", "Instrument lifetime") — the engine's threading rules your shell must respect.
- engine/include/pme/engine.hpp, events.hpp, params.hpp, diagnostics.hpp at **interfaces-v2**; params.json; docs/dev/interfaces.md.
- docs/adr/ADR-0007-host-thread-pool-split.md (**D1-R6 scope**), ADR-0005 (nlohmann/json), ADR-0003 (dependency pinning), ADR-0006 (Status returns).
- docs/dev/build.md, docs/dev/ci.md, docs/dev/testing.md, docs/dev/rtcheck.md.
- The CLAP headers at the pinned commit (`clap` 1.2.10 in cmake/dependencies.cmake): `entry.h`, `factory/plugin-factory.h`, `plugin.h`, `ext/audio-ports.h`, `ext/audio-ports-config.h`, `ext/note-ports.h`, `ext/params.h`, `ext/state.h`, `ext/latency.h`, `ext/tail.h`, `ext/voice-info.h`, `ext/thread-pool.h`, `ext/render.h`.
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs: **G0** passed (docs/gates/G0.md, evaluated at 15a1a43): P0.1 (CMake skeleton, CLAP pinned under `PME_FETCH_ALL_DEPS`, `plugin/clap/` empty), P0.2 (CI, `pme_rtcheck` with `ScopedRealtimeThread`), P0.3 (`pme_add_doctest`, test conventions), P0.4 and ICR-0001 (headers, stub engine, parameter registry). Branch `ccr-5ef35d73-oqb6fd`; no pull requests.
- Frozen interfaces: **interfaces-v2** (ICR-0001: `setInstrument` non-audio only, `collectRetired`, registry without id 1010, `Pluck` semantics). Your worktree must start from a commit that contains the ICR-0001 application; if `interfacesVersionMajor` in engine/include/pme/version.hpp is 1, stop and report.
- Decision D-3 (product name and plug-in identifiers) is **open**: use the internal codename `pme` and provisional identifiers, defined in exactly one header so D2 can replace them (see D1-R1).

## Concurrent work: P0.5, V1, A1 and D1 run at the same time

Four phases are implemented in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. Edits must not overlap. This table is identical in every brief of the four phases.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/osc/**`, `engine/tests/osc/**`, `docs/dev/osc.md` | A1 implementer | read only |
| In `engine/tests/interfaces/test_stubs.cpp`: the `SUBCASE("compileOscillator")` block only. In `docs/dev/interfaces.md`: the word `osc` in the first sentence of "Stubs (P0.4-R3)" only | A1 implementer | read only |
| `engine/tests/oracles/**` (new); in `engine/tests/oracles.md`: the status and test-location cells of rows ORC-01 and ORC-02 only | A1 test-author | read only |
| `reference/**`, `tools/reference-cli/**`, `tools/tests/test_p0_5_*.py`, `docs/dev/reference-solver.md` | P0.5 | read only |
| `pyproject.toml`, `uv.lock` | P0.5 | read only |
| `tools/trace/**`, `tools/tests/test_v1_*.py`, `docs/dev/traceability.md`, `.github/workflows/nightly.yml`, `tools/ci/**` except `required_checks.json` | V1 | read only |
| P0.3 follow-ups: `cmake/pme_testing.cmake`, `engine/tests/framework/**`, `engine/tests/rtcheck/CMakeLists.txt`, `tools/analysis/**`, `tools/bench/**`, `tools/tests/test_p0_3_*.py`, `docs/dev/testing.md`, `docs/dev/signal-analysis.md` | V1 | read only |
| `# Covers:` / `// Covers:` comment lines added to test files of completed phases P0.1 to P0.3 (comments only) | V1 | read only |
| `plugin/**` (`plugin/CMakeLists.txt`, `plugin/clap/`, `plugin/common/`), `docs/dev/clap-shell.md` | D1 | read only |
| `cmake/dependencies.cmake`, `docs/dev/build.md` | D1 | read only |
| `.github/workflows/pr.yml` | V1 owns every existing job; V1, D1 and P0.5 each **append one new job in a delimited block at the end of the file** (`traceability`, `plugin`, `reference-python`) | nobody else edits existing jobs |
| `tools/ci/required_checks.json` | each phase that adds a pr.yml job appends its own matrix-expanded job names at the end of the array | — |
| `CONTRIBUTING.md` | each phase that adds a pr.yml job appends its own rows at the end of the required-checks table | — |
| `docs/dev/ci.md` | V1; D1 and P0.5 each append one delimited subsection at the end of the file describing their job | — |
| Root `CMakeLists.txt` | nobody above the current last line; A1 implementer, A1 test-author, P0.5 and D1 each **append one delimited block at the end** | — |
| `CMakePresets.json`, `cmake/pme_module.cmake`, `cmake/compiler_options.cmake`, `cmake/sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody | read only |
| `engine/include/pme/**`, the other `engine/src/*/` stubs, `engine/tests/interfaces/**` (except the A1 row above), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2; changes only by ICR) | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

When two phases append at the same place (end of the root `CMakeLists.txt`, end of pr.yml, end of a JSON array or table), the orchestrator keeps both blocks when merging. Keep your blocks self-contained so that order does not matter.

**Your first commit (separate, so the orchestrator can merge it early): `cmake/dependencies.cmake` gating.** Replace the single `if(PME_FETCH_ALL_DEPS)` block so that:

- `option(PME_BUILD_PLUGINS "Build the plug-in shells (D1+)" OFF)` and `option(PME_BUILD_PYTHON_BINDINGS "Build the nanobind Python modules (P0.5+)" OFF)` are declared near the top of the file;
- nanobind is fetched when `PME_FETCH_ALL_DEPS OR PME_BUILD_PYTHON_BINDINGS` (update its comment: first built by P0.5);
- CLAP is fetched when `PME_FETCH_ALL_DEPS OR PME_BUILD_PLUGINS`;
- clap-wrapper, the VST 3 SDK and LV2 stay under `PME_FETCH_ALL_DEPS` only;
- every pin (URL, commit, version) is unchanged; one `CPMAddPackage` per package, never duplicated.

Document both options in docs/dev/build.md (Options paragraph). Commit message `D1: fetch CLAP and nanobind per consumer (PME_BUILD_PLUGINS, PME_BUILD_PYTHON_BINDINGS)`. P0.5 codes against `PME_BUILD_PYTHON_BINDINGS` concurrently; do not rename it.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- D1: CLAP plug-in shell ----
if(PME_BUILD_PLUGINS)
  add_subdirectory(plugin)
endif()
# ---- end D1 ----
```

`plugin/CMakeLists.txt` (yours) adds `common` and `clap`; D2 and D3 will add their directories later.

**pr.yml job.** Append one job in a delimited block at the end of `.github/workflows/pr.yml`:

```yaml
# ---- D1: CLAP plug-in shell ----
  plugin:
    name: plugin (${{ matrix.system }})
    …            # matrix system: [linux, macos, windows]
# ---- end D1 ----
```

On each system: configure `<system>-rt-safety` with `-DPME_BUILD_PLUGINS=ON`, build, run the D1 tests (`ctest --preset <system>-rt-safety -R "^D1-"` plus anything labelled for the plug-in), run clap-validator on the built `.clap` (D1-T1), and upload JUnit as `junit-plugin-<system>`; on Linux also run clang-tidy over `plugin/` with `tools/ci/run_clang_tidy.py` and this build's compile database (the lint job does not build plug-ins). clap-validator comes from its GitHub release: pin an exact release tag and verify the downloaded archive's SHA-256 in the workflow (read the tag and checksum from the release page yourself; never write a value you have not just read; if unreachable, stop and report). Append `plugin (linux)`, `plugin (macos)`, `plugin (windows)` at the end of the array in `tools/ci/required_checks.json` and one row after the last row of the CONTRIBUTING.md required-checks table (`test_p0_2_r7` checks both), and one delimited subsection at the end of docs/dev/ci.md. Do not edit any other job, and do not edit V1's `traceability` job: the orchestrator adds `plugin` to its `needs` when merging.

## Scope

- May edit (create or change): `plugin/CMakeLists.txt`, `plugin/clap/**`, `plugin/common/**` (the controller layer D3 reuses; listed in phases.yaml for D1), `docs/dev/clap-shell.md`, `cmake/dependencies.cmake` and the Options paragraph of `docs/dev/build.md` (first commit above), your root `CMakeLists.txt` block, your pr.yml block, your entries in `tools/ci/required_checks.json` and CONTRIBUTING.md, your subsection of docs/dev/ci.md.
- Read only: everything else (in particular `engine/**`: you call the engine, you never change it).
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D1-R1 Plug-in factory and descriptor: instrument, stereo output by default, up to 8 output channels.
2. D1-R2 Note ports accepting CLAP note events and MIDI (including CC64 sustain, CC66 sostenuto, CC67 soft); half-pedal from continuous CC64.
3. D1-R3 Parameters from the P0.4 registry: continuous parameters automatable and sample-accurate; structural parameters non-automatable and marked as requiring recompilation.
4. D1-R4 State save and load: instrument reference, parameter overrides, schema and plug-in versions.
5. D1-R5 Latency 0; tail reported as infinite while any string sounds; voice-info reporting keys as voices.
6. D1-R6 Thread-pool extension routed to the C5 host-pool adapter; fallback to the engine's own pool.
7. D1-R7 Offline (render) mode passes host bounce requests to the engine unchanged.

### Architecture

- **`plugin/common/` — controller layer, format-independent, no CLAP types in its public headers.** It owns one `pme::Engine` through a small adapter interface (`EngineAdapter`: `prepare`, `setInstrument`, `collectRetired`, `process`, `reset`, `popDiagnostic`) with the real `pme::Engine` behind it, so tests can substitute a recording fake. It holds: the event translator (host-neutral note, MIDI and parameter input → `pme::Event` with sample offsets, sorted by offset, into a preallocated buffer), the parameter model built from `pme::paramRegistry()`, the state serialiser, the task runner of ADR-0007, and the provisional identifiers (`plugin/common/include/…/ids.hpp`: plug-in id `pme.instrument` (provisional, D-3), name "pme", vendor "pme project", version from the CMake project version). D3 builds its LV2 adapter on this layer.
- **`plugin/clap/` — the CLAP adapter**: `clap_entry`, the plug-in factory, the plug-in object and each extension, translating CLAP structures to and from the controller layer. Output: a `.clap` module (MODULE library with suffix `.clap`; a bundle on macOS with a minimal Info.plist), built with `pme_build_options`, warnings as errors, the CLAP headers as a `SYSTEM` include.
- **Threads** (CLAP thread rules plus SPEC-EXEC): `activate` / `deactivate` and state on the main thread; `process`, `start_processing`, `reset` on the audio thread. `Engine::prepare` runs in `activate` (main thread); `setInstrument` and `collectRetired` only from the main thread (call `collectRetired` from `on_main_thread` or a host timer; ICR-0001). The audio thread never allocates, locks or throws: mark it with `pme::rtcheck::ScopedRealtimeThread` inside `process` in the rt-safety build.
- **Stub engine:** `prepare` returns `NotImplemented` for a valid configuration and `InvalidArgument` for an invalid one. Treat `NotImplemented` as "engine unavailable": activation succeeds and `process` outputs silence; treat `InvalidArgument` as an activation failure. Log nothing on the audio thread.

### How to meet each requirement

**D1-R1.** Factory with one descriptor: features `instrument`, `stereo`, `synthesizer`. `clap.audio-ports`: no inputs, one main output port, stereo by default. `clap.audio-ports-config`: configurations with 2, 4, 6 and 8 channels (one port); the selected count becomes `EngineConfig::channels` at the next `activate`. Provisional ids per above; docs/dev/clap-shell.md says D2 freezes them under D-3.

**D1-R2.** `clap.note-ports`: one input port, dialects CLAP and MIDI, preferred CLAP. Translation (all with the event's sample offset):

| Input | Engine event |
| --- | --- |
| CLAP note on (key k, velocity v) / MIDI note on (velocity > 0) | `NoteOn{key = k, velocity}` (v as given, MIDI velocity / 127); key −1 or out of 0..127: ignored |
| CLAP note off / MIDI note off or note on with velocity 0 | `NoteOff{key, releaseVelocity}` |
| CLAP note choke | `NoteOff{key, releaseVelocity = 0}` |
| MIDI CC64 value c | `Pedal{Sustain, c / 127}` — continuous, so half-pedal works |
| MIDI CC66 value c | `Pedal{Sostenuto, c ≥ 64 ? 1 : 0}` |
| MIDI CC67 value c | `Pedal{Soft, c / 127}` |
| MIDI polyphonic aftertouch (key, p) | `KeyDepth{key, p / 127}` (MAY; document if omitted) |
| Other MIDI | ignored |

Emit a CLAP `note end` event (`CLAP_EVENT_NOTE_END`) only when the engine can tell a voice ended; with the stub engine never. Events are passed to `Engine::process` as one `EventList` per host block, sorted by non-decreasing offset (SPEC-EXEC step 2.1 precondition); if the host block is split, offsets are re-based.

**D1-R3.** `clap.params`: one CLAP parameter per registry entry (`pme::paramRegistry()`), CLAP id = registry id, name, module path from the dotted name, min, max, default from the registry. Continuous: `CLAP_PARAM_IS_AUTOMATABLE`, delivered as `pme::Param{id, value}` events at their exact sample offsets (CLAP param-value events in `process`, and `flush` outside processing). Structural: without `CLAP_PARAM_IS_AUTOMATABLE`. CLAP has no "requires recompilation" flag, so mark them in the module path (`structural/…`) and in docs/dev/clap-shell.md, and deliver a change as a `Param` event like any other (SPEC-API: "a request to recompile for a structural one"; the engine does the recompilation). Values outside [min, max] are clamped. `value_to_text` shows the unit from the registry; `text_to_value` parses it.

**D1-R4.** `clap.state`: JSON written with nlohmann/json (ADR-0005 permits it in non-real-time code; state runs on the main thread and the engine targets stay free of it — the P0.3 isolation check still covers engine modules). Contents: `state_schema_version` (integer, 1), `plugin_version` (string), `interfaces_version` (`pme::interfacesVersionMajor`), `instrument` (a reference: path or id string, empty until B1 provides loading), `params` (only values that differ from the registry default, keyed by id, as exact doubles). Loading accepts the same schema version, ignores unknown ids with a note in the log, clamps values, and rejects a newer schema version cleanly (returns false). Never allocate on the audio thread when state changes: changed parameters reach the audio thread through a preallocated lock-free single-producer single-consumer queue (Real-time rule 3).

**D1-R5.** `clap.latency`: 0. `clap.tail`: `UINT32_MAX` (infinite) while any string sounds, else 0; with the stub engine "sounds" is "a note is held or a sustain pedal value > 0 since the last silence" (document; D6 replaces it with the engine's group-energy diagnostics). `clap.voice-info`: voice count and capacity = number of keys (128 for the stub; the instrument's note-group count once B1/C1 exist), flag `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES` off.

**D1-R6 (ADR-0007, Proposed).** Shell side only: query `clap.thread-pool` on the host; implement the plug-in's `clap_plugin_thread_pool.exec(task_index)`; a `TaskRunner` in `plugin/common/` with `run(task_count, fn)` that uses `request_exec` when the host offers it and runs the tasks inline otherwise; when the host has no pool, set `EngineConfig::threads` from a setting (default: the number of hardware threads minus one, at least 1, at most 8) so the engine uses its own pool. Nothing in the stub engine calls the runner; the engine-side adapter is C5-R5 and the routing is D6. Report D1-R6 as `deferred` with ADR-0007 and describe the delivered shell side in `notes`; document the runner's contract in docs/dev/clap-shell.md for C5.

**D1-R7.** `clap.render`: accept `CLAP_RENDER_REALTIME` and `CLAP_RENDER_OFFLINE`; the mode changes nothing in what the engine receives (same events, same offsets, same block lengths) and does not touch `EngineConfig::referenceMode` (that is a separate user option, not a bounce flag). Record the mode for D6's bounce-equals-real-time test.

## Tests that must pass

- **D1-T1** clap-validator passes with the stub engine;
- **D1-T2** state save and load round-trips exactly;
- **D1-T3** automation events arrive at the engine at their exact sample offsets.

### How each test is built

**D1-T1.** In the `plugin` job on all three systems: `clap-validator validate <path to .clap>` with all tests enabled; exit status 0. Also a ctest test `D1-T1 clap-validator passes with the stub engine` that runs the validator when its path is given through a CMake cache variable or environment variable (skipped otherwise), so a developer can run it locally. Record the validator version and the list of tests it ran in the report.

**D1-T2** (doctest, `unit`): through the CLAP state extension with in-memory `clap_ostream`/`clap_istream` implementations: set a non-default value on every continuous and structural parameter (including extreme doubles such as min, max and values with full 17-digit precision) and an instrument reference; save; load into a fresh plug-in instance; save again: the two byte streams are identical and every value compares equal with `==`. Also: a stream with a newer `state_schema_version` is rejected; an unknown parameter id is ignored.

**D1-T3** (doctest, `unit` and `rt`): with the recording fake `EngineAdapter`, drive the CLAP `process` call with a block of 512 frames containing param-value events for continuous parameters at offsets 0, 1, 255, 511 (and two events at the same offset), CLAP note on/off and MIDI CC64/66/67 at known offsets; the fake records the `EventList`: every event arrives once, with the exact offset, value and type, in non-decreasing offset order. Repeat with a host block split into sub-calls of 1, 7 and 504 frames. The `process` path runs under `ScopedRealtimeThread` and must not abort in the rt-safety build.

Additional `D1-R…` unit tests: the MIDI translation table (D1-R2), clamping and value-to-text (D1-R3), tail and voice-info (D1-R5), the task runner with a mock host pool running each index exactly once (D1-R6), render mode (D1-R7), activation with the stub engine succeeding and `InvalidArgument` failing.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to D1:

- Module note: docs/dev/clap-shell.md — SPEC-API sections used (events, parameter classes, real-time interface, instrument lifetime), the controller layer for D3, the translation table, the state format, the provisional identifiers (D-3), the task-runner contract for C5 (ADR-0007), and how to build and validate (`-DPME_BUILD_PLUGINS=ON`).
- RT-safety: the audio-thread path (`process`, event translation, parameter queue) is tested under `ScopedRealtimeThread` with the `rt` label in the `<system>-rt-safety` build.
- Coverage gate and performance baselines: D1 adds nothing under engine/src; they do not apply. Say so in `notes`.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `D1: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: configure and build `linux-debug` and `linux-rt-safety` with `-DPME_BUILD_PLUGINS=ON` (GCC and Clang); `ctest -R "^D1-"`; clap-validator locally on the `.clap`; `clang-format --dry-run --Werror` and `clang-tidy` on `plugin/`; `uv run pytest tools/tests/test_p0_2_r7.py tools/tests/test_p0_3_r1.py`; the default build (`PME_BUILD_PLUGINS=OFF`) still configures, builds and passes `ctest`.
- If the stub engine or the interface prevents a requirement (for example something D1 needs is missing from interfaces-v2), stop that item and report it; the planner writes an ICR or ADR. Do not edit engine headers.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, clap-validator version and result, and the pinned clap-validator tag and checksum with where you read them.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D1`, `role` `implementer`, `model` `sonnet`; `requirements` D1-R1 to D1-R7 (D1-R6 `deferred`, ADR-0007); `tests` D1-T1 to D1-T3; every created or changed path in `outputs`; `pull_requests` empty; `adrs` ADR-0003, ADR-0005, ADR-0007.
