# Brief: D5 Instrument and preset management — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/D5-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

where instruments, presets and compiled body data live on each system, and how they are imported, migrated and cached.

D5 delivers a non-real-time C++ library, `pme_plugin_library` in `plugin/common/library/`, and its tests. It is **not** wired into the CLAP or LV2 plug-ins yet: D6 connects it (instrument loading through the controller's `setInstrumentReference`, compile, swap). D4 (GUI) later presents it. Nothing in D5 runs on the audio thread.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Layout, Languages, Code style, Testing policy, CI, Documentation, Licensing); the Track D introduction and the phase card **D5 Instrument and preset management**; for context **B1**, **B2**, **D1**, **D4**, **D6**, **H2** (H2-R4 bundles factory instruments and verifies blob checksums at install) and **H3**.
- SPEC and PLAN sections:
  - SPEC-INST — docs/plan/engine-specification.md, "Instrument definitions", above all the paragraph **"Instrument file"**: "Physical parameters live in a versioned, human-readable description (geometry, materials, tuning, exciter and damper parameters, body references). Body modal data is stored separately as binary blobs referenced by hash. Sample-rate-dependent data (oscillator coefficients) is never stored; it is compiled on load. Instrument files do not carry the string f\_max: it comes from the engine configuration".
- Also: SPEC-API "Engine interfaces" ("Descriptions", "Parameter classes", "Compile functions"); SPEC-BODY "Local bridge model" (what `compileBody` and `buildLocalBridge` take as input, so you can key the cache on exactly those inputs).
- **docs/dev/instrument-format.md** in full (B1): canonical form, versions and migrations, blob manifest, error codes. The C++ loader API in `engine/src/runtime/loader/include/pme/loader/instrument_file.hpp` and `sha256.hpp` (target `pme_loader`).
- **docs/dev/clap-shell.md**: "Controller layer (for D3)", "Provisional identifiers (D-3)", "State (D1-R4)", "Threads". `plugin/common/include/pme/plugin/state.hpp`, `ids.hpp`, `param_model.hpp`.
- The frozen headers `engine/include/pme/instrument.hpp`, `body.hpp`, `params.hpp`, `status.hpp`, `version.hpp`.
- docs/adr/ADR-0003 (dependency pinning), ADR-0005 (nlohmann/json, non-real-time code only).
- docs/dev/build.md, docs/dev/ci.md, docs/dev/testing.md.
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs (docs/status/phase-status.md):
  - **D1** complete: `plugin/common/` (controller layer, target `pme_plugin_common`, namespace `pme::plugin`), D1 state JSON (`serializeState`/`parseState`), `ids.hpp`, the `plugin (linux|macos|windows)` jobs in pr.yml (pr.yml run 38153781819 at 2570bbc).
  - **B1** complete: JSON Schema and Python models in `compiler/pme_compiler/schema/`; C++ loader `pme_loader` (`loadInstrument`, `loadInstrumentFile(path, verifyBlobs, …)`, `writeInstrument`, `LoadError {path, code, message}`, `InstrumentFile {desc, blobs, sourceVersion}`), SHA-256 in `engine/src/runtime/loader/sha256.cpp`; example instruments in `instruments/examples/` (test-monochord, test-unison-pair, generic-grand-88, test-guitar-6); fixtures in `compiler/tests/data/b1/` including a version-0 instrument. pr.yml run 38176885514 at 8648745 green on all 19 jobs. No pull requests (single session branch `ccr-5ef35d73-oqb6fd`).
- Frozen interfaces: **interfaces-v2** (300b995; ICR-0001). If `interfacesVersionMajor` in engine/include/pme/version.hpp is not 2, stop and report. D5 changes no frozen header.
- Decision **D-3** (product name and identifiers) is **open**. Any directory name or file extension that carries the product name is provisional and lives in `plugin/common/include/pme/plugin/ids.hpp` only (D1 rule).
- `compileBody` and `buildLocalBridge` are P0.4 stubs (`NotImplemented`) until B2/B3/C2 provide them. D5 does **not** call them: the cache takes the producing function as a callback, and tests inject a counting fake.

## Concurrent work: D5 runs at the same time as A4, A6, B2 and B4 (and their test-authors), and a P0.5 follow-up

Each phase works in its own git worktree; the orchestrator merges the commits into `ccr-5ef35d73-oqb6fd`. The other phases own `engine/src/bridge/**`, `engine/tests/bridge/**`, docs/dev/bridge.md (A4); `engine/src/dampers/**`, `engine/tests/dampers/**`, docs/dev/dampers.md (A6); `compiler/pme_compiler/body/**`, `compiler/pme_compiler/materials/**`, docs/dev/body-compiler.md (B2); `compiler/pme_compiler/importers/**` (B4); `reference/**` (P0.5 follow-up). None of them touches `plugin/**`. **Everything not listed under "May edit" is read only for you**, in particular `engine/**` (including `engine/src/runtime/loader/`), `compiler/**`, `instruments/**`, `plugin/clap/**`, `plugin/lv2/**`, `pyproject.toml`, `uv.lock`, `cmake/dependencies.cmake`.

Shared files: edit only inside the delimited block or the single line given here, so the orchestrator can merge without conflict.

| File | Your change |
| --- | --- |
| `plugin/common/CMakeLists.txt` | append one line at the end: `add_subdirectory(library)  # D5` |
| `plugin/common/include/pme/plugin/ids.hpp` | append a block `// ---- Library (D5) ----` after the LV2 block, before the closing namespace; change nothing above it |
| `.github/workflows/pr.yml` | one change inside D1's `plugin` job: the ctest filter `-R "^D1-"` becomes `-R "^D[15]-"` (the step name may add "D5"). Nothing else in pr.yml. No new job. |
| `docs/dev/ci.md` | one sentence appended to the D1 `plugin` job subsection saying the job also runs the D5 tests |

## Scope

- May edit (create or change):
  - `plugin/common/library/**` — the card's Output: sources, headers (`plugin/common/library/include/pme/plugin/library/*.hpp`), `CMakeLists.txt`, tests in `plugin/common/library/tests/`, test fixtures in `plugin/common/library/tests/data/`.
  - `docs/dev/library.md` and `docs/user/files.md` — the card's Outputs.
  - The shared-file changes in the table above.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D5-R1 Install locations for bundled instruments and user data on each system.
2. D5-R2 Preset format: instrument reference plus parameter overrides, with schema version.
3. D5-R3 Import and export of instruments and presets; migration through B1.
4. D5-R4 Hash-keyed cache of compiled body data in the user cache directory.
5. D5-R5 Blob integrity checks by hash on load.

### Architecture

- **Target** `pme_plugin_library` (STATIC, alias `pme::plugin_library`), C++20, `pme_build_options`, warnings as errors, position-independent code (it will be linked into plug-in modules by D6). Links `pme::engine` (headers and the registry), `pme_loader` and `nlohmann_json::nlohmann_json` (ADR-0005: non-real-time). No new third-party dependency: if you believe one is needed (for example an archive library), stop and report; the planner writes an ADR.
- Namespace `pme::plugin::library`. Every function is non-audio, may allocate, never throws across the public API (catch `std::filesystem` and JSON exceptions internally), and returns `pme::Status` plus a `std::vector<LibraryError>` where it can fail. `LibraryError {std::string path; std::string code; std::string message;}` mirrors B1's `LoadError`; B1 errors pass through unchanged with their JSON Pointer, and file-system errors use the file path. Error codes are a closed, documented set (at least: B1's codes, `io`, `not-found`, `version`, `integrity`, `cache`).
- **Thread safety**: functions are re-entrant; two plug-in instances (two processes or two threads) may use the same directories at once. Every file write goes to a temporary file in the same directory, then an atomic rename (`std::filesystem::rename`; on Windows, replace semantics — document how). Readers never see a partial file.

