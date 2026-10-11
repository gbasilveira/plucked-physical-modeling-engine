<!-- SPDX-License-Identifier: MIT -->
# CLAP plug-in shell (D1)

The native CLAP instrument. It owns one `pme::Engine`, maps host events and parameters to engine
events, and saves state. D1 runs against the **stub engine** (silence; `Engine::prepare` returns
`NotImplemented` after validating its argument); D6 swaps in the real engine.

SPEC sections used (docs/plan/engine-specification.md): SPEC-API "Real-time interface", "Events",
"Parameter classes", "Diagnostics"; SPEC-EXEC "Execution order" ("Threading contract", "Instrument
lifetime"). Decisions: ADR-0003 (pinned dependencies), ADR-0005 (nlohmann/json), ADR-0006 (Status
returns), ADR-0007 (host thread-pool split), ICR-0001 (interfaces-v2).

## Build and validate

```bash
cmake --preset linux-rt-safety -DPME_BUILD_PLUGINS=ON     # or <system>-debug
cmake --build --preset linux-rt-safety
ctest --preset linux-rt-safety -R "^D1-"
```

`PME_BUILD_PLUGINS=ON` fetches the pinned CLAP headers and adds `plugin/`. The module is
`build/<preset>/plugin/clap/pme.clap` (a bundle with a minimal `Info.plist` on macOS).

`D1-T1` runs clap-validator on that module. It is skipped unless the validator is given:

```bash
PME_CLAP_VALIDATOR=/path/to/clap-validator ctest --preset linux-rt-safety -R "^D1-T1"
# or: cmake ... -DPME_CLAP_VALIDATOR=/path/to/clap-validator
/path/to/clap-validator validate build/linux-rt-safety/plugin/clap/pme.clap
```

The CI pin is clap-validator `0.3.2`; see "The `plugin` job" in [ci.md](ci.md).

## Layout

| Path | Contents |
| --- | --- |
| `plugin/common/` | **Controller layer**, format-independent, no CLAP types in its public headers (`pme_plugin_common`, namespace `pme::plugin`). D3 builds its LV2 adapter on it. |
| `plugin/clap/src/` | The CLAP adapter: `entry.cpp` (`clap_entry`), `plugin.cpp` (factory, plug-in object, every extension). |
| `plugin/clap/tests/`, `plugin/common/tests/` | doctest tests named after the requirement and test IDs. |

### Controller layer (for D3)

| Class | Role |
| --- | --- |
| `EngineAdapter` | `prepare`, `setInstrument`, `collectRetired`, `process`, `reset`, `popDiagnostic`. `RealEngineAdapter` wraps `pme::Engine`; tests use a recording fake (`plugin/common/tests/fake_engine.hpp`). |
| `Controller` | Owns the adapter, parameter model, translator, parameter queue and task runner. Main-thread API: `activate`, `deactivate`, `applyState`, `captureState`, `mainThreadTick`, `setChannels`, `setThreadsSetting`, `setHostPool`, `setRenderMode`. Audio-thread API: `beginBlock`, `events()`, `paramEvent`, `flushParamEvent`, `render`, `reset`. |
| `EventTranslator` | Host-neutral note, MIDI and parameter input to `pme::Event` in a preallocated buffer (4096 events per block), clamped and sorted by offset. |
| `ParamModel` | One entry per `pme::paramRegistry()` entry; atomic current values; clamping, value to text and text to value, module paths. |
| `serializeState` / `parseState` | JSON state (nlohmann/json, main thread only). |
| `SpscQueue` | Preallocated lock-free single-producer single-consumer queue (main thread to audio thread). |
| `TaskRunner` | Host-pool routing of ADR-0007 (below). |
| `ids.hpp` | The provisional identifiers (below). |

A block through the audio thread is `beginBlock(frames)`, then the format adapter pushes events into
`events()` (and calls `paramEvent` for parameters), then `render(frames, channels)`. `render` sorts
the events (SPEC-EXEC step 2.1: non-decreasing offsets, every offset in `[0, frames)`), and splits a
host block larger than the activated maximum into chunks with re-based offsets. The whole path is
free of allocation, locks, exceptions, I/O and system calls; the CLAP `process` runs under
`pme::rtcheck::ScopedRealtimeThread`, so the rt-safety build aborts on a violation.

