# ICR-0001 interfaces-v2: instrument retirement, plucked exciters, tuning keys, body geometry, registry clean-up

Status: applied · Raised by: P0.4, code-reviewer (via SI-0003 to SI-0007); filed by the planner · Date: 2026-10-11
Interfaces: interfaces-v1 (2daee85) → interfaces-v2 (300b995) · Major version: 1 → 2 (breaking)

## Problem

The P0.4 review found five SPEC gaps that the frozen interfaces-v1 headers cannot express or express wrongly. The orchestrator accepted all five spec issues on 2026-10-11. This ICR batches every header change they need, plus three documentation-only comment fixes, into one major bump.

| Source | What blocks | Blocked phase or test |
| --- | --- | --- |
| [SI-0006](../spec-issues/SI-0006-compiled-instrument-retirement.md) | `setInstrument` is callable "from any thread" and nothing frees the outgoing `CompiledInstrument` off the audio thread. Dropping the last `shared_ptr` on the audio thread deallocates there. | C1-T3, C4-T3 (structural edits under rt-safety); C1, C4, C6 cannot be briefed |
| [SI-0005](../spec-issues/SI-0005-plucked-exciter-interfaces.md) | `Pluck` duplicates the group's `ExciterParams` (`force`, `kind`); `Finger` mixes driver and contact stiffness; no `InitialDisplacement` exciter | P1-R1 to P1-R5, ORC-10, P6 |
| [SI-0004](../spec-issues/SI-0004-tuning-keys-and-segment-addressing.md) | No per-string unison detune; key numbering and segment index space undefined | B1-R1 (schema mirrors `SegmentDesc`), C1 (who computes f₁), C4 (event addressing) |
| [SI-0003](../spec-issues/SI-0003-body-description-types.md) | `Point` frame contradicts θ\_g; `PlateParams` holds only a rectangle, uniform thickness and an undefined rim stiffness; f\_split default undocumented | B1-R1, B2-R1 |
| [SI-0007](../spec-issues/SI-0007-parameter-registry.md) | `pedal.soft` (1003) and `unaCorda.shift` (1010) are one quantity; provisional ranges unconfirmed; f\_max precedence | C4, D1 |
| [SI-0001](../spec-issues/SI-0001-compile-string-registered-points.md), [SI-0002](../spec-issues/SI-0002-api-compiled-data-and-io-types.md) follow-ups | `StringModes::phiBar` documented as window-averaged (it holds point values, W = 1); `Diagnostic::group` documented as a key | Documentation only |

## Proposed change

Declarations only. Members not mentioned keep their declaration and comment. "Doc" marks a comment change with no declaration change. Every new member has a default member initialiser. Comments cite the SPEC key and the SI.

### version.hpp

```diff
-/// Compatibility policy (interfaces-v1): ...
+/// Compatibility policy (interfaces-v2, ICR-0001): source compatibility holds within a major
+/// version; ABI is not promised. ...
-inline constexpr int interfacesVersionMajor = 1;
+inline constexpr int interfacesVersionMajor = 2;
```

### engine.hpp (SI-0006, SI-0007 item 4)