### How to meet each requirement

**D5-R1 (install locations).** A `Locations` struct with four directories: `factory` (bundled instruments and presets, read-only), `user` (user instruments and presets), `cache` (compiled body data), and the per-kind subdirectories `instruments/` and `presets/` under `factory` and `user`. `defaultLocations()` resolves them per system:

| System | factory | user | cache |
| --- | --- | --- | --- |
| Linux | first existing `<dir>/<data dir>` in `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`) | `$XDG_DATA_HOME/<data dir>` (default `~/.local/share/<data dir>`) | `$XDG_CACHE_HOME/<data dir>` (default `~/.cache/<data dir>`) |
| macOS | `/Library/Application Support/<data dir>` | `~/Library/Application Support/<data dir>` | `~/Library/Caches/<data dir>` |
| Windows | `FOLDERID_ProgramData\<data dir>` | `FOLDERID_RoamingAppData\<data dir>` | `FOLDERID_LocalAppData\<data dir>\Cache` |

`<data dir>` is a provisional string in `ids.hpp` (`kDataDirName`, value `pme`). Use `SHGetKnownFolderPath` on Windows (link `shell32`, `ole32` only there) and the environment on the others; `HOME` missing or relative → error, never a guess. A `Locations` can also be constructed from explicit paths; every other function takes a `Locations` argument (no global state), so tests use temporary directories and never touch the real user folders. Instruments are found by **library id** `factory/<name>` or `user/<name>` (the directory name under `instruments/`); `resolveInstrument(locations, id)` returns the instrument.json path. A user entry never shadows a factory entry (the prefix makes the id unique). H2 installs files into `factory`; D5 only defines and reads it.

