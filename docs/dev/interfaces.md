# Public interfaces (P0.4)

SPEC sections implemented: SPEC-API (declarations and stubs only); SPEC-EXEC (threading contract as
documented on each function).

Everything in `engine/include/pme/` is the public interface of the engine. Phase P0.4 declared it and
stubbed every function; the orchestrator tagged the merge commit `interfaces-v1`. ICR-0001
(docs/icr/ICR-0001-interfaces-v2.md) amended them to `interfaces-v2` (SPEC-API, SPEC-EXEC "Instrument
lifetime", SPEC-PLK, SPEC-BODY, SPEC-STR, SPEC-INST; spec issues SI-0003 to SI-0007). The headers are
frozen at the current tag: the files change only through an approved interface change request
(docs/icr/README.md).

## Compatibility policy (interfaces-v2, ICR-0001)

**Source compatibility holds within a major version; ABI is not promised.** The major version is
`pme::interfacesVersionMajor` in `version.hpp` (2).

Breaking changes, which need an ICR and a new major version: removing or renaming a type, function,
enumerator or member; changing a type, a parameter list, a return type or a default value;
reordering the members of an aggregate that callers initialise positionally; changing a threading
or allocation guarantee; adding a `Status` code or an `Event` alternative that existing `switch`
statements would not handle. Compatible changes, which still go through an ICR while the tag is
frozen: adding a new header, a new function, or a new member at the end of a struct with a default
member initialiser. Documentation fixes that do not change meaning need no ICR.

Never promised: binary compatibility. Structs and classes may change size and layout; plug-ins and
the standalone app are built with the engine from the same sources.

## Error model (ADR-0006)

- Non-real-time functions that can fail return `[[nodiscard]] pme::Status` (`Ok`, `NotImplemented`,
  `InvalidArgument`, `Rejected`) and deliver their SPEC-API result through a final reference
  out-parameter. On any status other than `Ok` the out-parameter is left unchanged.
- Real-time functions (`Engine::process`, `reset`, `popDiagnostic`, `setInstrument`,
  `collectRetired`) keep SPEC-API's signatures, are `noexcept` and never fail.
  Threading (SPEC-EXEC): `process` and `reset` are audio-thread only; `prepare`, `setInstrument`,
  `collectRetired` and `popDiagnostic` are non-audio only and serialised by the caller. `Engine::prepare` runs off the audio thread and returns
  a `Status`.
- No public function throws, except that allocation failure in a non-real-time function may
  propagate as `std::bad_alloc`.
- Result types are default-constructible. Description types carry default member initialisers where
  SPEC-INST or the defining section gives a value; otherwise members are value-initialised.
- Public headers contain declarations and documentation only (no function bodies except `= default`
  and `= delete`; `constexpr` constants and enums are allowed).

## Header map

| Header | Declares | SPEC |
| --- | --- | --- |
| `status.hpp` | `Status`, `toString` | ADR-0006 |
| `types.hpp` | `Point` | SPEC-BODY, SPEC-CONV |
| `version.hpp` | `interfacesVersionMajor` | P0.4-R6 |
| `osc.hpp` | `OscCoeffs`, `compileOscillator` | SPEC-OSC |
| `string.hpp` | `Boundary`, `BridgeEnd`, `StringParams`, `StringModes`, `compileString` | SPEC-STR |
| `bridge.hpp` | `SegmentBridgeTerms`, `BridgePointDesc`, `compileBridgeTerms` | SPEC-BRG |
| `body.hpp` | `BodySource`, `RibDesc`, `BridgeBeamDesc`, `PlateParams`, `BodyParams`, `BodyModes`, `LocalBridge`, `ChannelDesc`, `compileBody`, `buildLocalBridge` | SPEC-BODY |
| `nonlinear.hpp` | nothing (documents that the interfaces have no nonlinear API) | SPEC-NL |
| `exciters.hpp` | `FeltLaw`, `HammerParams`, `UnaCordaParams`, `PluckPulse`, `Plectrum`, `Finger`, `InitialDisplacement`, `ExciterParams` | SPEC-HAM, SPEC-PLK |
| `dampers.hpp` | `DamperMode`, `DamperParams` | SPEC-DMP |
| `barriers.hpp` | `BarrierKind`, `BarrierParams` | SPEC-PLK |
| `instrument.hpp` | `SegmentDesc`, `NoteGroupDesc`, `TuningCurve`, `Defaults`, `InstrumentDesc`, `CompiledInstrument` (opaque), `compileInstrument` | SPEC-INST, SPEC-API |
| `engine.hpp` | `EngineConfig`, `AudioOut`, `Engine` | SPEC-API, SPEC-EXEC |
| `events.hpp` | `NoteOn`, `NoteOff`, `KeyDepth`, `Pedal`, `Pluck`, `Bend`, `Param`, `Event`, `EventList` | SPEC-API Events |
| `params.hpp` | `ParamId`, `ParamClass`, `ParamInfo`, `paramRegistry`, `findParam` | SPEC-API Parameter classes |
| `diagnostics.hpp` | `DiagnosticKind`, `Diagnostic` | SPEC-API Diagnostics |
| `pme.hpp` | umbrella include | |

The contracts SPEC lists in module sections but not in SPEC-API (`bridgeForce`, `GlobalBody`,
`savStep`, `Potential`, `launchHammer`, `updateDampers`, `longitudinalForce` ...) are internal to the
engine; the phase that implements each declares it privately. They are not part of the public interfaces.

`SegmentDesc` fields beyond SPEC-API's list: none. `Engine` keeps its state behind a private pointer
to an incomplete `Impl`; copy is deleted, move is allowed (a moved-from engine can be prepared
again). `CompiledInstrument` is declared and never defined in a public header.

## Events (P0.4-R5)

One struct per row of the SPEC-API events table; `Event` is a sample offset (`std::int32_t`, within
the host block, >= 0) plus a `std::variant` of the seven. `NoteOn`, `NoteOff` and `KeyDepth` address
a group by MIDI note number (A4 = 69; SPEC-STR "Tension for pitch"). `Pluck` is
`{segment, position, angle, strength}` (ICR-0001, SI-0005): `segment` is the flat segment index across
groups in `InstrumentDesc` order and then in `segments` order (SI-0004, also used by `Bend`), the
group's `ExciterParams` fixes the model (there is no `PluckKind`), `position` 0 means the
description's x_p, and `strength` is the normalised nu of the SPEC-PLK strength map. `EventList` is `std::span<const Event>`,
sorted by non-decreasing offset: a precondition of `Engine::process`. Every event type, and `Event`,
is trivially copyable (checked by static assertions in `engine/tests/interfaces/test_events.cpp`) so
events can cross lock-free single-producer single-consumer queues.

## Parameter registry (P0.4-R4)

`paramRegistry()` returns a static table sorted by id; `findParam(id)` binary-searches it. Both are
real-time safe. The registry holds instrument-wide parameters that can arrive as `Param` events:

- every continuous parameter SPEC-API names: sustain, sostenuto and soft pedal values, una corda
  shift (`pedal.soft`, 1003, the only id for it), sympathetic gain kappa_s, output gain (dB), damper timing;
- the instrument-wide structural parameters whose defaults SPEC-INST's piano table gives.

Per-key and per-segment physical parameters (string geometry, hammer felt, damper pad, barriers,
exciters) stay in the description structs and are never registered.

**Id scheme.** Ids are stable 32-bit numbers, never reused or renumbered. The thousands digit names
the area, the hundreds digit the class within it (0 continuous, 1 structural):

| Block | Area |
| --- | --- |
| 1000 | performance controls (pedals, una corda shift) |
| 2000 | output |
| 3000 | sympathetic path |
| 4000 | dampers |
| 5000 | una corda felt map |
| 6000 | strings and geometry |
| 7000 | body |

New parameters take the next free id in their block and class. A retired parameter's id is kept
reserved in this file.

**Retired ids:** 1010 `unaCorda.shift` (ICR-0001; use 1003 `pedal.soft`).

**Provisional entries.** The ranges and classes were confirmed by SI-0007 (ICR-0001). One default
remains a choice: `strings.fMax` default 16 kHz (SPEC-INST; V2 may revise).

**`params.json`.** The file at the repository root is generated by `pme_params_json`
(`tools/params/`), deterministic: sorted by id, 12 significant digits, LF line endings. Regenerate
with `cmake --build <build-dir> --target update_params_json`. The CTest test
`P0.4-R4_params_json_matches_registry` fails when the committed file differs from the generator's
output.

## Provisional types

SPEC-API names these types without listing members. The members below come from the defining SPEC
section and are marked **provisional** in the headers; the planner files the gaps as spec issues.

- `AudioOut` (a span of per-channel pointers), `Diagnostic` (kind, group, sample, value), `Defaults`
  (instrument-wide values of SPEC-INST's piano table plus `localBridgeFSplit`, whose 2 kHz default is
  provisional until B3-T2 and V3), `BridgePointDesc`, `ChannelDesc` (`gainDb` is a later compatible
  ICR).
- Compiled data: `OscCoeffs` (complete: SPEC-OSC Contract), `StringModes`, `SegmentBridgeTerms`,
  `BodyModes`, `LocalBridge` (oscillator coefficients are not stored because `buildLocalBridge` has
  no sample rate; `compileInstrument` derives them).
- `BodyParams`: `RibDesc`, `modalTableHash`.

Settled by ICR-0001 and no longer provisional: the `Point` body frame (SI-0003), `TuningCurve` and
`NoteGroupDesc::key` (SI-0004), `Finger` and the `Pluck` units (SI-0005), `PlateParams` geometry
(outline, thickness grid, `BridgeBeamDesc`, rim stiffness; SI-0003).

Compiled-data types expose their members so tests written from the public headers can read them.

## `pme_engine` and the modules

`pme_engine` (alias `pme::engine`) stays an INTERFACE library that links the eleven module
libraries. It lists the public headers as a header file set and, privately, as sources, so CMake
generates a build target for it, and it depends on every `pme_<module>`: `cmake --build --preset
<preset> --target pme_engine` builds the whole engine. Consumers inherit only the link
dependencies and the include directory `engine/include`.

## Stubs (P0.4-R3)

`osc`, `string`, `bridge`, `body` and `runtime` hold `<module>_stub.cpp`; `nonlinear`, `exciters`,
`dampers`, `barriers`, `sched` and `simd` keep their placeholders until their own phases. Every
compile function returns `Status::NotImplemented` and leaves its out-parameter unchanged.
`Engine::prepare` checks `fs > 0`, `subBlock` in {16, 32} (SPEC-CONV), `maxBlock >= 1`,
`channels >= 1`, `threads >= 1` (`InvalidArgument`), else returns `NotImplemented`.
`Engine::process` writes exact `0.0f` to `frames` samples of every channel (null channel pointers
are skipped), reads and ignores the events, and does not allocate, lock or throw, also when
`prepare` was never called or failed. `setInstrument` (non-audio only) accepts and releases its
argument on the calling thread and never swaps; `collectRetired` is a no-op (the retire queue is C1's
work, SPEC-EXEC "Instrument lifetime"). `popDiagnostic` returns `false`. No performance baseline is needed: the stubs only zero
buffers.

## Tests

`engine/tests/interfaces/` builds `pme_interface_tests` (`pme_add_doctest`, own `main`; one CTest test per test case, label `unit`, plus `rt` in the rt-safety build):

| Test | What |
| --- | --- |
| `P0.4-T1` | CMake generates one translation unit per `engine/include/pme/*.hpp` that includes it twice and nothing else; the count must equal the number of headers. |
| `P0.4-T2` | rt-safety build with `pme_rtcheck` only: the stub engine processes 120 blocks under `ScopedRealtimeThread` without allocating. Skipped (CMake STATUS message) until P0.2 is merged. |
| `P0.4-R3` | compile stubs return `NotImplemented` and leave outputs unchanged; `prepare` checks; `toString`; allocation count of `process` (not in rt-safety builds); exact zeros. |
| `P0.4-T3`, `P0.4-R4` | registry invariants; `params.json` equals the generator output. |
| `P0.4-R5` | event fields; trivially-copyable static assertions. |
| `ICR-0001 ...` | interfaces version is 2; retired id 1010 is not registered; v2 description defaults; `collectRetired` on prepared and unprepared engines. |

Detail: `pme::toString(Status)` is found by argument-dependent lookup and shadows doctest's own
`toString` in `CHECK(status == ...)`. The test executable defines `DOCTEST_CONFIG_DOUBLE_STRINGIFY`
to make doctest convert the result again; the shared doctest helper (P0.3) needs the same
definition for any target whose tests compare `Status` values.