```diff
 /// Threading contract (SPEC-EXEC):
-/// `prepare` and `popDiagnostic` run on non-audio threads, `setInstrument` on any thread,
-/// `process` and `reset` only on the audio thread.
+/// `prepare`, `setInstrument`, `collectRetired` and `popDiagnostic` run on non-audio threads,
+/// one at a time (the caller serialises them); `process` and `reset` only on the audio thread.
+/// The audio thread never touches a reference count (SPEC-EXEC "Instrument lifetime").

 struct EngineConfig {
-    double fMaxStrings{};  ///< Highest kept string frequency f_max in Hz (SPEC-INST: 12 to 20 kHz).
+    double fMaxStrings{};  ///< Highest kept string frequency f_max in Hz (SPEC-INST: 12 to 20 kHz,
+                           ///< default 16 kHz). Authoritative at `prepare`; a `strings.fMax`
+                           ///< Param event recompiles the instrument and updates this copy
+                           ///< (SPEC-API "Parameter classes", SI-0007).
 };

 class Engine {
     /// Configure the engine and allocate everything `process` needs (SPEC-API).
-    /// Thread: non-audio; must not run concurrently with `process`. Allocates: yes.
+    /// Thread: non-audio; must not run concurrently with `process`. Allocates: yes (including
+    /// the 4-entry retire queue). Drains the retire queue first (SPEC-EXEC "Instrument lifetime").

-    /// Offer a compiled instrument; it is swapped in with one atomic exchange at the start of
-    /// the next block (SPEC-EXEC step 1). Thread: any. Allocates: no. ...
-    void setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept;
+    /// Offer a compiled instrument; the audio thread swaps it in with one atomic exchange at the
+    /// start of the next block (SPEC-EXEC step 1, "Instrument lifetime"). The engine keeps the
+    /// shared_ptr; the audio thread sees only the raw pointer. A null argument removes the
+    /// instrument at the next block (silence). Drains the retire queue first; if a previously
+    /// offered instrument was not yet taken, it is retired by this call.
+    /// Thread: non-audio only. Allocates: no; may free retired instruments. Never throws.
+    void setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept;

+    /// Release every instrument the audio thread has retired (SPEC-EXEC "Instrument lifetime").
+    /// Call it periodically from a non-audio thread (plug-in shells: from the main or a timer
+    /// thread). Thread: non-audio only. Allocates: no; frees retired instruments. Lock-free.
+    void collectRetired() noexcept;

-    /// Take the oldest pending diagnostic (SPEC-API "Diagnostics"). Lock-free.
-    /// Thread: non-audio. Allocates: no. ...
+    /// Take the oldest pending diagnostic (SPEC-API "Diagnostics"). Lock-free. Also drains the
+    /// retire queue (SPEC-EXEC "Instrument lifetime").
+    /// Thread: non-audio. Allocates: no; may free retired instruments. ...
     bool popDiagnostic(Diagnostic& d) noexcept;

-    ~Engine();
+    ~Engine();  ///< Releases the current, pending and retired instruments. Thread: non-audio.
 };
```

### events.hpp (SI-0005 item 1 and 3; SI-0004 items 1 and 4)

```diff
 struct NoteOn {
-    std::int32_t key{};  ///< Key number of the note group.
+    std::int32_t key{};  ///< MIDI note number 0..127 (A4 = 69) of the target group (SPEC-STR
+                         ///< "Tension for pitch", SI-0004). No group with this key: ignored.
     float velocity{};    ///< ...
 };
 // NoteOff::key, KeyDepth::key: Doc, same wording.

-/// Kind of plucked exciter a `Pluck` event starts (SPEC-API "Events", SPEC-PLK "Exciters").
-enum class PluckKind : std::uint8_t { ForcePulse, Plectrum, Finger };
+// PluckKind removed (ICR-0001, SI-0005): the group's ExciterParams fixes the model.

-/// Start a plucked exciter on a segment (SPEC-API "Events", SPEC-PLK).
+/// Start the group's plucked exciter on one segment (SPEC-API "Events", SPEC-PLK "Exciters").
+/// The model and its parameters come from the group's ExciterParams; the event chooses only
+/// where and how hard (SI-0005).
 struct Pluck {
-    std::int32_t segment{};  ///< Segment index (flattened across note groups in description order).
-    double position{};       ///< Pluck position x_p along the string (m).
-    double force{};          ///< Peak force F_p (N) of a pulse, or driver force scale (N).
-    double angle{};          ///< Polarisation angle theta_p (rad).
-    PluckKind kind{PluckKind::ForcePulse};  ///< Exciter model to start.
+    std::int32_t segment{};  ///< Flat segment index: 0.. across groups in InstrumentDesc order,
+                             ///< then within each group in `segments` order (SPEC-API, SI-0004).
+                             ///< Out of range, or a group without a plucked exciter: ignored.
+    double position{};       ///< x_p (m) on the segment's own x axis (as compileString's
+                             ///< registered points); 0 means the description's x_p. Outside
+                             ///< [0, L): event ignored (SPEC-PLK).
+    double angle{};          ///< Polarisation angle theta_p (rad); replaces the description's.
+    float strength{};        ///< Normalised strength nu in (0, 1]; <= 0 ignores the event, > 1
+                             ///< is treated as 1 (SPEC-PLK "Strength map").
 };

 struct Bend {
-    std::int32_t segment{};  ///< Segment index, as for `Pluck`.
+    std::int32_t segment{};  ///< Flat segment index, as for `Pluck`; out of range: ignored.
     double cents{};          ///< ...
 };
```