**D5-R2 (preset format).** One UTF-8 JSON file per preset, extension from `ids.hpp` (`kPresetExtension`, provisional, value `.pmepreset`), canonical form like D1 state (compact or 2-space indented — choose one and document it; keys sorted, shortest round-trip doubles, LF, final newline):

```json
{
  "format": "pme-preset",
  "instrument": {"id": "factory/generic-grand-88", "hash": "sha256:<64 hex>"},
  "name": "Bright grand",
  "params": {"2001": -6.5, "4101": 0.4},
  "presetSchemaVersion": 1
}
```

- `instrument.id` is a library id (D5-R1); `instrument.hash` is the SHA-256 of the instrument's canonical text (`writeInstrument` output) at save time, used to warn — not fail — when the instrument has changed since (diagnostic code `instrument-changed`).
- `params` holds parameter overrides keyed by the registry **id** as a decimal string (ids are the stable contract; ICR-0001 retired id 1010), only values that differ from the default, continuous and structural alike. On load: unknown or retired ids are ignored with a diagnostic; values are clamped to the registry range with a diagnostic; non-finite values are an error.
- The `format` value is provisional (D-3) and lives in `ids.hpp` (`kPresetFormat`).
- `presetSchemaVersion` current = **1**. A newer version is an error `version` (nothing loaded). An older version migrates on load; writers always write the current version.
- **Version 0** (never shipped; exists so that the migration path is exercised from the first release, as B1 did): `{"format":"pme-preset","presetSchemaVersion":0,"instrument":"<library id>","params":{"<dotted registry name>": value}}` — the instrument is a bare string without hash and parameters are keyed by dotted name. Migration 0 → 1: `instrument` → `{"id": <string>}` (hash absent; the loader then skips the changed-instrument check), each dotted name → its registry id (an unknown name is dropped with a diagnostic), `name` defaults to the file stem. Document the version table in docs/dev/library.md.
- Conversion helpers to and from D1's `PluginState` (`state.hpp`): a preset applies to a state as `instrument` = library id and `params` = overrides; a state saves as a preset. Do not change D1's state format or `state.hpp`.

**D5-R3 (import, export, migration through B1).**
- **Import instrument** `importInstrument(locations, sourceInstrumentJson, …) → library id`: load with B1's `loadInstrumentFile(path, /*verifyBlobs=*/true, …)` (so every error is B1's, with its JSON path); on success write the instrument into `user/instruments/<name>/` as **canonical current-version text** via `writeInstrument` (this is the migration through B1: a version-0 instrument is stored as version 1), and copy each manifest blob to the same relative path, verifying its hash again after the copy. `<name>` is the instrument's `name` reduced to `[a-z0-9-]` (other characters → `-`, collapsed, at most 64 characters); an existing directory with a different hash gets a `-2`, `-3` … suffix; one with the same hash is reused (import is idempotent). Write into a temporary directory and rename it into place.
- **Export instrument** `exportInstrument(locations, id, targetDir)`: writes `targetDir/<name>/instrument.json` (canonical) plus its blobs; the result re-imports to the same hash. A single-file archive is out of scope (no archive dependency); say so in docs/user/files.md.
- **Import/export preset**: import reads any supported version, migrates, validates against the registry and writes the current version into `user/presets/<name><ext>`; export writes the current version to a chosen path. A preset whose instrument id does not resolve still imports (diagnostic `not-found`); loading it later fails with `not-found`.
- `listInstruments(locations)` and `listPresets(locations)` return id, name, source (factory or user), schema version found on disk and any error, without failing the whole listing on one bad entry.

