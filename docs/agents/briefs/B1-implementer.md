# Brief: B1 Instrument description schema and loader — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/B1-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the versioned, human-readable instrument format from SPEC-INST, with loaders in C++ and Python and canonical example instruments.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Repository layout, Languages and dependencies, Real-time rules 1–2, Code style, Testing policy, Documentation rules, Licensing); the phase card **B1 Instrument description schema and loader**; for context the cards **B2** (consumes the plate description), **C1** and **C8** (consume `InstrumentDesc`), **D5** (instrument management) and **P6** (updates test-guitar-6).
- SPEC and PLAN sections:
  - SPEC-INST — docs/plan/engine-specification.md, "Instrument definitions" (instrument table, "Piano defaults", "Instrument file")
  - SPEC-API — docs/plan/engine-specification.md, "Engine interfaces" ("Descriptions", "Event addressing", "Parameter classes")
  - For field meanings and units only: SPEC-STR "Stiff string model" ("Tension for pitch", "Duplex and rear segments"), SPEC-HAM "Hammer and action", SPEC-DMP "Dampers and pedals", SPEC-PLK "Plucked exciters and barriers" ("Exciters", "Exciter parameters and events"), SPEC-BODY "Soundboard and bodies" ("Plate model", "Body frame", "Geometry inputs"), SPEC-BRG "Bridge coupling".
- The frozen description headers (interfaces-v2 = 300b995): engine/include/pme/instrument.hpp, string.hpp, exciters.hpp, dampers.hpp, barriers.hpp, bridge.hpp, body.hpp, types.hpp. **The structs and their member comments are the field list, the units and the defaults of your schema.**
- docs/icr/ICR-0001-interfaces-v2.md — what changed from interfaces-v1 (2daee85); you need it for the version-0 format (B1-R2).
- docs/adr/ADR-0005-json-library.md (nlohmann/json, already pinned in cmake/dependencies.cmake), docs/adr/ADR-0006-interface-status-returns.md (`Status` returns, out-parameters).
- docs/spec-issues/SI-0004-tuning-keys-and-segment-addressing.md (key rules), SI-0003-body-description-types.md (body frame, outline, thickness grid), SI-0010-rear-segment-tension-and-mute.md (segment-order rule, open).
- docs/dev/testing.md (test naming P0.3-R1, `pme_add_doctest`, labels), docs/dev/build.md (dependencies).
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs: **G0** passed (docs/gates/G0.md, at 15a1a43): P0.1 (layout, `compiler/pme_compiler` package skeleton, `instruments/` and `data/` placeholders, uv project), P0.2 (CI: build jobs run ctest on all presets; python jobs run `uv run pytest`, `ruff`, `mypy --strict` on Windows, macOS and Linux), P0.3 (`pme_add_doctest`, nlohmann/json v3.12.0 pinned, engine-isolation check in cmake/pme_testing.cmake), P0.4 (headers). All on branch `ccr-5ef35d73-oqb6fd`; no pull requests.
- Frozen interfaces: **interfaces-v2 = 300b995** (ICR-0001 applied; tag created locally, push pending the owner). Your worktree must start from a commit that contains 300b995; if `interfacesVersionMajor` in engine/include/pme/version.hpp is not 2, stop and report.
- Not yet available: B2 (plate compiler), C1 (compileInstrument). Your loader produces `InstrumentDesc`; nothing compiles it yet.

## Concurrent work: P0.5, D1, B1, A2 and A3 run at the same time