`Event::payload` keeps its seven alternatives in the same order; the trivially-copyable static assertions stay.

### exciters.hpp (SI-0005 items 2, 4, 5)

```diff
 /// Generic force-pulse pluck (SPEC-PLK "Exciters", row "Force pulse").
 struct PluckPulse {
     double duration{};      ///< Pulse duration t_p (s); 0.5 to 5 ms.
-    double peakForce{};     ///< Peak force F_p (N).
+    double peakForceMax{};  ///< F_p,max (N): peak force at strength 1 (SPEC-PLK "Strength map").
     double position{};      ///< Window centre x_p (m) on the segment's x axis; default position.
     double width{};         ///< Window width b_p (m).
     double angle{};         ///< Polarisation angle theta_p (rad) used by NoteOn launches.
+    double strengthGamma{1.0};  ///< gamma_p of the strength map (dimensionless); default 1.
 };

 /// Plectrum: pick, quill, mizrab or bachi (SPEC-PLK "Exciters", row "Plectrum").
 struct Plectrum {
     double tipMass{};          ///< Tip mass m_p (kg).
     double driverStiffness{};  ///< Spring K_p between tip and driver (N/m).
-    double driverSpeed{};      ///< Driver speed v_d along the pluck direction (m/s).
+    double driverSpeedMax{};   ///< v_d,max (m/s): driver speed at strength 1 (SPEC-PLK).
     double slipHeight{};       ///< Release threshold h_slip (m).
     double contactK{};         ///< Contact stiffness K of the one-sided power law (N/m^alpha).
     double contactAlpha{};     ///< Contact exponent alpha (dimensionless).
+    double position{};         ///< Default pluck position x_p (m) on the segment's x axis.
+    double angle{};            ///< Pluck direction theta_p (rad) used by NoteOn launches.
+    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
 };

-/// Finger (SPEC-PLK "Exciters", row "Finger"). Provisional: ...
+/// Finger (SPEC-PLK "Exciters", row "Finger"): the plectrum's structure with a wide soft
+/// Hunt-Crossley flesh contact (SI-0005).
 struct Finger {
-    double mass{};         ///< Finger tip mass m_f (kg).
-    double stiffness{};    ///< Contact stiffness K_f (N/m^alpha_f).
-    double alpha{};        ///< Contact exponent alpha_f (dimensionless).
-    double muFlesh{};      ///< Hunt-Crossley flesh damping mu_f (s/m).
-    double width{};        ///< Contact width b_f (m); approximately 10 mm.
-    double driverSpeed{};  ///< Driver speed v_d (m/s). Provisional ("as plectrum").
-    double slipHeight{};   ///< Release threshold h_slip (m); larger than a plectrum's.
+    double tipMass{};          ///< Finger tip mass m_f (kg).
+    double driverStiffness{};  ///< Driver spring K_f between tip and driver (N/m).
+    double driverSpeedMax{};   ///< v_d,max (m/s): driver speed at strength 1.
+    double slipHeight{};       ///< Release threshold h_slip,f (m); larger than a plectrum's.
+    double contactK{};         ///< Flesh contact stiffness K_c,f (N/m^alpha_f).
+    double contactAlpha{};     ///< Contact exponent alpha_f (dimensionless).
+    double muFlesh{};          ///< Hunt-Crossley flesh damping mu_f (s/m).
+    double width{};            ///< Raised-cosine contact width b_f (m); approximately 10 mm.
+    double position{};         ///< Default pluck position x_p (m) on the segment's x axis.
+    double angle{};            ///< Pluck direction theta_p (rad) used by NoteOn launches.
+    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
 };

+/// Initial displacement (SPEC-PLK "Exciters", row "Initial displacement"): the segment is set
+/// to the closed-form triangle of height A at x_p, at rest, at the event's sample.
+struct InitialDisplacement {
+    double amplitudeMax{};     ///< A_max (m): triangle height at strength 1.
+    double position{};         ///< Default apex position x_p (m) on the segment's x axis.
+    double angle{};            ///< Polarisation angle theta_p (rad) used by NoteOn launches.
+    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
+};

 struct ExciterParams {
-    std::variant<HammerParams, PluckPulse, Plectrum, Finger> e;  ///< The exciter model.
+    std::variant<HammerParams, PluckPulse, Plectrum, Finger, InitialDisplacement> e;
 };
```