**D5-R4 (hash-keyed cache of compiled body data).** The cache stores sample-rate-independent body data only — `pme::BodyModes` (from `compileBody`) and `pme::LocalBridge` (from `buildLocalBridge`); never oscillator coefficients or anything that depends on fs (SPEC-INST "Instrument file").
- **Keys**: SHA-256 over a canonical byte encoding (documented in docs/dev/library.md) of exactly the inputs of the producing function, plus a cache format version, `interfacesVersionMajor` and a producer version string passed by the caller (D6 will pass the engine version, so an engine update invalidates the cache):
  - body entry: `BodyParams` (including `modalTableHash`, so a changed blob changes the key), the bridge-point positions (`BridgePointDesc::position`, in order) and the channel positions — the arguments of `compileBody`;
  - local-bridge entry: the body key, the point index, `maxModes` and `fSplit` — the arguments of `buildLocalBridge`; the effective `maxModes` and `fSplit` are the instrument's `Defaults` overridden by the registry's structural `body.localBridgeModes` (7001) when the preset sets it.
  Nothing else enters a key: strings, exciters, dampers, tuning, `strings.fMax`, the sample rate and the continuous parameters do not. The encoding is written explicitly field by field (not by hashing a JSON dump), so a new struct member is a compile error in a `static_assert(sizeof …)` guard or a documented manual step; doubles are encoded as their IEEE-754 bit patterns, little-endian; strings as length plus bytes.
- **Store**: `cache/body/<key hex>.bin`, a small binary container: magic, container version, kind (body or local bridge), the key, payload length, payload SHA-256, payload (the struct serialised field by field, little-endian, with lengths). Writes are atomic (temp + rename).
- **API**: `getOrCompute(locations, key, kind, producer)` where `producer` is a callable returning `Status` and the struct; on a miss or an invalid entry it calls the producer and stores the result; on `NotImplemented` or any non-`Ok` status it stores nothing and returns that status. A helper `bodyCacheKeys(const InstrumentDesc&, const ParamOverrides&, producerVersion)` returns the body key and one local-bridge key per bridge point, for D6 and for D5-T3.
- **Size bound**: `pruneCache(locations, maxBytes)` deletes least-recently-used entries (by modification time; a hit touches the file) until the total is at most `maxBytes`; default 1 GiB, documented. Never deletes outside `cache/body/`.

**D5-R5 (blob integrity by hash on load).**
- Instrument load from the library (`loadLibraryInstrument(locations, id, …)`) always calls B1's `loadInstrumentFile` with `verifyBlobs = true`: a missing blob or a SHA-256 mismatch is an error `blob` naming the manifest path (`/blobs/<i>`) and the blob file; nothing is returned. B1 already computes the hash — reuse it, do not write a second SHA-256.
- Cache entries are verified on every read (magic, versions, key equal to the requested key, payload length, payload SHA-256, and that the payload deserialises with consistent lengths, for example `phi.size() == numModes * numPoints`). A bad entry is reported (diagnostic code `integrity`, with the file path), deleted, and recomputed through the producer; it is never returned.

## Tests that must pass

- **D5-T1** presets round-trip;
- **D5-T2** an old preset migrates;
- **D5-T3** a structural edit invalidates exactly the affected cache entries;
- **D5-T4** a corrupted blob is detected and reported.

### How each test is built

doctest, in `plugin/common/library/tests/`, registered with the project's `pme_add_doctest` (label `unit`). Test names start with the ID (`D5-T1 presets round-trip`, …); requirement-level unit tests are named `D5-R1 …` to `D5-R5 …`. Every test uses fresh temporary directories through an explicit `Locations`; none reads or writes the real user folders or the network. Fixtures: copy `instruments/examples/test-unison-pair` and the B1 version-0 fixture from `compiler/tests/data/b1/` at test time (do not duplicate them); a fixture instrument with one small blob (a `ModalTable` body) is generated by the test itself or checked in under `plugin/common/library/tests/data/` with its manifest.

- **D5-T1**: (a) a preset with overrides of at least one continuous and one structural parameter saves, loads and saves again byte-identical; (b) export → import into a fresh `Locations` → export is byte-identical, for a preset and for an instrument with a blob (same canonical hash); (c) preset → `PluginState` → preset is identical; (d) unknown id ignored, out-of-range value clamped, each with its diagnostic.
- **D5-T2**: (a) a version-0 preset fixture (dotted names, bare instrument string) loads as version 1 with the expected ids and values, and saving it writes version 1; an unknown dotted name is dropped with a diagnostic; (b) a preset with `presetSchemaVersion` above the current one fails with `version` and loads nothing; (c) importing B1's version-0 instrument fixture stores a version-1 canonical instrument whose `sourceVersion` was 0 (migration through B1).
- **D5-T3**: build an instrument with at least two bridge points; fill the cache through a counting fake producer (one body entry, one local-bridge entry per point). Then, one edit at a time from the same start, re-run and check the producer call counts and which entry files changed:

  | Edit | Body recomputed | Local bridges recomputed |
  | --- | --- | --- |
  | none (second load) | 0 | 0 |
  | sample rate changed (48 → 96 kHz in the engine configuration passed alongside) | 0 | 0 |
  | a string's tension, a hammer mass, the tuning, `strings.fMax` | 0 | 0 |
  | `body.localBridgeModes` (7001) override, or `Defaults::localBridgeFSplit` | 0 | all |
  | plate thickness (or `modalTableHash` of a `ModalTable` body) | 1 | all |
  | one bridge point's position | 1 | all |

  The table is the dependency rule of D5-R4; each row is one doctest subcase. Also check the producer version string: a different one recomputes everything.
