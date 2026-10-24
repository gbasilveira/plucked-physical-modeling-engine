# Brief: D3 LV2 plug-in — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/D3-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

a native LV2 build for Linux hosts, sharing the CLAP shell's controller layer.

D3 runs against the **stub engine** (silence; `Engine::prepare` returns `NotImplemented` for a valid configuration), exactly as D1 did. D6 later swaps in the real engine for every format at once.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Real-time rules, Code style, Testing policy, CI, Licensing); the Track D introduction and the phase card **D3 LV2 plug-in**; for context **D1**, **D2** and **D6**.
- SPEC and PLAN sections:
  - PLAN-STACK — docs/plan/implementation-plan.md, "Technology stack" (row "Other formats": "LV2 on Linux")
- Also, because the adapter sits on the D1 controller: SPEC-API — docs/plan/engine-specification.md, "Engine interfaces" ("Events", "Parameter classes"); SPEC-EXEC — "Execution order", "Threading contract" and "Instrument lifetime" (which engine calls are non-audio).
- **docs/dev/clap-shell.md** in full, above all "Controller layer (for D3)", "Provisional identifiers (D-3)", "State (D1-R4)" and "Threads". Then `plugin/common/include/pme/plugin/*.hpp` (controller, event translator, parameter model, state, SPSC queue, task runner, engine adapter, ids) and `plugin/common/tests/fake_engine.hpp`.
- docs/adr/ADR-0003 (dependency pinning), ADR-0005 (nlohmann/json, non-real-time code only), ADR-0007 (host thread pool; LV2 has no host pool, so the controller's own-pool fallback applies).
- docs/dev/build.md, docs/dev/ci.md (including "D1: the `plugin` job"), docs/dev/testing.md, docs/dev/rtcheck.md.
- The LV2 headers and specification bundles at the pinned commit (LV2 1.18.10, `0bcde338db1c63bbc503b4d1f6d7b55ed43154af`, cmake/dependencies.cmake): core (`lv2.h`, threading classes), `atom`, `midi`, `urid`, `patch`, `state`, `worker`, `options`, `buf-size`, `units`, `log`.
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs: **D1** complete (docs/status/phase-status.md): `plugin/common/` (controller layer, namespace `pme::plugin`, target `pme_plugin_common`), `plugin/clap/`, docs/dev/clap-shell.md, `PME_BUILD_PLUGINS` in cmake/dependencies.cmake, the `plugin` job in pr.yml. Reviewer approved; pr.yml run 38153781819 green on all 16 jobs at 2570bbc. No pull requests (single session branch `ccr-5ef35d73-oqb6fd`).
- Frozen interfaces: **interfaces-v2** (300b995; ICR-0001). If `interfacesVersionMajor` in engine/include/pme/version.hpp is not 2, stop and report.
- Decision **D-3** (product name and plug-in identifiers) is **open**. Every identifier is provisional and lives in **one place**, `plugin/common/include/pme/plugin/ids.hpp` (D1 rule: "Nothing else in the repository spells these strings"). Add the LV2 identifiers there and nowhere else (see D3-R2, "Identifiers"). D2 replaces them all when D-3 is answered.
- Open risks inherited from D1 (not yours to fix; do not make them worse): the parameter SPSC queue (capacity 1024) and the carry buffer (256) drop changes silently when full; the tail heuristic; wildcard note-off ignored; the state parser does not range-check `interfaces_version`.

## Concurrent work: D3 runs at the same time as B1, P0.5 and possibly A4, A5, A6, plus review fixes on A2, A3, V1

Each phase works in its own git worktree; the orchestrator merges the commits into `ccr-5ef35d73-oqb6fd`. Your edits must not overlap anyone else's. **Everything not listed under "May edit" below is read only for you**, in particular `engine/**`, `compiler/**`, `reference/**`, `instruments/**`, `tools/**` except your entries in `tools/ci/required_checks.json`, `pyproject.toml`, `uv.lock`, `plugin/clap/**`, and every existing job in pr.yml.

Shared files: you append **delimited blocks at the end** (other phases may append theirs at the same place; the orchestrator keeps both, so keep your blocks self-contained):

| File | Your block |
| --- | --- |
| `.github/workflows/pr.yml` | one job `lv2` at the end, between `# ---- D3: LV2 plug-in ----` and `# ---- end D3 ----` |
| `tools/ci/required_checks.json` | `"lv2 (linux)"` appended at the end of the array |
| `CONTRIBUTING.md` | one row after the last row of the required-checks table |
| `docs/dev/ci.md` | one subsection at the end between `<!-- ---- D3: lv2 job ---- -->` and `<!-- ---- end D3 ---- -->` |
| Root `CMakeLists.txt` | none expected (the D1 block already adds `plugin/` under `PME_BUILD_PLUGINS`). If you find you need one, append a single block `# ---- D3: LV2 plug-in ----` … `# ---- end D3 ----` at the end and change nothing above it |

Do not edit V1's `traceability` job: the orchestrator adds `lv2` to its `needs` when merging.

## Scope

- May edit (create or change):
  - `plugin/lv2/**` (replace `plugin/lv2/.gitkeep`) and `docs/dev/lv2.md` — the card's Outputs.
  - `plugin/CMakeLists.txt`: one guarded line adding `lv2` (Linux only), and the comment that names the subdirectories.
  - `plugin/common/include/pme/plugin/ids.hpp`: **append** the LV2 identifiers; change nothing that is there.
  - `plugin/common/**` otherwise: only additive, format-neutral changes that the LV2 adapter cannot do without (for example a const accessor). Every D1 test must still pass unchanged; list each such change in the report's `notes` with the reason. No LV2 types in `plugin/common/`.
  - `cmake/dependencies.cmake`: **only** to gate the already-pinned LV2 fetch — move the existing LV2 `CPMAddPackage` out of the `if(PME_FETCH_ALL_DEPS)` block into its own `if(PME_FETCH_ALL_DEPS OR (PME_BUILD_PLUGINS AND CMAKE_SYSTEM_NAME STREQUAL "Linux"))` block, with URL, commit and version byte-identical and the comment's "first built: D3" kept. One `CPMAddPackage` for LV2, never duplicated. In docs/dev/build.md change only the consumer cell of the LV2 row of the dependency table to say so.
  - Your blocks in the shared files above.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D3-R1 LV2 adapter in plugin/lv2 over the same controller layer as D1.
2. D3-R2 Turtle descriptors for ports, parameters and state; worker extension for recompilation.

### Architecture

- **`plugin/lv2/`** builds a shared library (MODULE, no `lib` prefix) inside an LV2 bundle directory `build/<preset>/plugin/lv2/<bundle>.lv2/` together with its `manifest.ttl` and plug-in `.ttl`. Built only when `PME_BUILD_PLUGINS` is ON **and** the system is Linux (the card: "for Linux hosts"); macOS and Windows builds skip the directory. Same build options as D1: `pme_build_options`, warnings as errors, LV2 headers as a `SYSTEM` include, position-independent code (plugin/CMakeLists.txt already sets it). Only `lv2_descriptor` is exported (hidden visibility for everything else).
- The adapter owns one `pme::plugin::Controller` with a `RealEngineAdapter`, and is constructible with any `std::unique_ptr<EngineAdapter>` so tests can inject the recording fake. It translates LV2 structures to and from the controller and contains no event-translation, clamping or state logic of its own: those are D1's and stay in `plugin/common/`.
- **Threads** (LV2 threading classes plus SPEC-EXEC): `instantiate`, `activate`, `deactivate`, `cleanup` (Instantiation class), state `save`/`restore`, and worker `work` are non-audio; `run` and `work_response` are audio. `Controller::activate` runs in LV2 `activate` with the host's sample rate and maximum block length (`buf-size:maxBlockLength` from `options:options` when given; otherwise a documented default — `Controller::render` already splits larger blocks). Every call to the controller's main-thread API (`activate`, `deactivate`, `applyState`, `captureState`, `mainThreadTick`, `setInstrumentReference`, …) is serialised by one `std::mutex` that **only non-audio threads take** (SPEC-EXEC: non-audio engine calls are serialised by the caller; the parameter queue has a single producer). `run` never takes it.
- The audio thread never allocates, locks, logs or throws. `run` marks itself with `pme::rtcheck::ScopedRealtimeThread` in the rt-safety build, as D1's `process` does. LV2 `log:log` may be used from non-audio functions only.
- Stub engine: as D1 — `NotImplemented` from `prepare` means "engine unavailable", activation succeeds and `run` writes silence; `InvalidArgument` is an activation failure.

### How to meet each requirement

**D3-R1 (adapter over the controller layer).** Per `run(n)`: `beginBlock(n)`; walk the input atom sequence in frame order — `midi:MidiEvent` → `events().midi(frame, status, data1, data2)` (the D1 translation table applies unchanged: notes, CC64 continuous, CC66, CC67, velocity-0 note off); `patch:Set` of a structural parameter → `paramEvent(frame, id, value)`; `patch:Set` of the instrument property → copy the path into a preallocated buffer and hand it to the worker (D3-R2); compare each continuous control port with its last seen value and call `paramEvent(0, id, value)` when it changed (bitwise), and for every port on the first `run` after `activate` or `restore`; then `render(n, {left, right})`. Output: stereo only (one left and one right `lv2:AudioPort`); D1-R1's 4/6/8-channel configurations are CLAP-only for now — say so in docs/dev/lv2.md. LV2 has no host thread pool: leave the controller's own-pool fallback (ADR-0007) in place. If the host offers an `lv2:freeWheeling` control port, MAY map it to `Controller::setRenderMode` (D1-R7 parity); document either way. No latency port (latency is 0). LV2 has no tail or voice-info extension: document that these D1-R5 items do not apply.

**D3-R2 (Turtle descriptors, state, worker).**

*Identifiers (D-3, provisional, one place).* Append to `ids.hpp`: the plug-in URI `https://pme.invalid/lv2/instrument` (the reserved `.invalid` domain makes the provisional status explicit and can never resolve to someone else's site), the bundle directory name `pme.lv2`, and the binary base name. The Turtle files and CMake obtain these values from ids.hpp (generated Turtle, below; CMake may read the header with `file(STRINGS … REGEX …)`): no other file spells them. docs/dev/lv2.md says D2 replaces them under D-3.

*Generated Turtle.* `manifest.ttl` and the plug-in `.ttl` are **generated at build time** by a small host tool in `plugin/lv2/` that links `pme_plugin_common` and writes them from `ids.hpp` and `pme::paramRegistry()`, so ports and parameters can never drift from the registry. Checked-in hand-written Turtle for registry content is not allowed. Contents:

| Element | Turtle |
| --- | --- |
| Plug-in | `a lv2:Plugin, lv2:InstrumentPlugin`; `doap:name`, `doap:license <https://spdx.org/licenses/MIT>`; `lv2:requiredFeature urid:map`; `lv2:optionalFeature` `work:schedule`, `options:options`, `log:log`, `lv2:hardRTCapable`; `lv2:extensionData state:interface, work:interface`; `patch:writable` for every structural parameter and the instrument property |
| Port 0 `in` | `atom:AtomPort`, `lv2:InputPort`, `atom:bufferType atom:Sequence`, `atom:supports midi:MidiEvent, patch:Message`, `lv2:designation lv2:control` |
| Port 1 `notify` | `atom:AtomPort`, `lv2:OutputPort`, `atom:Sequence`, `atom:supports patch:Message`, `lv2:designation lv2:control` (patch:Set replies to patch:Get and after restore) |
| Ports 2, 3 `out_l`, `out_r` | `lv2:AudioPort`, `lv2:OutputPort` |
| Ports 4 … | one `lv2:ControlPort` `lv2:InputPort` per **continuous** registry entry, in registry order: `lv2:symbol` from the dotted name (`.` → `_`), `lv2:name`, `lv2:minimum`, `lv2:maximum`, `lv2:default` from the registry; `units:unit` where the registry unit maps to an LV2 unit (dB, Hz, s, ms, cent), none otherwise |
| Structural parameters | one `lv2:Parameter` per **structural** registry entry, URI `<plug-in URI>#param.<dotted name>`, `rdfs:label`, `rdfs:range atom:Double`, `lv2:minimum`, `lv2:maximum`, `lv2:default`; not ports, so hosts do not automate them (SPEC-API "Parameter classes": structural = recompiled) |
| Instrument | one `lv2:Parameter` `<plug-in URI>#instrument`, `rdfs:range atom:Path` |

Port indices are a public contract: one header in `plugin/lv2/` defines them and both the adapter and the generator use it; a new registry entry may only be appended. Document the policy.

*State* (`state:interface`). `save` stores one property `<plug-in URI>#state`, type `atom:String`, flags `LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE`, value `serializeState(captureState())` — D1's JSON, byte for byte. `restore` retrieves it, `parseState` → `applyState`; a newer `state_schema_version` or unparsable text returns an LV2 state error and leaves the plug-in unchanged; unknown parameter ids are ignored (D1 behaviour). Continuous parameters are owned by the control ports: after `restore`, the next `run` re-sends every control port value (port values win over the JSON). If the instrument reference is a file path, MAY use `state:mapPath` for portability; with B1 not merged the reference is a plain string — document.

*Worker* (`work:interface`, `work:schedule`) — the LV2 plug-in's non-audio thread for everything SPEC-EXEC keeps off the audio thread: instrument (re)loading and recompilation, `collectRetired`, diagnostics. D3 delivers the shell side:
- `run` schedules a fixed-size, trivially copyable job (no pointers to audio-thread memory): **instrument** (path copied into the job, truncated paths rejected) and **tick** (about every 100 ms of rendered frames, at most one tick outstanding).
- `work` takes the mutex and runs the controller's non-audio path: instrument → `setInstrumentReference` (D6 adds compile and `EngineAdapter::setInstrument` to this same job; leave a clearly marked hook); tick → `mainThreadTick` (`collectRetired`, diagnostics). It responds with a small result.
- `work_response` (audio thread) only records the result and, for an instrument change, queues a `patch:Set` reply on `notify`.
- Structural parameter changes travel as `Param` events (D1 behaviour: "a request to recompile for a structural one"; the engine recompiles off the audio thread). Do not invent a second recompilation path in the shell.
- Without `work:schedule` the plug-in still instantiates; instrument changes from `run` are ignored and counted, and `mainThreadTick` runs in `deactivate` and `save`. Document.

## Tests that must pass

- **D3-T1** lv2lint passes;
- **D3-T2** a headless LV2 host loads the plug-in and renders a test scenario in CI.

### How each test is built

Test names start with the ID (`D3-T1 lv2lint passes`, `D3-T2 a headless LV2 host loads the plug-in and renders a test scenario`); unit tests are named `D3-R1 …`, `D3-R2 …`.

**Tool discovery.** A CMake option `PME_REQUIRE_LV2_TOOLS` (default OFF). When lv2lint or lilv (via pkg-config `lilv-0`) is missing, the dependent test is skipped (`SKIP_RETURN_CODE`) — unless the option is ON, then configuration fails with a clear message. The CI job sets it ON, so a skip can never pass in CI. The D1 `plugin (linux)` job also builds `plugin/` with `PME_BUILD_PLUGINS=ON` and without these tools: your code must build there and its tests that need the tools must skip, not fail.

**D3-T1.** ctest test running `lv2lint` on the built bundle with `LV2_PATH` set to the bundle's parent plus the LV2 specification bundles (from the pinned LV2 source or the system `lv2-dev`; say which), plug-in URI from ids.hpp; exit status 0. Any check you disable (`-d`/`-w` options) is listed in docs/dev/lv2.md with the reason; an error-level check may not be disabled. Record lv2lint's version and full output in the report.

**D3-T2.** A small headless host in `plugin/lv2/tests/` using **lilv** (system package, test-only, never linked into the plug-in or shipped; ISC licence — record the package name and version): discover the bundle through `LV2_PATH`, find the plug-in by URI, instantiate at 48 kHz with `urid:map`, `options:options` (`maxBlockLength` 512) and a `work:schedule` implementation (a real worker thread), connect every port, activate, then render this scenario in blocks of 512 frames for 1 s: MIDI note on key 60 velocity 100 at frame 100; CC64 127 at frame 1000; a control-port change of a continuous parameter in block 3; a `patch:Set` of one structural parameter at frame 2000; a `patch:Set` of the instrument at frame 3000; note off at frame 24000; CC64 0 at frame 30000. Pass when: every `run` returns, every output sample is finite and (stub engine) exactly 0, the worker executed the instrument job and at least one tick on a non-audio thread, and a `save` → `restore` into a second instance → `save` gives byte-identical state strings. Deactivate and clean up without leaks under the sanitizer presets.

**Unit tests** (doctest, labels `unit`, and `rt` for the audio-thread path; in the `<system>-rt-safety` build `run` executes under `ScopedRealtimeThread` and must not abort). Drive the adapter's `run` directly (no lilv) with the recording fake `EngineAdapter` and hand-built atom sequences:
- `D3-R1 …`: MIDI and `patch:Set` events arrive once, with exact frame offsets, values and types, in non-decreasing order; a changed control port arrives at offset 0; an unchanged one does not; every port value is re-sent after activate and after restore; output is silence with the stub; activation with `InvalidArgument` fails.
- `D3-R2 …`: the generated Turtle names every registry entry exactly once with the registry's range and default, continuous as control ports and structural as `patch:writable` parameters, port indices matching the shared header; the plug-in URI and bundle name come from ids.hpp; state round trip, newer schema rejected, unknown id ignored; the worker job is trivially copyable and `run` never calls a controller main-thread function (the fake records the calling thread).

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to D3:

- **Platforms.** The card scopes LV2 to Linux hosts. D3-T1 and D3-T2 run on Linux only (the `lv2 (linux)` job). On macOS and Windows the requirement is that the existing `plugin` jobs and default builds stay green with `plugin/lv2/` skipped. State this in the report's `notes`.
- **Module note:** docs/dev/lv2.md — the SPEC sections used (SPEC-API "Events", "Parameter classes"; SPEC-EXEC "Threading contract", "Instrument lifetime"), port table and index policy, parameter mapping (continuous → control ports, structural → patch parameters), state format (D1's JSON), worker jobs and the D6 hook, threading and the mutex rule, provisional identifiers (D-3), what does not apply (channels > 2, tail, voice-info, host thread pool), how to build, lint and run the host test (`-DPME_BUILD_PLUGINS=ON`, `LV2_PATH`).
- **Lint:** clang-format and clang-tidy clean on `plugin/lv2/` (D1's `plugin (linux)` job runs clang-tidy over `plugin/`; your job does the same with its own compile database so the lilv host is covered).
- **Coverage gate and performance baselines:** D3 adds nothing under engine/src; they do not apply. Say so in `notes`.

### The `lv2` job (pr.yml)

Appended in your delimited block: `name: lv2 (linux)`, `runs-on: ubuntu-24.04` (same runner as the other Linux jobs), pinned actions by commit SHA like the existing jobs, the CPM cache keyed on cmake/dependencies.cmake. Steps: install Ninja, Clang with clang-tidy (as in the `plugin` job), `lv2lint`, `liblilv-dev`, `lilv-utils` and `lv2-dev` from apt, and print `dpkg-query -W` for them so the versions are in the log. If `lv2lint` is not in the runner's apt sources, build it from its upstream repository at an exact commit SHA that you read yourself, with its licence recorded; never write a version, commit or checksum you have not just read; if unreachable, stop and report. Configure `linux-rt-safety` with `-DPME_BUILD_PLUGINS=ON -DPME_REQUIRE_LV2_TOOLS=ON`, build, `ctest --preset linux-rt-safety -R "^D3-"`, run clang-tidy over `plugin/lv2/` with `tools/ci/run_clang_tidy.py`, upload JUnit as `junit-lv2-linux`. Then append `"lv2 (linux)"` to `tools/ci/required_checks.json`, a CONTRIBUTING.md row, and your docs/dev/ci.md subsection; `uv run pytest tools/tests/test_p0_2_r7.py` must pass.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `D3: ` and ending with the attribution lines your session provides. Make the `cmake/dependencies.cmake` gating its own first commit (`D3: fetch LV2 for Linux plug-in builds (PME_BUILD_PLUGINS)`). **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths (other agents' uncommitted files may be present in shared checkouts; never add them).
- Run before you finish: configure and build `linux-debug` and `linux-rt-safety` with `-DPME_BUILD_PLUGINS=ON` (GCC and Clang); `ctest -R "^D3-"` with the tools installed and once without them (skips, no failures); `ctest -R "^D1-"` unchanged and passing; lv2lint by hand on the bundle; `clang-format --dry-run --Werror` and clang-tidy on `plugin/lv2/`; `uv run pytest tools/tests/test_p0_2_r7.py tools/tests/test_p0_3_r1.py`; the default build (`PME_BUILD_PLUGINS=OFF`) still configures, builds and passes `ctest`.
- If the controller layer or interfaces-v2 prevents a requirement, stop that item and report it; the planner writes an ICR or ADR. Never edit `engine/**` or `plugin/clap/**`.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, lv2lint and lilv versions and output, where the LV2 specification bundles came from, and any `plugin/common/` change with its reason.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D3`, `role` `implementer`, `model` `sonnet`; `requirements` D3-R1 and D3-R2; `tests` D3-T1 and D3-T2; every created or changed path in `outputs`; `pull_requests` empty; `adrs` ADR-0003, ADR-0005, ADR-0007.
