<!-- SPDX-License-Identifier: MIT -->
# LV2 plug-in (D3)

A native LV2 instrument for Linux hosts. It shares the CLAP shell's controller layer
(`plugin/common/`, see [clap-shell.md](clap-shell.md)) and, like D1, runs against the **stub
engine** (silence; `Engine::prepare` returns `NotImplemented`). D6 swaps in the real engine for every
format at once.

SPEC sections used (docs/plan/engine-specification.md): SPEC-API "Events" and "Parameter classes";
SPEC-EXEC "Threading contract" and "Instrument lifetime". PLAN-STACK ("Other formats": LV2 on
Linux). Decisions: ADR-0003 (pinned LV2 1.18.10), ADR-0005 (nlohmann/json, non-real-time only),
ADR-0007 (LV2 has no host thread pool, so the controller's own pool applies).

## Build, lint and run the host test

```bash
cmake --preset linux-rt-safety -DPME_BUILD_PLUGINS=ON      # add -DPME_REQUIRE_LV2_TOOLS=ON in CI
cmake --build --preset linux-rt-safety
ctest --preset linux-rt-safety -R "^D3-"
```

`PME_BUILD_PLUGINS=ON` on Linux fetches the pinned LV2 headers (the only consumer of that pin) and
builds `plugin/lv2/`; macOS and Windows skip the directory. The bundle is
`build/<preset>/plugin/lv2/pme.lv2/` with `pme_lv2.so`, `manifest.ttl` and `pme_lv2.ttl`; the two
Turtle files are generated at build time (below). `build/<preset>/plugin/lv2/lv2path/` holds a link
to the bundle: it is the `LV2_PATH` entry of the tests, together with the LV2 specification bundles
of the pinned source (`<build>/_deps/lv2-src/lv2`), so the tests need no system `lv2-dev`.

| Test | What | Tools |
| --- | --- | --- |
| D3-T1 `lv2lint passes` | `lv2lint -Mpack <URI>` over the built bundle (found through `LV2_PATH`), exit status 0 (`plugin/lv2/run_lv2lint.cmake`) | `lv2lint` |
| D3-T2 `a headless LV2 host loads the plug-in and renders a test scenario` | `plugin/lv2/tests/lv2_host.cpp`, a lilv host with a real worker thread (below) | `liblilv-dev` (pkg-config `lilv-0`), test-only, ISC, never shipped |
| D3-R1, D3-R2 unit tests | `plugin/lv2/tests/test_lv2_*.cpp`: `run`, state, worker, notify, Turtle, with a recording engine and hand-built atom sequences (labels `unit`, `rt`) | none |

`PME_REQUIRE_LV2_TOOLS` (default OFF): when `lv2lint` or `lilv-0` is missing the dependent test is
skipped (`SKIP_REGULAR_EXPRESSION`); with the option ON, configuration fails. The `lv2` CI job sets it
ON, so a skip cannot pass in CI. No lv2lint check is disabled and none is promoted to an error with
`-E` (the default configuration fails on error-level findings only); `-Mpack` only skips checks
meant for distribution packagers. Current non-fatal finding: `Plugin Author Email` (`foaf:mbox`
missing from the maintainer); a real contact address awaits decision D-3, and none is invented.

### lv2lint (pinned source build)

| Item | Value |
| --- | --- |
| Source | GitHub mirror `https://github.com/amsynth/lv2lint` (upstream: `git.open-music-kontrollers.ch/~hp/lv2lint`, unreachable from the D3 sessions) |
| Commit | `ec29cfed3193a89a8091a4c18e22bb36e6b43b24` (master of the mirror when read; the mirror has no tags); version string 0.17.71 |
| Licence | Artistic-2.0 (SPDX header of `src/lv2lint.c`; `meson.build` and the README are CC0-1.0; the vendored subprojects `mapper.lv2` and `varchunk` carry their own `LICENSES/`). Test-only tool, never linked into or shipped with the plug-in |
| Build | `meson setup --prefix=$HOME/lv2lint-prefix` and `ninja install`; online, ELF and X11 checks stay disabled (the defaults). The installed `lv2lint` wrapper script hard-codes its install prefix, so it must be run from where it was installed |
| CLI | `-v` prints the version (`--version` is not an option), `-Mpack` as above, plug-in URIs as arguments; plug-ins are found via `LV2_PATH` (`-I <dir>` would add a second search directory and cause a duplicate-plug-in warning here) |

CI verifies the checked-out SHA and caches the install keyed on it (docs/dev/ci.md). Moving the pin
means re-reading the new commit, its licence and `-h` output.

Run by hand:

```bash
export LV2_PATH=build/linux-rt-safety/plugin/lv2/lv2path:build/linux-rt-safety/_deps/lv2-src/lv2
lv2lint -Mpack https://pme.invalid/lv2/instrument
build/linux-rt-safety/plugin/lv2/pme_lv2_host https://pme.invalid/lv2/instrument
lv2info https://pme.invalid/lv2/instrument
```

## Provisional identifiers (D-3)

Decision D-3 is open. The identifiers are appended to
`plugin/common/include/pme/plugin/ids.hpp` and spelled nowhere else: plug-in URI
`https://pme.invalid/lv2/instrument` (the reserved `.invalid` domain can never resolve to anyone
else's site), bundle directory `pme.lv2`, module base name `pme_lv2`. CMake reads them from the
header (`file(STRINGS ...)`) and the Turtle generator writes them. D2 replaces them when D-3 is
answered.

## Ports (public contract)

| Index | Symbol | Type | Role |
| --- | --- | --- | --- |
| 0 | `in` | atom input, `atom:Sequence`, supports `midi:MidiEvent`, `patch:Message` | MIDI and patch messages |
| 1 | `notify` | atom output, `atom:Sequence`, supports `patch:Message` | `patch:Set` replies |
| 2, 3 | `out_l`, `out_r` | audio output | stereo |
| 4 .. 11 | dotted name with `.` as `_` (`pedal_sustain`, ..., `damper_kRelease`) | control input | one per **continuous** registry entry |

`plugin/lv2/src/ports.hpp` defines the indices and `kControlPortParams`, the registry ids of the
control ports in port order. The adapter and the generator both use it. **Index policy:** a
parameter added to the registry later may only be appended to that table; existing entries never
move, are never removed and never change class. The generator and the D3-R2 tests fail when a
continuous registry entry is missing from the table.

## Turtle (D3-R2)

`pme_lv2_gen` (`plugin/lv2/src/gen_ttl.cpp`, library `turtle.cpp`) writes `manifest.ttl` and
`pme_lv2.ttl` at build time from ids.hpp and `pme::paramRegistry()`; no registry content is checked
in. The plug-in is `lv2:Plugin, lv2:InstrumentPlugin`, license `<https://spdx.org/licenses/MIT>`,
`lv2:requiredFeature urid:map`, optional `work:schedule`, `options:options`, `log:log`,
`lv2:hardRTCapable`, extension data `state:interface` and `work:interface`.

| Registry entry | LV2 |
| --- | --- |
| continuous | `lv2:ControlPort` input: `lv2:symbol`, `lv2:name` (dotted name), `lv2:minimum`, `lv2:maximum`, `lv2:default` from the registry; `units:unit` for dB, Hz, s, ms, cent |
| structural | `lv2:Parameter` `<URI>#param.<dotted name>`, `rdfs:range atom:Double`, range and default from the registry, listed in `patch:writable`. Not a port, so hosts do not automate it (SPEC-API "Parameter classes": structural means recompiled) |
| instrument | `lv2:Parameter` `<URI>#instrument`, `rdfs:range atom:Path`, in `patch:writable` |

## Run (D3-R1)

Per `run(n)`: `Controller::beginBlock(n)`; the input sequence in frame order: `midi:MidiEvent` goes
to `events().midi(...)` (D1's translation table applies unchanged: notes, CC64 continuous, CC66,
CC67, velocity-0 note off, polyphonic aftertouch); `patch:Set` of a structural parameter goes to
`paramEvent(frame, id, value)` (numeric atoms of any width); `patch:Set` of the instrument property
copies the path into a worker job; `patch:Get` is answered on `notify`. Then every continuous
control port is compared bitwise with its last seen value and sent with `paramEvent(0, ...)` when it
changed, and all of them on the first `run` after `activate` or `restore`. A port is a float: when it
holds the float nearest the registry default the exact default is sent, so rounding never makes a
parameter look changed in the saved state. Last, `render(n, {left, right})`. Event frames are
clamped to the block, an input sequence in beats is ignored. `run` marks itself
`ScopedRealtimeThread` in the rt-safety build.

**Does not apply or is not done:** more than two channels (D1-R1's 4/6/8-channel configurations are
CLAP-only for now); tail and voice-info (LV2 has neither extension: D1-R5's tail and voice count do
not apply; latency is 0 and there is no latency port); the host thread pool (LV2 has none; the
controller's own pool of ADR-0007 stays in place); `lv2:freeWheeling`: no such port is offered, so
`Controller::setRenderMode` is not driven (D1-R7 parity is not claimed for LV2).

## State (D3-R2)

`state:interface`. `save` stores one property `<URI>#state`, type `atom:String` (with the NUL),
flags `LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE`, value `serializeState(captureState())`: D1's JSON,
byte for byte. `restore` retrieves it, `parseState`, then `applyState`. A newer
`state_schema_version`, unparsable text or a wrong type returns an LV2 error and leaves the plug-in
unchanged; unknown ids are ignored (logged through `log:log`). Continuous parameters are owned by
the control ports: the next `run` after `restore` re-sends every port value, so port values win over
the JSON (a host keeps port values in its own session). The next `run` also reports the structural
parameters and the instrument on `notify`. `state:mapPath` is not used: until B1 the instrument
reference is a plain string.

## Worker (D3-R2)

The LV2 worker is the plug-in's non-audio thread for what SPEC-EXEC keeps off the audio thread.
`run` schedules fixed-size, trivially copyable jobs (`Job` in `lv2_shell.hpp`: kind, length, a
1024-byte path; no pointers):

| Job | Scheduled | `work` (worker thread, mutex held) | `work_response` (audio thread) |
| --- | --- | --- | --- |
| Instrument (1) | on `patch:Set` of the instrument; a path of 1024 bytes or more is rejected | `setInstrumentReference` (**D6 hook**: compile and `EngineAdapter::setInstrument` go here, marked in `Plugin::work`) | stores the path; the next `run` sends `patch:Set` on `notify` |
| Tick (2) | about every 100 ms of rendered frames, at most one outstanding | `mainThreadTick` (`collectRetired`, diagnostics) | clears the outstanding flag |

Structural parameter changes travel as `Param` events (the engine recompiles off the audio thread);
the shell has no second recompilation path. Without `work:schedule` the plug-in still instantiates;
instrument changes from `run` are ignored and counted (`droppedInstrumentJobs`), and `mainThreadTick`
runs in `deactivate` and `save`.

## Threads

Non-audio: `instantiate`, `activate`, `deactivate`, `cleanup`, `save`, `restore`, `work`. Audio:
`run`, `work_response`. Every call into the controller's main-thread API (`activate`, `deactivate`,
`applyState`, `captureState`, `mainThreadTick`, `setInstrumentReference`) is serialised by one
`std::mutex` that only non-audio threads take; `run` never locks. `restore` hands the restored
instrument path to `run` through an atomic state machine (no lock). The audio thread does not
allocate, log or throw.

## D3-T2 scenario

`lv2_host` instantiates at 48 kHz with `urid:map`, `options:options` (`maxBlockLength` 512) and a
`work:schedule` backed by a real worker thread, checks the port count, symbols, ranges and
`patch:writable` through lilv, connects every port, activates and renders 1 s in blocks of 512:
note on key 60 velocity 100 at frame 100; CC64 127 at 1000; a control-port change (`output.gain`
-6 dB) in block 3; `patch:Set` of `unaCorda.s0` at 2000; `patch:Set` of the instrument at 3000; note
off at 24000; CC64 0 at 30000. It passes when every `run` returns, every sample is finite and
exactly 0 (stub engine), the worker ran the instrument job and at least one tick on its own thread,
the instrument change was reported on `notify`, and `save`, `restore` into a second instance, `save`
give byte-identical state strings.

## Tests

| ID | File |
| --- | --- |
| D3-R1 | `plugin/lv2/tests/test_lv2_run.cpp` |
| D3-R2 | `plugin/lv2/tests/test_lv2_state.cpp` (state, worker, notify, threads), `test_lv2_turtle.cpp` |
| D3-T1 | `plugin/lv2/CMakeLists.txt`, `run_lv2lint.cmake`; `lv2` job |
| D3-T2 | `plugin/lv2/tests/lv2_host.cpp`; `lv2` job |

D3 adds nothing under `engine/src`, so the coverage gate and performance baselines do not apply.