- **D5-T4**: (a) flip one byte in a library instrument's blob → `loadLibraryInstrument` fails with code `blob` whose path names the manifest entry, and nothing is returned; (b) delete the blob → same failure; (c) flip one byte in a cache entry's payload, truncate another, and change the stored key of a third → each is reported with `integrity` and the file path, deleted, and recomputed (producer called once each); the struct returned equals the original.
- **Unit tests** `D5-R1 …`: `defaultLocations` follows the table for the current system with environment variables overridden in the test process (Linux and macOS: set `HOME`, `XDG_*`; Windows: the known-folder call returns absolute paths); a missing `HOME` is an error; ids resolve and never escape their root (`../` and absolute ids rejected). `D5-R3 …`: import is idempotent; a name clash gets a suffix; a failed import leaves no partial directory. `D5-R4 …`: concurrent `getOrCompute` from 4 threads on the same key leaves one valid entry; `pruneCache` keeps the most recently used entries.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to D5:

- **Platforms.** The D5 tests run in the existing `plugin (linux)`, `plugin (macos)` and `plugin (windows)` jobs (`PME_BUILD_PLUGINS=ON`, `<system>-rt-safety` presets) through the widened ctest filter. Paths must work with Windows separators and non-ASCII user names (use `std::filesystem::path` and `u8path`/`char8_t` conversions; test one non-ASCII instrument name).
- **RT-safety:** D5 has no audio-thread code; nothing in `pme_plugin_library` may be called from `process`/`run` (state this in the header comments and docs/dev/library.md). No `rt` label.
- **Module note:** docs/dev/library.md — SPEC sections used (SPEC-INST "Instrument file"; SPEC-API "Descriptions", "Parameter classes", "Compile functions"); the location table; library ids; the preset format, version table and migration 0 → 1; import/export rules and the canonical form; the cache key encoding, the dependency table of D5-T3, the container format, pruning; integrity rules; error codes; concurrency and atomic writes; provisional identifiers (D-3); what D6 must do to connect it.
- **User doc:** docs/user/files.md — for players, in plain words: where instruments, presets and the cache live on each system, how to import, export and back up, that the cache can be deleted safely, and what an integrity error means. No internal API detail.
- **Lint:** clang-format and clang-tidy clean on `plugin/common/library/` (the `plugin` jobs run clang-tidy over `plugin/`).
- **Coverage gate and performance baselines:** nothing under engine/src; they do not apply. Say so in `notes`.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `D5: ` and ending with the attribution lines your session provides. Make the shared-file edits (pr.yml filter, docs/dev/ci.md sentence, `plugin/common/CMakeLists.txt` line, `ids.hpp` block) one separate commit so the orchestrator can merge it on its own. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths (other agents' uncommitted files may be present; never add them).
- Run before you finish: configure and build `linux-debug` and `linux-rt-safety` with `-DPME_BUILD_PLUGINS=ON` (GCC and Clang); `ctest -R "^D[15]-"` passes and no D1 test changed; `clang-format --dry-run --Werror` and clang-tidy on `plugin/common/library/`; `uv run pytest tools/tests/test_p0_2_r7.py tools/tests/test_p0_3_r1.py`; the default build (`PME_BUILD_PLUGINS=OFF`) still configures, builds and passes `ctest`.
- If B1's loader or interfaces-v2 prevents a requirement (for example a missing accessor), stop that item and report it; the planner writes an ICR or a B1 follow-up. Never edit `engine/**` or `compiler/**`.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, test output summary, and each shared-file edit.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D5`, `role` `implementer`, `model` `sonnet`; `requirements` D5-R1 to D5-R5; `tests` D5-T1 to D5-T4; every created or changed path in `outputs`; `pull_requests` empty; `adrs` ADR-0003, ADR-0005.