P0.5 and D1 are in progress; B1 starts now; A2 and A3 (each with a test-author) start when A1 is complete. Each runs in its own git worktree and the orchestrator merges commits into `ccr-5ef35d73-oqb6fd`. Edits must not overlap. This table is identical in the B1, A2 and A3 briefs; P0.5 and D1 keep the rows of their own briefs, which this table repeats.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `compiler/pme_compiler/schema/**`, `compiler/pme_compiler/examples/**`, `compiler/tests/test_b1_*.py`, `compiler/tests/data/b1/**`, `engine/src/runtime/loader/**`, `engine/tests/loader/**`, `instruments/examples/**`, `docs/dev/instrument-format.md` | B1 implementer | read only |
| `engine/src/string/**` (replaces `string_stub.cpp`), `engine/tests/string/**`, `docs/dev/string.md` | A2 implementer | read only |
| In `engine/tests/interfaces/test_stubs.cpp`: the `compileString` SUBCASE only. In `docs/dev/interfaces.md`: the word `string` in the stub-module list of "Stubs (P0.4-R3)" only | A2 implementer | read only |
| `engine/tests/oracles/string/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-03, ORC-04, ORC-05 only; one line `add_subdirectory(string)` appended to `engine/tests/oracles/CMakeLists.txt` | A2 test-author | read only |
| `engine/src/nonlinear/**` (replaces `nonlinear_placeholder.cpp`), `engine/tests/nonlinear/**`, `docs/dev/nonlinear.md`; in `docs/dev/interfaces.md` the word `nonlinear` in the placeholder list of "Stubs (P0.4-R3)" only | A3 implementer | read only |
| `engine/tests/oracles/nonlinear/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-08 only; one line `add_subdirectory(nonlinear)` appended to `engine/tests/oracles/CMakeLists.txt` | A3 test-author | read only |
| `engine/src/osc/**`, `engine/tests/osc/**`, `engine/tests/oracles/osc/**`, `docs/dev/osc.md` | A1 (in review: review fixes only) | read only; A2 and A3 use the internal headers under `engine/src/osc/include/pme/osc/` |
| `reference/**`, `tools/reference-cli/**`, `tools/tests/test_p0_5_*.py`, `docs/dev/reference-solver.md` | P0.5 | read only |
| `pyproject.toml`, `uv.lock` | P0.5 | B1 adds exactly two dependency lines and regenerates `uv.lock` with `uv lock` (see B1's "Shared files"); nobody else |
| `plugin/**`, `docs/dev/clap-shell.md`, `cmake/dependencies.cmake`, `docs/dev/build.md` | D1 | read only |
| `.github/workflows/pr.yml`, `tools/ci/required_checks.json`, `CONTRIBUTING.md`, `docs/dev/ci.md` | existing content: nobody; P0.5 and D1 append their delimited blocks as their briefs say | B1, A2, A3 and the test-authors add nothing (their tests run in the existing build and python jobs) |
| Root `CMakeLists.txt` | nobody above the current last line; P0.5, D1, B1, A2 and A3 implementers each **append one delimited block at the end** | test-authors add none (they use `engine/tests/oracles/CMakeLists.txt`) |
| `tools/trace/**`, `tools/tests/test_v1_*.py`, `docs/dev/traceability.md`, `.github/workflows/nightly.yml`, `tools/ci/**`, `cmake/pme_testing.cmake`, `engine/tests/framework/**`, `engine/tests/rtcheck/**`, `tools/analysis/**`, `tools/bench/**`, `docs/dev/testing.md`, `docs/dev/signal-analysis.md` | V1 and P0.3 (complete; follow-ups only when the orchestrator routes them) | read only |
| `CMakePresets.json`, `cmake/pme_module.cmake`, `cmake/compiler_options.cmake`, `cmake/sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody | read only |
| `engine/include/pme/**`, the other `engine/src/*/` stubs (including `engine/src/runtime/*.cpp`), `engine/tests/interfaces/**` (except the A2 row), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2 = 300b995; changes only by ICR) | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

When two phases append at the same place (end of the root `CMakeLists.txt`, end of `engine/tests/oracles/CMakeLists.txt`), the orchestrator keeps both blocks when merging. Keep blocks self-contained so that order does not matter.

### Shared files for B1

- **pyproject.toml and uv.lock** (owned by P0.5). B1 needs the JSON Schema validator **jsonschema** (MIT) in Python. Make exactly these edits, in **one commit of its own** titled `B1: jsonschema dependency` that touches nothing else: append `"jsonschema>=4.23",` as the last entry of `[project] dependencies`, append `"types-jsonschema>=4.23",` as the last entry of `[dependency-groups] dev`, then run `uv lock` (never hand-edit uv.lock). Merge order: the orchestrator merges P0.5 first if both are ready. Whichever of P0.5 and B1 merges second takes the other's `uv.lock`, re-applies its own pyproject lines, reruns `uv lock` and commits the result; uv.lock is never merged by hand.
- **cmake/dependencies.cmake** (owned by D1): no edit. nlohmann/json is already pinned (ADR-0005); link `nlohmann_json::nlohmann_json`.
- **Root CMakeLists.txt**: your block, appended after the last line and nothing else changed:

  ```cmake
  # ---- B1: instrument file loader ----
  add_subdirectory(engine/src/runtime/loader)
  add_subdirectory(engine/tests/loader)
  # ---- end B1 ----
  ```

- **pr.yml**: no edit. C++ loader tests run in the existing build jobs (ctest), Python tests in the existing python jobs (`uv run pytest` collects `compiler/tests`).

## Scope

- May edit (create or change): the paths the ownership table gives the **B1 implementer**, the two dependency lines and `uv.lock` above, and your root `CMakeLists.txt` block.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B1-R1 JSON Schema (draft 2020-12) mirroring the SPEC-API description structs, with SI units recorded for every numeric field.
2. B1-R2 Schema version field and migration functions between versions.
3. B1-R3 Blob manifest: content hash, array shapes, data types, units, producing tool and version.
4. B1-R4 Non-real-time C++ loader producing InstrumentDesc; Python models mirroring the same structure.
5. B1-R5 Validation errors report the exact JSON path and reason.
6. B1-R6 Example instruments: test-monochord, test-unison-pair, generic-grand-88 (generated by a parametric script) and test-guitar-6.

### How to meet each requirement

**Format rules (apply to R1 to R5).** One JSON document per instrument, UTF-8.

- Top level: `"format": "pme-instrument"`, `"schemaVersion": 1`, `"instrument": {…}` (the `InstrumentDesc`), `"blobs": […]` (the manifest, R3; may be empty).
- Field names are the C++ member names exactly (`dCore`, `rhoCore`, `ECore`, `f1Target`, `kappaL`, `unisonDetuneCents`, …), nested as the structs nest. Enums are their enumerator names as strings (`"Pinned"`, `"At0"`, `"HuntCrossley"`, `"PlateModel"`, …). `ExciterParams` is an object with exactly one key naming the alternative: `hammer`, `pluckPulse`, `plectrum`, `finger`, `initialDisplacement`. `std::optional` members (`NoteGroupDesc::exciter`, `SegmentDesc::damper`) are omitted when empty. `Point` is `{"x": …, "y": …, "z": …}`.
- A member with a default member initialiser in the header (for example `longitudinalModes{8}`, `kappaL{0.026}`, `TuningCurve::referencePitch{440.0}`, the `Defaults`, `UnaCordaParams`, `DamperParams` timing defaults, `strengthGamma{1.0}`) is optional in the file and takes the header's default when absent. Every other member is required. Unknown keys are errors (`"additionalProperties": false` / `unevaluatedProperties: false`).
- `BodyParams::modalTableHash` is the manifest hash string `"sha256:<64 lower-case hex>"`; it must name an entry of `blobs` when `source` is not `PlateModel`, and be empty when it is.

**B1-R1 — schema.** `compiler/pme_compiler/schema/instrument.schema.json`, draft 2020-12, one `$defs` entry per struct (`StringParams`, `SegmentDesc`, `NoteGroupDesc`, `HammerParams`, `UnaCordaParams`, `PluckPulse`, `Plectrum`, `Finger`, `InitialDisplacement`, `ExciterParams`, `DamperParams`, `BarrierParams`, `BridgePointDesc`, `Point`, `RibDesc`, `BridgeBeamDesc`, `PlateParams`, `BodyParams`, `ChannelDesc`, `TuningCurve`, `Defaults`, `InstrumentDesc`, `BlobEntry`, `BlobArray`), with `properties` in struct member order. Every numeric property (and every array of numbers) carries the annotation keyword `"x-unit"` with the SI unit from the header comment (`"m"`, `"kg/m^3"`, `"Pa"`, `"N/m^p"`, `"Hz"`, `"1/s"`, `"s^(-1/2)"`, `"s"`, `"rad"`, `"cent"`, `"N/m^2"`, …; `"1"` for dimensionless, `"index"` for integer indices and counts) and a `description` that cites the SPEC key. Value constraints are physical only and taken from SPEC or the header comments (for example L > 0, densities > 0, `packing` in (0, 1], `muteRatio` in [0, 1], `key` in −1 … 127, `side` ∈ {−1, +1}, `thicknessNx`/`Ny` ≥ 2 when a grid is given); do not invent tighter "typical" ranges. List every constraint and its source in docs/dev/instrument-format.md.

Cross-field rules JSON Schema cannot express are checked by both loaders after schema validation, with the codes below: `key` unique among groups with key ≥ 0 (SPEC-API "Event addressing"); every `bridgePoint` index < `bridgePoints` size; `dOuter ≥ dCore`; `thicknessGrid` size = Nx·Ny when non-empty; `BarrierParams` arrays `x`, `b`, `w` of equal length; `modalTableHash` resolves in the manifest; **segment order (SI-0010, proposed)**: a group's first segment has `bridgeEnd` `AtL`, so every `At0` (rear or duplex) segment follows a speaking segment. If the orchestrator rejects SI-0010 item 1, the last rule is removed in review; implement it as one separately named check.

**B1-R2 — versions and migrations.** Current version **1** = the interfaces-v2 structs above. Define **version 0** as the same format mirroring the interfaces-v1 structs (2daee85; see the diffs in ICR-0001): no `unisonDetuneCents`; `PluckPulse.peakForce` instead of `peakForceMax`, no `strengthGamma`; `Plectrum.driverSpeed` instead of `driverSpeedMax`, no `position`, `angle`, `strengthGamma`; `Finger` with `mass`, `stiffness`, `alpha`, `muFlesh`, `width`, `driverSpeed`, `slipHeight`; `PlateParams` without `outline`, `thicknessNx`, `thicknessNy`, `thicknessGrid`, `bridges`, `rimRotationalStiffness`. Version 0 never shipped; it exists so that the migration path is exercised from the first release. Migration 0 → 1, implemented once in Python (`pme_compiler.schema.migrate`) and once in the C++ loader, both applied automatically on load and reported (the loader returns the source version):

| v0 | v1 |
| --- | --- |
| no `unisonDetuneCents` | `unisonDetuneCents: 0` |
| `PluckPulse.peakForce` | `peakForceMax` (same value), `strengthGamma: 1` |
| `Plectrum.driverSpeed`, no `position`/`angle` | **error** `migration` at the plectrum's path: no v0 source for `position` and `angle`; the user must supply them |
| `Finger` (v0 members) | **error** `migration` at the finger's path: no v0 source for `driverStiffness`, `position`, `angle` |
| `PlateParams` (v0 members) | same values; new members empty or 0 (rectangle plate, uniform thickness, no bridges as beams, no rotational rim stiffness) |

A file with `schemaVersion` greater than the current version, or not an integer, is an error `version`. Writers always write the current version.

**B1-R3 — blob manifest.** Each `blobs` entry (`BlobEntry`): `hash` (`"sha256:<hex>"` of the whole blob file), `path` (relative to the instrument file's directory, forward slashes, no `..`), `producer` `{ "tool": …, "version": … }`, `arrays` (`BlobArray` list: `name`, `dtype` ∈ {`float32`, `float64`, `int32`, `uint32`}, `shape` (list of positive integers), `unit` (as `x-unit`), `offset` (bytes from the start of the file, aligned to the dtype size)). Blob files are raw little-endian arrays at the given offsets. Both loaders validate the manifest's internal consistency (offsets plus sizes within the file length when the file is present, no overlap, alignment) and, when asked to (`verifyBlobs = true`, the default for the Python compiler and false for the C++ engine-side load), recompute the SHA-256 of each file (Python `hashlib`; C++: a small SHA-256 in the loader, tested with the FIPS 180-4 example vectors). The manifest is not part of `InstrumentDesc` (frozen); the C++ loader returns it beside it.

**B1-R4 — loaders.**

- **C++** (`engine/src/runtime/loader/`, internal header `engine/src/runtime/loader/include/pme/loader/instrument_file.hpp`, namespace `pme::loader`, target `pme_loader`, a static library with its own CMakeLists.txt linking `nlohmann_json::nlohmann_json` and including `engine/include`). It must **not** be linked into `pme_engine` or any `pme_<module>` target, and `pme_runtime`'s non-recursive glob does not pick up `loader/` sources (keep it that way). Non-real-time; exceptions from nlohmann/json are caught inside and never cross the API. Suggested API (ADR-0006 style):

  ```cpp
  struct LoadError { std::string path; std::string code; std::string message; };
  struct BlobArray { std::string name, dtype, unit; std::vector<std::int64_t> shape; std::int64_t offset{}; };
  struct BlobEntry { std::string hash, path, tool, toolVersion; std::vector<BlobArray> arrays; };
  struct InstrumentFile { InstrumentDesc desc; std::vector<BlobEntry> blobs; int sourceVersion{}; };
  [[nodiscard]] Status loadInstrument(std::string_view json, InstrumentFile& out, std::vector<LoadError>& errors);
  [[nodiscard]] Status loadInstrumentFile(const std::filesystem::path&, bool verifyBlobs, InstrumentFile& out, std::vector<LoadError>& errors);
  [[nodiscard]] Status writeInstrument(const InstrumentFile&, std::string& json);  // canonical form
  ```

  `InvalidArgument` with at least one `LoadError` on any validation failure; `out` unchanged unless `Ok`.
- **Python** (`compiler/pme_compiler/schema/`): frozen dataclasses mirroring every struct with the same field names, types and defaults (`models.py`), `load(path_or_text) -> InstrumentFile`, `dumps(InstrumentFile) -> str` (canonical form), `validate(document) -> list[LoadError]` (jsonschema `Draft202012Validator` plus the cross-field rules), `migrate`. Strict mypy, ruff clean.
- **Canonical form** (both writers): 2-space indentation, object keys in schema `properties` order, every non-optional member written (defaults included), optional members omitted when empty, numbers as the shortest decimal that round-trips to the same float64 (integers for `int` members), LF line ends, final newline. Write it down in docs/dev/instrument-format.md. Both writers must produce byte-identical output for the same data; where Python's `repr(float)` and nlohmann/json's number format differ (exponent form, `1e-05` versus `1e-5`), the Python writer follows nlohmann/json, and docs/dev/instrument-format.md says so.

**B1-R5 — errors.** Every error carries `path` as a JSON Pointer (RFC 6901, for example `/instrument/groups/3/segments/0/string/L`), a `code` from a closed list shared by both languages (`parse`, `type`, `missing`, `unknown_key`, `range`, `enum`, `version`, `migration`, `reference`, `duplicate`, `structure`, `blob`), and a human-readable `message` naming the expected value. For the same malformed input the C++ and Python loaders report the same set of (path, code) pairs; messages may differ. Report all errors found, not just the first (cap at 100).

**B1-R6 — examples.** `instruments/examples/<name>/instrument.json`, written by the canonical writer. The parameter values below are **nominal, chosen by the planner for testing, not measured or fitted**; E1 and P6 replace them. JSON has no comments, so docs/dev/instrument-format.md says so for each example.

- **test-monochord** (`name` `"test-monochord"`): one group, key 45 (A2, 110 Hz), one plain steel speaking string, L = 1.0 m, dCore = dOuter = 1.0 mm, ρ = 7850 kg/m³, E = 200 GPa, b₁ = 0.5 s⁻¹, b₂ = 0, b₃ = 2 × 10⁻⁹ s; hammer m = 8 g, p = 2.5, K chosen so the SPEC-HAM oracle contact time at 3 m/s is 2 ms, μ\_HC = 0.2 s/m, width 10 mm, strike ratio 1/8; no damper; one bridge point (θ\_b = 0, G\_h = 10⁻³ s/kg); body `PlateModel`, a rectangle 0.6 × 0.4 m, uniform 6 mm spruce (E\_L = 11 GPa, E\_R = 0.9 GPa, G\_LR = 0.7 GPa, ν\_LR = 0.37, ν\_RL = 0.03, ρ = 420 kg/m³, MC 0.12, moisture slopes 0, η = 0.01, f\_max 3 kHz, grain angle 0, no outline, grid, ribs, bridges, rim stiffness 0); one channel `"mono"` at (0.3, 0.2, 1.0) m. b₂ = 0 and the plain rectangle keep it comparable with P0.5.
- **test-unison-pair**: as test-monochord, but key 60 (C4), two identical plain steel speaking strings L = 0.64 m, d = 1.05 mm, `unisonDetuneCents` 0 for both, both on bridge point 0.
- **generic-grand-88**: 88 groups, keys 21 to 108, written by `python -m pme_compiler.examples.generic_grand --out instruments/examples/generic-grand-88/` (`compiler/pme_compiler/examples/generic_grand.py`, every constant named at the top of the file with its source "nominal"). Rules:
  - Strings per key: 1 (keys 21–28), 2 (29–40), 3 (41–108); unison detune 0; no stretch (`stretchCents` empty), reference pitch 440 Hz; each segment's `f1Target` is the equal-tempered value (compileInstrument recomputes it, SI-0004).
  - Speaking length: log-linear interpolation between anchors (key: m) 21: 2.00, 28: 1.85, 29: 1.83, 40: 1.50, 41: 1.45, 48: 1.15, 60: 0.64, 72: 0.33, 84: 0.17, 96: 0.090, 108: 0.052.
  - Plain steel (keys 41–108): d log-linear from 1.20 mm (41) to 0.80 mm (108), ρ = 7850 kg/m³, E = 200 GPa.
  - Wound (keys 21–40): steel core d\_c log-linear from 1.60 mm (21) to 1.15 mm (40); copper winding ρ\_w = 8960 kg/m³, η\_w = 0.85, ε\_w = 0; target tension linear from 1600 N (21) to 850 N (40); the script solves d\_o from ρA = (T + π²EI/L²)/(4L²f₁²) and the SPEC-STR wound-string formula.
  - The script asserts every speaking string's closed-form tension lies in 400–2000 N and prints min, max and B per region.
  - Losses b₁ = 0.4 s⁻¹, b₂ = 0, b₃ = 2 × 10⁻⁹ s; boundary pinned; detuneH 0.
  - Rear segments: none in this version (SI-0010 is open; add them when it is resolved).
  - Hammer: Hunt–Crossley, p = 2.5, mass log-linear 11 g (21) to 4.5 g (108), K per key so the SPEC-HAM oracle contact time at 3 m/s goes log-linearly from 4 ms (21) to 0.5 ms (108), μ\_HC = 0.2 s/m, width linear 15 mm to 5 mm, strike ratio 1/8 up to key 88 then linear to 1/12 at 108; velocity-map defaults.
  - Dampers on keys 21–88 (`hasDamper` true; x\_d/L = 0.05, width 20 mm, K\_d = 5000 N/m, R\_d = 5 kg/s, timing defaults); keys 89–108 have `damper` with `hasDamper` false (SPEC-DMP "Undamped notes").
  - Bridge points: one per key (88), θ\_b = 0, G\_h = 10⁻³ s/kg, positioned along the bridge polylines of the body (keys 21–40 on the bass bridge, 41–108 on the long bridge), z = 0.
  - Body: `PlateModel` with an 8- to 12-vertex outline of a grand soundboard about 1.5 m × 1.4 m, uniform 9 mm, the spruce values of test-monochord, grain angle 0.7 rad, about 12 ribs across the grain (25 × 20 mm spruce), two bridges as polylines (maple, E = 12 GPa, ρ = 700 kg/m³, 25 × 30 mm), rim stiffness 10⁷ N/m², f\_max 3 kHz; `Defaults` from the header.
  - Channels: `"L"` at (−0.5, 0.7, 1.0) m and `"R"` at (2.0, 0.7, 1.0) m (body frame).
  - A test regenerates the file and checks it is byte-identical to the committed one.
- **test-guitar-6**: six groups with key −1 (plucked strings addressed by segment, SI-0004), one speaking segment each, L = 0.650 m, `f1Target` 82.407, 110.000, 146.832, 195.998, 246.942, 329.628 Hz (strings 6 to 1), target tension 70 N each; strings 1–3 plain nylon (ρ = 1140 kg/m³, E = 4 GPa, d solved from the tension); strings 4–6 nylon core d\_c = 0.50 mm with copper winding (η\_w = 0.85), d\_o solved from the tension; b₁ = 1.0 s⁻¹, b₂ = 0, b₃ = 5 × 10⁻⁹ s; exciter `finger` (tip mass 2 g, driver stiffness 2000 N/m, v\_d,max 1 m/s, h\_slip 2 mm, contact K 10⁵ N/m^α, α 1.5, μ\_f 0.5 s/m, width 10 mm, position L − 0.12 m, angle 0, γ\_p 1); no dampers, no barriers (P3 and P6 add frets); six bridge points along the saddle; body a 0.48 × 0.36 m rectangle of 2.5 mm spruce (values as above, f\_max 3 kHz); one channel `"mono"`. Generated by `python -m pme_compiler.examples.test_guitar` (the same regeneration test).

test-monochord and test-unison-pair may be written by hand or by a script; all four must load in both languages without errors.

## Tests that must pass

- **B1-T1** Python → JSON → C++ → JSON round trip is identical;
- **B1-T2** malformed files fail with the expected paths;
- **B1-T3** a version-0 fixture migrates to the current version.

### How each test is built

Shared fixtures live in `compiler/tests/data/b1/` (C++ tests find them through a compile definition with `PROJECT_SOURCE_DIR`).

**B1-T1.** CI runs C++ and Python in different jobs, so the round trip is split at a committed file, and both halves run on all three systems:

1. Python (`compiler/tests/test_b1_t1.py`): for each example instrument and for `all-fields.json` (a fixture in which every member of every struct has a non-default value, every enum value and every exciter alternative occurs at least once, and the manifest has two entries), build the models in Python (or load them), `dumps`, and compare byte-for-byte with the committed file; then `load(dumps(x)) == x` (dataclass equality, float64 bit-identical).
2. C++ (`engine/tests/loader/`, doctest `B1-T1 …`, label `unit`): load each committed file, `writeInstrument`, and compare byte-for-byte with the committed file; for `all-fields.json` also check every member of the resulting `InstrumentDesc` against the expected value field by field (this is what proves the C++ structure mirrors the file).

Pass: both halves identical for every file. "Identical" means byte-identical text, which implies identical data.

**B1-T2.** A table of malformed fixtures (`compiler/tests/data/b1/malformed/*.json` with `expected.json` listing per file the expected (path, code) pairs): at least one fixture per error code, including a parse error, a wrong type deep in a segment, a missing required member, an unknown key, an out-of-range value (`packing` = 1.2, `key` = 128), a bad enum string, two groups with the same key, a `bridgePoint` index out of range, an `At0` first segment, a `modalTableHash` not in the manifest, a blob offset past the end of its file, and a file with three errors at once. Python test `test_b1_t2_*` and C++ `B1-T2 …` both load every fixture and assert the reported set of (path, code) pairs equals `expected.json`.

**B1-T3.** `compiler/tests/data/b1/v0/piano-v0.json`: a version-0 file with two hammer groups, one `pluckPulse` group (with `peakForce`) and a v0 plate; `piano-v0.expected.json`: the same instrument written in version 1 by hand. Both loaders load the v0 file (source version 0 reported) and their canonical output equals the expected file byte-for-byte. Also: `plectrum-v0.json` and `finger-v0.json` fail with code `migration` at the exciter's path in both languages; a `schemaVersion` of 2 fails with `version`.

### Other tests

- `B1-R1 …` (Python): the schema is itself valid draft 2020-12 (`Draft202012Validator.check_schema`); every numeric property has `x-unit` from the allowed vocabulary; the set of properties of each `$defs` entry equals the field names of the matching dataclass (and, through `all-fields.json` in B1-T1, the C++ members); every example instrument validates.
- `B1-R3 …`: SHA-256 against the FIPS 180-4 vectors (C++); a fixture blob (`compiler/tests/data/b1/blobs/modal-small.bin`, a few hundred bytes, committed as a normal file) whose hash, offsets and shapes are checked in both languages; a wrong hash fails with `blob`.
- `B1-R4 …` (C++): no exception escapes `loadInstrument` (catch nlohmann/json's internally); a 10 MB garbage input and a 10 000-deep nested array return `InvalidArgument` with a `parse` or `structure` error and do not crash; the configure-time engine-isolation check still passes with `pme_loader` present (it is not an engine target).
- `B1-R6 …` (Python): regenerating generic-grand-88 and test-guitar-6 reproduces the committed files; the generic-grand-88 tension bounds hold.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to B1:

- Module note: docs/dev/instrument-format.md — SPEC-INST and SPEC-API sections implemented; the format rules, canonical form, error codes, every constraint with its source, version history (v0, v1) and migrations, the manifest and blob layout, how to add a field (schema, both loaders, both migrations, `all-fields.json`), and the nominal status of the example values.
- No audio-thread code: the loader is never reachable from `Engine::process`; no RT-safety test is required. Coverage: `engine/src/runtime/loader` counts towards the 90% engine/src line-coverage gate; your C++ tests must cover it.
- Performance: no baseline. Report the load time of generic-grand-88 in C++ (Release) and Python in `notes`.
- If a member cannot be represented without changing a header (for example a public loader entry point the plug-in will need), do not change the header: describe the proposed interface change in `icrs` in your report.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `B1: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` with GCC and with Clang; `uv run ruff check`, `uv run ruff format --check`, `uv run mypy`, `uv run pytest compiler/tests tools/tests/test_p0_3_r1.py`.
- If something cannot be done as written (a struct member whose unit or constraint SPEC leaves undefined, a SPEC contradiction), do not guess silently: choose the least restrictive reading, mark it in docs/dev/instrument-format.md and in `spec_issues` in your report.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, the list of fixtures with their (path, code) results, the generic-grand-88 tension range and B per region, load times.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B1`, `role` `implementer`, `model` `sonnet`; `requirements` B1-R1 to B1-R6; `tests` B1-T1 to B1-T3; every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0010 plus any you raise; `icrs` any proposed interface change; `open_risks` as found.