`HammerParams` and `UnaCordaParams` are unchanged. The default `ExciterParams` still holds `HammerParams`.

### instrument.hpp (SI-0004, SI-0003 item 3)

```diff
 struct SegmentDesc {
     ...
     double kappaT{};       ///< ...
+    double unisonDetuneCents{};  ///< Unison detune d_s of this string (cents), added to the
+                                 ///< tuning curve (SPEC-STR "Tension for pitch", SI-0004).
 };

 struct NoteGroupDesc {
-    int key{};  ///< Key number (see TuningCurve for the numbering).
+    int key{};  ///< MIDI note number 0..127 (A4 = 69), unique among groups that receive NoteOn;
+                ///< -1 for groups that never do (plucked strings addressed by segment,
+                ///< sympathetic strings). With key >= 0, compileInstrument computes every
+                ///< segment's f_1 from the tuning curve (SPEC-STR, SI-0004).
 };

-/// Pitch map (...). Provisional members: SPEC-API names the type only.
+/// Pitch map (SPEC-STR "Tension for pitch", SI-0004).
 struct TuningCurve {
     double referencePitch{440.0};  ///< f_ref (Hz) of MIDI key 69 (A4); default 440.
-    std::vector<double> stretchCents;  ///< ... Provisional.
+    std::vector<double> stretchCents;  ///< Per-key stretch (cents) indexed by MIDI key, key 0
+                                       ///< first; keys beyond the vector have 0.
 };

 struct Defaults {
-    double localBridgeFSplit{2000.0};  ///< f_split (Hz) for buildLocalBridge. Provisional: no SPEC default.
+    double localBridgeFSplit{2000.0};  ///< f_split (Hz) for buildLocalBridge; SPEC-INST default
+                                       ///< 2 kHz, provisional until B3-T2 and V3 (SI-0003).
 };
```

### string.hpp (SI-0004 item 3; SI-0001 follow-up)

Doc only:

```diff
-    double f1Target{};  ///< Target first-partial frequency f_1 from the tuning curve (Hz), SPEC-STR.
+    double f1Target{};  ///< Target first-partial frequency f_1 (Hz), SPEC-STR. For a group with
+                        ///< key >= 0, compileInstrument replaces it with the tuning-curve value
+                        ///< in its compiled copy; for key = -1 it is used as given (SI-0004).

-    /// Window-averaged shape values phiBar_n at every registered point, computed float64 and
-    /// stored float32 (SPEC-STR "Forces and observations"). phiBar[p][n] is point p ...
+    /// Point shape values phi_n(x_0) (W = 1) at every registered point, computed float64 and
+    /// stored float32 (SPEC-STR "Contract", SI-0001). The caller applies the window factor
+    /// W(k_n). phiBar[p][n] is point p (order of `registeredPoints`), mode n; unit m^(-1/2).

-/// @param registeredPoints Points x_0 (m) at which phiBar is evaluated (exciter, damper, barrier,
-///                         bridge).
+/// @param registeredPoints Positions x_0 (m), 0 <= x_0 <= L, on the segment's own x axis, at
+///                         which point shapes phi_n(x_0) (W = 1) are evaluated (exciter, damper,
+///                         barrier). The bridge needs none.
```

### diagnostics.hpp (SI-0002 follow-up)

Doc only:

```diff
-    std::int32_t group{};   ///< Note group (key) index the record refers to.
+    std::int32_t group{};   ///< Index into InstrumentDesc::groups (not the key), SPEC-API.
```

### types.hpp (SI-0003 item 1)

Doc only:

```diff
-/// Bridge points and radiation channels are given in the body's coordinate frame, which is
-/// defined by the body description (SPEC-BODY, "Plate model"): x along the grain (L axis), y
-/// across it (R axis), z normal to the plate towards the strings. Provisional: ...
+/// Bridge points, plate outline, ribs, bridges and radiation channels are given in the body
+/// frame (SPEC-BODY "Plate model", SI-0003): x and y span the plate's mid-surface in the
+/// instrument's case frame defined by the body outline; the grain (L axis) lies at angle
+/// theta_g from +x; z is normal to the plate and points towards the strings (the +y
+/// polarisation axis). Bridge points and outline vertices use z = 0.
 struct Point {
-    double x{};  ///< Coordinate along the grain, metres (SPEC-CONV). Provisional frame.
-    double y{};  ///< Coordinate across the grain, metres (SPEC-CONV). Provisional frame.
-    double z{};  ///< Coordinate normal to the plate, metres (SPEC-CONV). Provisional frame.
+    double x{};  ///< Case-frame x, metres (SPEC-CONV).
+    double y{};  ///< Case-frame y, metres (SPEC-CONV).
+    double z{};  ///< Normal to the plate, towards the strings, metres (SPEC-CONV).
 };
```

### body.hpp (SI-0003 item 2)

```diff
-/// A rib or bridge attached to the plate as a beam (...). Provisional: ...
+/// A rib: a straight beam attached to the plate (SPEC-BODY "Plate model", SI-0003).
 struct RibDesc { /* members unchanged */ };

+/// A bridge attached to the plate: a polyline beam of rectangular section (SPEC-BODY "Plate
+/// model", SI-0003).
+struct BridgeBeamDesc {
+    std::vector<Point> path;  ///< Polyline vertices in the body frame (m), z = 0, at least 2.
+    double width{};           ///< Section width (m).
+    double height{};          ///< Section height (m).
+    double youngs{};          ///< Young's modulus along the bridge (Pa).
+    double density{};         ///< Density (kg/m^3).
+};

 struct PlateParams {
-    double lengthX{};    ///< Plate extent along the grain (m). Provisional.
-    double lengthY{};    ///< Plate extent across the grain (m). Provisional.
-    double thickness{};  ///< Thickness h (m); uniform. Provisional (SPEC-BODY has h(x, y)).
+    double lengthX{};    ///< Extent along +x (m); with an empty `outline` the plate is the
+                         ///< rectangle (0, 0)-(lengthX, lengthY); ignored otherwise.
+    double lengthY{};    ///< Extent along +y (m); see `lengthX`.
+    double thickness{};  ///< Uniform thickness h (m), used when `thicknessGrid` is empty.
     ...
-    double rimStiffness{};  ///< Elastic rim support stiffness (N/m per m of rim). Provisional.
+    double rimStiffness{};  ///< Translational rim support stiffness per unit rim length k_rim
+                            ///< (N/m^2), SPEC-BODY.
     double fMax{};
-    std::vector<RibDesc> ribs;  ///< Ribs and bridges as attached beams, SPEC-BODY.
+    std::vector<RibDesc> ribs;  ///< Ribs as straight attached beams, SPEC-BODY.
+    std::vector<Point> outline;  ///< Outline polygon in the body frame (m), z = 0, vertices in
+                                 ///< order, not closed; empty = the lengthX x lengthY rectangle.
+    int thicknessNx{};           ///< Thickness grid points along x (>= 2 when the grid is used).
+    int thicknessNy{};           ///< Thickness grid points along y (>= 2 when the grid is used).
+    std::vector<double> thicknessGrid;  ///< h (m) on a regular Nx x Ny grid over the outline's
+                                        ///< bounding box, row-major [j * Nx + i], bilinearly
+                                        ///< interpolated; empty = uniform `thickness`.
+    std::vector<BridgeBeamDesc> bridges;  ///< Bridges as polyline beams, SPEC-BODY.
+    double rimRotationalStiffness{};  ///< Rotational rim stiffness per unit length k_rim,theta
+                                      ///< (N m/rad per m); 0 = none (MAY, SPEC-BODY).
 };

 struct ChannelDesc {  // Doc on the struct comment only:
+/// Per-channel gain is a structural property of the channel and will be added here as
+/// `gainDb` by a later compatible ICR when D4 or E1 needs it (SPEC-API, SI-0007).
 };
```

Existing `PlateParams` members keep their order; the new members are appended after `ribs`, so positional initialisers of interfaces-v1 still compile, but the meaning of `lengthX`/`lengthY` changes from grain axes to case axes (breaking, covered by the major bump).

### params.hpp, params.json and the registry (SI-0007)