## Provisional identifiers (D-3)

Decision D-3 (product name and plug-in identifiers) is open. Every identifier is defined in
`plugin/common/include/pme/plugin/ids.hpp` and nowhere else: plug-in id `pme.instrument`, name
`pme`, vendor `pme project`, version from the CMake project version, module file `pme.clap`, macOS
bundle id `org.pme.clap` (`plugin/clap/CMakeLists.txt`). D2 freezes the final values under D-3; the
state format does not contain the plug-in id.

## CLAP surface

| Extension | Behaviour | Requirement |
| --- | --- | --- |
| descriptor | features `instrument`, `stereo`, `synthesizer` | D1-R1 |
| `clap.audio-ports` | no inputs, one main output, stereo by default (`port_type` stereo), the selected count otherwise | D1-R1 |
| `clap.audio-ports-config` (+ `-info`) | configurations 0 to 3 = 2, 4, 6, 8 channels, one port; `select` only while inactive; the count becomes `EngineConfig::channels` at the next `activate` | D1-R1 |
| `clap.note-ports` | one input, dialects CLAP and MIDI, preferred CLAP | D1-R2 |
| `clap.params` | one parameter per registry entry (below) | D1-R3 |
| `clap.state` | JSON (below) | D1-R4 |
| `clap.latency` / `clap.tail` / `clap.voice-info` | 0 / infinite while sounding / 128 voices | D1-R5 |
| `clap.thread-pool` | `exec(task_index)` forwards to `TaskRunner::exec` | D1-R6 |
| `clap.render` | accepts realtime and offline | D1-R7 |
| `clap.timer-support` (host) | 100 ms timer drives `mainThreadTick`; without it `request_callback` and `on_main_thread` do | ICR-0001 |

### Event translation (D1-R2)

Every event keeps its sample offset. Key outside 0..127 (including the wildcard -1) is ignored.

| Input | Engine event |
| --- | --- |
| CLAP note on (key k, velocity v) / MIDI note on (velocity > 0) | `NoteOn{key = k, velocity}`; a CLAP velocity <= 0 becomes 1/127 because `NoteOn` needs nu in (0, 1]; MIDI velocity / 127 |
| CLAP note off / MIDI note off / MIDI note on with velocity 0 | `NoteOff{key, releaseVelocity}` (velocity 0 for note on with velocity 0) |
| CLAP note choke | `NoteOff{key, releaseVelocity = 0}` |
| MIDI CC64 value c | `Pedal{Sustain, c / 127}` (continuous: half-pedal works) |
| MIDI CC66 value c | `Pedal{Sostenuto, c >= 64 ? 1 : 0}` |
| MIDI CC67 value c | `Pedal{Soft, c / 127}` |
| MIDI polyphonic aftertouch (key, p) | `KeyDepth{key, p / 127}` |
| other MIDI, gestures, modulation, transport | ignored |