No declaration change in `params.hpp`. Registry (`engine/src/runtime/params.cpp`) and generated `params.json`:

| Change | Entry |
| --- | --- |
| Remove; id reserved, never reused | 1010 `unaCorda.shift` |
| Description: append "Also the una corda shift s (grand) and hammer-rail position (upright); the only id for that quantity." | 1003 `pedal.soft` |
| Description: drop "Range provisional." / "Class provisional." (ranges and classes confirmed) | 2001, 3101, 4001, 4002, 4003, 4101, 4102, 4103, 5003, 5004, 6001, 6002 |
| Description: "Highest kept string frequency f_max; default 16 kHz (SPEC-INST), V2 may revise. Structural: recompiles the instrument; EngineConfig::fMaxStrings is updated." | 6003 `strings.fMax` |
| `"interfaces_version": 2` (from `interfacesVersionMajor`) | file header |

Ids, names, units, min, max, defaults and classes of all remaining entries are unchanged. docs/dev/interfaces.md lists 1010 under a new "Retired ids" line.

### Stubs

- `engine/src/runtime/runtime_stub.cpp`: define `Engine::collectRetired()` (no-op in the stub, `noexcept`); update the `setInstrument` comment ("non-audio only; the stub releases the argument on the calling thread and never swaps"). No other behaviour changes. The retire queue itself is C1's work (SPEC-EXEC "Instrument lifetime"), not the stub's.
- `engine/src/runtime/params.cpp`: registry edits above.
- `osc`, `string`, `bridge`, `body` stubs: no change (signatures unchanged).

### SPEC text

Applied by the planner in the same PLAN commit as this ICR: SPEC-API (descriptions, real-time interface, events table, key and segment numbering, parameter classes), SPEC-EXEC (step 1 and new "Instrument lifetime"), SPEC-PLK ("Exciters": strength map, Pluck semantics, finger structure, initial displacement), SPEC-BODY ("Plate model": frame and geometry inputs), SPEC-STR ("Tension for pitch": key numbering and f₁ ownership), SPEC-INST ("Piano defaults": f\_split, string f\_max default; "Instrument file": no f\_max).

### Tests that must change (engine/tests/interfaces/)

| File | Change |
| --- | --- |
| `test_headers.cpp` | `P0.4-T1 interfaces version is 1` → `ICR-0001 interfaces version is 2`, checks `interfacesVersionMajor == 2` |
| `test_events.cpp` | Pluck case: construct `pme::Pluck{3, 0.12, 0.5, 0.75f}`; check `segment`, `position`, `angle`, `strength`; no `kind`/`force`. Keep the trivially-copyable static assertions. |
| `test_engine.cpp` | Event list line 42: `pme::Pluck{0, 0.1, 0.0, 1.0f}`. Confirm no `setInstrument` call runs inside `ScopedRealtimeThread` (the allocation-count case calls it outside any RT scope; keep it outside). |
| `test_stubs.cpp` | Add `static_assert(noexcept(std::declval<pme::Engine&>().collectRetired()))`; call `collectRetired()` on a prepared and an unprepared engine (coverage); defaults case also checks `SegmentDesc{}.unisonDetuneCents == 0.0`, `PlateParams{}.outline.empty()`, `thicknessGrid.empty()`, `bridges.empty()`, `rimRotationalStiffness == 0.0`, `std::variant_size_v<decltype(pme::ExciterParams{}.e)> == 5`, and `PluckPulse{}.strengthGamma == 1.0` (same for `Plectrum`, `Finger`, `InitialDisplacement`). |
| `test_params.cpp` | `P0.4-T3 ... continuous parameters`: drop 1010 from the list; new case `ICR-0001 retired id 1010 is not registered` checks `findParam(1010) == nullptr`. The `params.json` comparison test (`P0.4-R4_params_json_matches_registry`) passes after regeneration. |
| `test_headers` P0.4-T1 count | Unchanged: no header is added or removed. |

## Phases affected