The MIDI channel is ignored. No CLAP `note end` event is emitted: with the stub engine the shell
cannot tell that a voice ended (D6 emits it from the engine's diagnostics).

### Parameters (D1-R3)

CLAP id = registry id; `name` = the dotted registry name; `module` = the dotted prefix with `/`;
min, max and default from the registry.

* **Continuous**: `CLAP_PARAM_IS_AUTOMATABLE`; the value is delivered as a `pme::Param{id, value}`
  event at its exact sample offset (CLAP param-value events in `process`; `flush` stores the value
  and delivers it at offset 0 of the next block).
* **Structural**: not automatable. CLAP has no "requires recompilation" flag, so the module path
  starts with `structural/` (for example `structural/strings`) and the value is delivered as a
  `Param` event like any other; the engine does the recompilation (SPEC-API "Parameter classes").
* Values outside `[min, max]` are clamped, NaN is ignored. Events in another event space are
  ignored. `value_to_text` prints 9 significant digits and the registry unit (`-6 dB`; the unit `1`
  is omitted); `text_to_value` parses the leading number.

### State (D1-R4)

Compact JSON, keys in sorted order, doubles in their shortest exact form (nlohmann/json, ADR-0005;
state runs on the main thread only):

```json
{"instrument":"","interfaces_version":2,"params":{"2001":-6.5},"plugin_version":"0.0.0","state_schema_version":1}
```

`instrument` is the instrument reference (path or id; empty until B1 provides loading). `params`
holds only values that differ from the registry default, keyed by id. Loading accepts schema 1,
ignores unknown ids (a note goes to the host log), clamps values, makes every unlisted parameter
default, and **rejects** a newer `state_schema_version` (and an empty or malformed stream) by
returning false without changing anything. Changed values reach the audio thread through the
preallocated SPSC queue (1024 entries), never by allocation; when the plug-in is inactive,
`activate` queues every non-default value.

### Tail and voices (D1-R5)

Latency is 0. The tail is `UINT32_MAX` (infinite) while "a string sounds" and 0 otherwise. With the
stub engine "sounds" means a key is held (a `NoteOn` without its `NoteOff`) or the sustain value is
above 0 (`Pedal` or the `pedal.sustain` parameter); `reset` clears it. D6 replaces this with the
engine's group-energy diagnostics. `clap.voice-info` reports 128 voices and capacity 128 (one per
key; the instrument's note-group count once B1/C1 exist), without
`CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES`.

### Offline rendering (D1-R7)

`clap.render` accepts `CLAP_RENDER_REALTIME` and `CLAP_RENDER_OFFLINE` and records the mode
(`Controller::renderMode()`) for D6's bounce-equals-real-time test. The engine receives the same
events, offsets and block lengths in both modes, and `EngineConfig::referenceMode` is never touched
(that is a separate user option, not a bounce flag).

## Threads

Main thread: `init`, `activate` (calls `Engine::prepare`), `deactivate`, state, `on_main_thread`,
the timer, `get_info` and the other query callbacks. Audio thread: `process`, `start_processing`,
`reset`, `flush` while processing. `Engine::setInstrument` and `collectRetired` are called from the
main thread only (`collectRetired` from the timer or `on_main_thread`, ICR-0001). The stub engine's
`NotImplemented` from `prepare` means "engine unavailable": activation succeeds and the shell writes
silence itself; `InvalidArgument` fails the activation. Nothing is logged from the audio thread.

## Host thread pool (D1-R6, ADR-0007)

D1 delivers the shell side only; the engine-side adapter is C5-R5 and the routing is D6 (D1-R6 is
`deferred` with ADR-0007). The shell queries `clap.thread-pool` at `init`, implements
`clap_plugin_thread_pool.exec`, and hands a `TaskRunner` to whoever needs it
(`Controller::taskRunner()`). **Contract for C5** (`task_runner.hpp`):

* `run(count, fn, context)` is called on the audio thread only, never concurrently and never from a
  pool task. It returns when all `count` tasks have finished. `fn` is `void (*)(void*, uint32_t)
  noexcept` and must be real-time safe.
* With a host pool, `run` calls `request_exec(count)`; the host runs `exec(index)` once per index in
  `[0, count)` on its pool threads (the audio thread may take part). If the host rejects the request
  or has no pool, `run` executes indices `0 .. count-1` in order on the calling thread.
* Every index runs exactly once. `run` allocates nothing and takes no lock of its own.
* When the host has no pool, `EngineConfig::threads` is the thread setting
  (`Controller::setThreadsSetting`, default: hardware threads minus one, at least 1, at most 8) so
  that the engine uses its own pool; with a host pool it is 1 until D6 decides the task count.

## Tests

| ID | File | What |
| --- | --- | --- |
| D1-T1 | `plugin/clap/CMakeLists.txt`, `run_validator.cmake`; `plugin` job | clap-validator on the built module |
| D1-T2 | `plugin/clap/tests/test_clap_state.cpp` | exact state round trip through in-memory streams; newer schema rejected; unknown id ignored |
| D1-T3 | `plugin/clap/tests/test_clap_process.cpp` | automation, note and MIDI events at exact offsets, whole and with blocks split 1/7/504 |
| D1-R1..R7 | `plugin/common/tests/`, `plugin/clap/tests/test_clap_shell.cpp` | translation table, parameters, tail and voices, task runner, render mode, activation |

D1 adds nothing under `engine/src`, so the coverage gate and performance baselines do not apply.