| Phase | Impact | Must rebase |
| --- | --- | --- |
| P0.4 (complete) | Outputs amended by this ICR: headers, runtime stub, registry, params.json, docs/dev/interfaces.md, interface tests | n/a (applied as `ICR-0001:` commits) |
| P0.3 (in flight) | `engine/tests/oracles.md` ORC-16 row changes owner (C1); no interface use | Yes, if it edits oracles.md |
| B1 | Schema mirrors the new `SegmentDesc`, `PlateParams`, `BridgeBeamDesc`, exciters and key rules | Starts on interfaces-v2 |
| B2, B3 | Plate geometry inputs; f\_split default | Start on interfaces-v2 |
| C1, C4, C6 | Instrument lifetime; tuning ownership; event addressing; Pluck semantics | Must not be briefed before interfaces-v2 is tagged |
| C8, D1, D6 | `collectRetired` call sites; registry without 1010 | Start on interfaces-v2 |
| A2 | `phiBar` and `f1Target` comments only | No (not started) |
| P1, P5, P6, E4–E8 | New exciter structs, strength map, InitialDisplacement | Start on interfaces-v2 |
| V2, V3 | strings.fMax default and f\_split confirmation | No |

No phase except P0.3 is in progress; nothing else needs rebasing.

## Decision

**Approved by the orchestrator** on 2026-10-11. Reason: the owner instructed the orchestrator to proceed to full implementation without stopping, applying the recommended defaults and flagging them. The orchestrator accepted SI-0003, SI-0004, SI-0005, SI-0006 and SI-0007 with the planner's recommended resolutions and chose to batch every header change into one breaking bump (interfaces-v2) so that C1, C4, C6 and B1 start on settled interfaces.

**Flagged for owner review** (orchestrator default under the owner's proceed instruction; physics or numerics choices that the SI protocol sends to a human reviewer):

1. SI-0005: the strength map F\_p = F\_p,max ν^γ\_p, v\_d = v\_d,max ν^γ\_p, A = A\_max ν^γ\_p with default γ\_p = 1.
2. SI-0005: finger structure (tip mass on driver spring K\_f; flesh contact K\_c,f, α\_f, Hunt–Crossley μ\_f over a raised-cosine b\_f; v\_d; h\_slip,f).
3. SI-0005: InitialDisplacement replaces the segment's modal state with the closed-form triangle at rest; split into polarisations by θ\_p; NoteOn on a plucked group launches its exciter at the description's position and angle on every speaking segment.
4. SI-0005: a Pluck at a position other than the description's evaluates φ̄\_n(x\_p) in float64 at the event (O(N), preallocated buffer).
5. SI-0003: plate geometry inputs (outline, bilinear thickness grid, polyline bridges, k\_rim in N/m², optional k\_rim,θ).
6. SI-0003: f\_split = 2 kHz, provisional; B3-T2 measures, V3 confirms before G5.
7. SI-0004: key numbering (MIDI, A4 = 69) and f₁ = f\_ref 2^((key−69)/12) 2^((stretch + d\_s)/1200) computed by compileInstrument.

Interfaces tag after applying: **interfaces-v2**, created by the orchestrator on the head of the `ICR-0001:` commits and pushed by the owner. Tag SHA: _recorded here and in docs/status/phase-status.md once applied._

## Application

**Applied** on 2026-10-11 by the interface agent (implementer, Sonnet) in commits ad193d5 to 300b995 (`P0.4: ICR-0001 …`, items I1 to I12). The code reviewer approved with no blocking items. CI: pr.yml run 38149794842 green on all 12 jobs at 300b995.

Interfaces tag: **interfaces-v2 = 300b995**, created locally; push pending the owner (as for interfaces-v1 = 2daee85). SI-0003 to SI-0007 are resolved by this application.

Non-blocking review items, carried to the next ICR (documentation only; none changes a declaration):

1. Stale "interfaces-v1" wording in comments: `engine/include/pme/nonlinear.hpp` lines 7 and 16, `engine/include/pme/pme.hpp` line 5.
2. Registry entry 1003 `pedal.soft`: the appended description repeats the una corda clause; reduce it to one sentence.
3. `docs/dev/interfaces.md` lists non-audio functions (`prepare`, `setInstrument`, `collectRetired`, `popDiagnostic`) under "Real-time functions"; split the section by thread.
4. ICR test-name templates: the "Tests that must change" table above named new cases `ICR-0001 …`, which breaks the P0.3-R1 rule (test names begin with a requirement or test ID). The interface agent renamed them with a P0.4 prefix in 300b995. Future ICRs follow the naming rule in [README.md](README.md).

