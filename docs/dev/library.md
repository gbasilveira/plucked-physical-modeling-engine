# Instrument and preset library (D5)

SPEC sections used: SPEC-INST "Instrument definitions" and above all the paragraph "Instrument file"
(physical parameters live in a versioned, human-readable description; body modal data is stored apart
as binary blobs referenced by hash; sample-rate-dependent data such as oscillator coefficients is never
stored but compiled on load; the instrument file does not carry the string f\_max); SPEC-API "Engine
interfaces" ("Descriptions", "Parameter classes", "Compile functions"); SPEC-BODY "Local bridge model"
(the inputs of `compileBody` and `buildLocalBridge`, which the cache keys mirror).

`pme_plugin_library` (alias `pme::plugin_library`) is a non-real-time C++20 static library in
`plugin/common/library/`, namespace `pme::plugin::library`. It decides where instruments, presets and
compiled body data live on each system and how they are imported, exported, migrated, checked and
cached. **Nothing in it may be called from `Engine::process`, from `run`, or from any audio-thread
callback.** It allocates, uses `std::filesystem` and nlohmann/json (ADR-0005), and never throws across
its API (every public function catches internally and reports `io` or a more specific code). It is not
wired into the CLAP or LV2 plug-ins yet; D6 connects it (see "What D6 must do"). D4 (GUI) will present
it. It changes no frozen header (interfaces-v2).

| Header (`plugin/common/library/include/pme/plugin/library/`) | Content |
| --- | --- |
| `error.hpp` | `LibraryError {path, code, message}`, `Report {errors, diagnostics}`, the code names |
| `files.hpp` | UTF-8 paths, atomic writes, hashing helpers |
| `locations.hpp` | `Locations`, `defaultLocations`, library ids, `resolveInstrument`, `resolvePreset` (D5-R1) |
| `preset.hpp` | `Preset`, parse and serialise, migration, import, export, listing, `PluginState` conversion (D5-R2, D5-R3) |
| `instruments.hpp` | import, export, listing and loading of instruments (D5-R3, D5-R5) |
| `cache.hpp` | cache keys, container, `getOrCompute`, `pruneCache` (D5-R4, D5-R5) |

Every function takes its directories as an argument (`Locations`); there is no global state, so tests
use temporary directories and never touch the real user folders. Functions return `pme::Status`
(`Ok` or `InvalidArgument`; the out-parameter is unchanged on failure) and fill a `Report`.

## Install locations (D5-R1)

`<data dir>` is `kDataDirName` in `plugin/common/include/pme/plugin/ids.hpp` (provisional, value `pme`;
decision D-3).

| System | factory (bundled, read-only) | user | cache |
| --- | --- | --- | --- |
| Linux | first existing `<dir>/<data dir>` in `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`); the first entry if none exists yet | `$XDG_DATA_HOME/<data dir>` (default `~/.local/share/<data dir>`) | `$XDG_CACHE_HOME/<data dir>` (default `~/.cache/<data dir>`) |
| macOS | `/Library/Application Support/<data dir>` | `~/Library/Application Support/<data dir>` | `~/Library/Caches/<data dir>` |
| Windows | `FOLDERID_ProgramData\<data dir>` | `FOLDERID_RoamingAppData\<data dir>` | `FOLDERID_LocalAppData\<data dir>\Cache` |

- Windows uses `SHGetKnownFolderPath` (`shell32` and `ole32` are linked only on Windows). Linux and macOS
  read the environment. `HOME` is required only where a rule uses it; an unset, empty or relative `HOME`
  is an error `io`, never a guess. On Linux an `XDG_*` value that is not an absolute path is ignored (XDG
  Base Directory specification).
- `locationsFromEnvironment(Platform, EnvLookup, ...)` evaluates the Linux and macOS rules from any
  lookup function on any host (this is how the tests check both tables on every system).
  `defaultLocations()` picks the table of the build system.
- Under `factory` and `user`: `instruments/<name>/instrument.json` (plus its blobs) and
  `presets/<name><kPresetExtension>`. Under `cache`: `body/<key hex>.bin`.
- H2 installs files into `factory`; D5 only defines and reads it. The library never writes there.

### Library ids

An instrument or preset is addressed by `factory/<name>` or `user/<name>` (the directory name under
`instruments/`, or the file stem under `presets/`). `<name>` is 1 to 128 characters of
`[A-Za-z0-9._-]` not starting with `.`; anything else (`..`, separators, colons, an absolute path, an
empty name) is the error `invalid-id`, so an id never leaves its root. The prefix makes every id unique,
so a user entry never shadows a factory entry. `resolveInstrument` returns the `instrument.json` path or
`not-found`.

## Presets (D5-R2)

One UTF-8 JSON file per preset, extension `kPresetExtension` (provisional, `.pmepreset`). Example,
canonical current version:

```json
{"format":"pme-preset","instrument":{"hash":"sha256:<64 hex>","id":"factory/generic-grand-88"},"name":"Bright grand","params":{"2001":-6.5,"4101":0.4},"presetSchemaVersion":1}
```

**Canonical form** (used by every writer; D1's state uses the same style): compact JSON (no whitespace),
object keys sorted by byte order, shortest round-trip doubles (nlohmann/json `dump`), LF, one final
newline. A load followed by a save is byte-identical.

- `format`: `kPresetFormat` (provisional, `pme-preset`). A different value is the error `enum`.
- `instrument.id`: a library id. `instrument.hash` (optional): `sha256:` and 64 lower-case hex digits of
  the instrument's canonical text (the `writeInstrument` output, `instrumentHash()`) at save time. When the
  instrument has changed since, loading adds the diagnostic `instrument-changed` and still succeeds.
- `name`: display name (default: the file stem).
- `params`: overrides keyed by the registry id as a decimal string (ids are the stable contract; ICR-0001
  retired id 1010), only values that differ from the registry default, continuous and structural alike.
  On load, an unknown or retired id is ignored with the diagnostic `unknown-param` (path
  `/params/<key>`); a value outside the registry range is clamped with `clamped`; a non-finite or
  non-number value is an error (`range`, `type`); a repeated id (for example `"1"` and `"01"`) is
  `duplicate`; a value equal to the default is dropped silently, so the in-memory form is normalised.
- Unknown members are `unknown_key` errors, as in B1. Error codes are B1's, with JSON Pointer paths.

### Versions and migration

`presetSchemaVersion` is an integer. The current version is **1**. Writers always write the current
version. A version above the current one, below 0 or not an integer is the error `version` and nothing is
loaded.

| Version | Shape | Status |
| --- | --- | --- |
| 0 | `{"format","presetSchemaVersion":0,"instrument":"<library id>","params":{"<dotted registry name>": value},"name"?}` | Never shipped; exists so that the migration path is exercised from the first release, as B1 did |
| 1 | as in the example above | Current |

Migration 0 to 1 (applied before validation, so a migrated file is validated as version 1):

| Version 0 | Version 1 |
| --- | --- |
| `instrument`: bare string | `instrument: {"id": <string>}` (no `hash`; the changed-instrument check is skipped) |
| `params` keyed by dotted registry name (`output.gain`) | keyed by the registry id (`"2001"`); an unknown name is dropped with `unknown-param` at `/params/<name>` |
| no `name` | `name` = the file stem |

### Conversion to the plug-in state

`presetToState(preset)` gives a D1 `StateData` with `instrument` = the library id and `params` = the
overrides; `presetFromState(state, name, hash)` is the reverse. D1's state format and `state.hpp` are
unchanged.

## Instruments (D5-R3, D5-R5)

- **Import** (`importInstrument`). The file is loaded with B1's `loadInstrumentFile(path,
  /*verifyBlobs=*/true, ...)`, so every error is B1's (JSON Pointer path, B1 code). On success the
  instrument is stored in `user/instruments/<name>/instrument.json` as canonical current-version text
  (`writeInstrument`): this is the migration through B1, a version-0 instrument is stored as version 1
  (`ImportResult::sourceVersion` reports 0). Every manifest blob is copied to the same relative path and
  its SHA-256 is verified again after the copy. `<name>` is `reduceName(desc.name)`: `[A-Z]` lower-cased,
  every other character outside `[a-z0-9]` (including every non-ASCII byte) becomes `-`, runs collapse,
  leading and trailing `-` are removed, at most 64 characters, `instrument` if nothing is left. If the
  directory exists and holds a different instrument it gets the suffix `-2`, `-3`, ... (up to 1000); if it
  holds the same canonical text the import is idempotent and reuses it (`alreadyPresent`). The directory
  is built under `user/instruments/.import-<token>` and renamed into place, so a failed import leaves no
  partial directory (and no staging directory).
- **Export** (`exportInstrument`) loads the library instrument with blob verification and writes
  `targetDir/<name>/instrument.json` (canonical) and the blobs (each copy hash-verified); the
  instrument file is written last. Importing the result again gives the same canonical text and the same
  `instrumentHash`. A single-file archive is out of scope (no archive dependency).
- **Load** (`loadLibraryInstrument`) always uses `verifyBlobs = true`: a missing blob file or a SHA-256
  mismatch is the error `blob`, with B1's path (`/blobs/<i>/path` or `/blobs/<i>/hash`, or the array
  offset for a truncated file) and a message that names the blob file; nothing is returned. The hash code
  is B1's (`sha256.cpp`); the library has no second implementation.
- **Listing** (`listInstruments`, `listPresets`): id, name, source, schema version found on disk
  (instruments are loaded without blob verification here) and the errors of that entry; one bad entry
  never fails the listing. Hidden entries (`.name`) are skipped.
- **Presets** (`importPreset`, `exportPreset`, `loadLibraryPreset`): import reads any supported version,
  migrates, validates and writes the current version to `user/presets/<name><ext>` (`reduceName` of the
  preset name; same suffix and reuse rules, created exclusively so two processes cannot both claim a
  name). A preset whose instrument id does not resolve still imports (diagnostic `not-found`);
  `loadLibraryPreset` then fails with `not-found`.

## Cache of compiled body data (D5-R4)

The cache stores sample-rate-independent data only: `pme::BodyModes` (the result of `compileBody`) and
`pme::LocalBridge` (the result of `buildLocalBridge`). It never stores oscillator coefficients or anything
that depends on fs (SPEC-INST "Instrument file"). It does not call `compileBody` or `buildLocalBridge`
(P0.4 stubs until B2, B3 and C2): `getOrCompute` takes the producer as a callable returning `Status`.

### Keys

A key is SHA-256 over an explicit byte encoding, field by field (no JSON dump). All integers are
little-endian; `f64` is the IEEE-754 binary64 bit pattern as `u64`; `str` is `u64` length plus the bytes;
a list is `u64` count plus the elements.

```text
body key  = SHA-256(
  str "pme-cache-key-body", u32 kCacheFormatVersion, i32 interfacesVersionMajor, str producerVersion,
  BodyParams: u8 source,
    PlateParams: f64 x18 in declaration order (lengthX, lengthY, thickness, grainAngle, youngsL,
      youngsR, shearLR, poissonLR, poissonRL, density, moisture, dEdMcL, dEdMcR, dGdMc, dRhodMc,
      lossFactor, rimStiffness, fMax),
      list ribs { Point start, Point end, f64 width, height, youngs, density },
      list Point outline, i32 thicknessNx, i32 thicknessNy, list f64 thicknessGrid,
      list bridges { list Point path, f64 width, height, youngs, density }, f64 rimRotationalStiffness,
    str modalTableHash,
  list Point bridge-point positions (BridgePointDesc::position, in order),
  list Point channel positions (ChannelDesc::position, in order))
local key = SHA-256(
  str "pme-cache-key-local-bridge", u32 kCacheFormatVersion, i32 interfacesVersionMajor,
  32 bytes body key, i32 point, i32 maxModes, f64 fSplit)
Point     = f64 x, f64 y, f64 z
```

The body key covers exactly the arguments of `compileBody` (`BodyParams` including `modalTableHash`, the
bridge-point positions, the channel positions); the local-bridge key covers exactly those of
`buildLocalBridge` (the body key stands for the `BodyModes` argument). The effective `maxModes` and
`fSplit` come from the instrument's `Defaults` (`localBridgeModes`, `localBridgeFSplit`), with `maxModes`
overridden by the structural parameter `body.localBridgeModes` (7001, clamped to its registry range) when
the preset sets it. `producerVersion` is a string the caller passes (D6 passes the engine version, so an
engine update invalidates the cache); `kCacheFormatVersion` (1) invalidates everything when the encoding
or container changes. `bodyCacheKeys(desc, overrides, producerVersion)` returns the body key and one
local-bridge key per bridge point.

In the encoding code each struct is bound with a structured binding that names every member
(`const auto& [lengthX, ...] = plate`), so adding a member to `BodyParams`, `PlateParams`, `RibDesc`,
`BridgeBeamDesc` or `Point` is a compile error in `src/cache_keys.cpp` until the new member is encoded and
`kCacheFormatVersion` is bumped (a `sizeof` guard would differ between standard libraries). The test
`D5-R4 the key encoding is stable` pins two golden keys that were computed independently with this layout.

### Dependency table (D5-T3)

| Edit | Body recomputed | Local bridges recomputed |
| --- | --- | --- |
| none (second load) | 0 | 0 |
| sample rate (the engine configuration is not an input) | 0 | 0 |
| a string's tension, a hammer mass, the tuning, `strings.fMax`, any continuous parameter | 0 | 0 |
| `body.localBridgeModes` (7001) override, `Defaults::localBridgeModes`, `Defaults::localBridgeFSplit` | 0 | all |
| plate thickness or any `PlateParams` member, `modalTableHash` | 1 | all |
| one bridge point's position, a channel position | 1 | all |
| a different producer version string | 1 | all |

### Container

`cache/body/<key hex>.bin`, little-endian:

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 8 | magic `PMECACHE` |
| 8 | 4 | container version (= `kCacheFormatVersion`) |
| 12 | 4 | kind: 1 body, 2 local bridge |
| 16 | 32 | the key |
| 48 | 8 | payload length |
| 56 | 32 | SHA-256 of the payload |
| 88 | n | payload |

Payload of `BodyModes`: `i32 numModes, numPoints, numChannels`, then seven lists of `f64`: `omega`,
`sigma`, `phi`, `alpha`, `beta`, `channelDelay`, `residualMobility`. Payload of `LocalBridge`: `i32 point`,
list `i32 modeIndex`, lists `f64 omega, sigma, phi`, `f64 conductanceHf`, `f64 fSplit`.

Writes are atomic (temporary file in `cache/body/`, then rename), so concurrent writers of one key leave
one valid entry and readers never see a partial file. A failure to store only adds a diagnostic `cache`;
the computed value is still returned. A hit sets the file's modification time to now.

### Pruning

`pruneCache(locations, maxBytes)` deletes the entries of `cache/body/*.bin` with the oldest modification
time first until their total size is at most `maxBytes` (default `kDefaultCacheMaxBytes` = 1 GiB), and
removes temporary files older than one hour. It never deletes anything outside `cache/body/`, and never
a file that does not end in `.bin`.

## Integrity (D5-R5)

- Library instruments: see "Load" above; `verifyBlobs` is always true.
- Cache entries are verified on every read: magic, container version, kind, stored key equal to the
  requested key, payload length equal to the file size minus the header, payload SHA-256, and that the
  payload deserialises with consistent lengths (`omega.size() == numModes`, `phi.size() == numModes *
  numPoints`, `alpha.size() == beta.size() == numChannels * numModes`, `channelDelay` empty or
  `numChannels`, `residualMobility` empty or `numPoints`; for a local bridge `omega`, `sigma`, `phi` the
  size of `modeIndex`; no trailing bytes). A bad entry is reported as a diagnostic `integrity` whose path
  is the entry file, deleted, and recomputed through the producer; it is never returned.

## Error codes

Closed set (`pme::plugin::library::code`). Errors fail the call; diagnostics do not.

| Code | Kind | Meaning |
| --- | --- | --- |
| `parse`, `type`, `missing`, `unknown_key`, `range`, `enum`, `version`, `migration`, `reference`, `duplicate`, `structure`, `blob` | error | B1's codes (docs/dev/instrument-format.md), passed through with their JSON Pointer; presets reuse `parse`, `type`, `missing`, `unknown_key`, `range`, `enum`, `version`, `duplicate` |
| `io` | error | A file-system failure; `path` is the file |
| `not-found` | error, or diagnostic on preset import | An id, file or directory is missing |
| `invalid-id` | error | A malformed library id |
| `integrity` | diagnostic (cache), error never | A cache entry failed a check and was discarded |
| `cache` | diagnostic | A cache entry could not be stored or read |
| `instrument-changed` | diagnostic | The preset's instrument hash differs from the instrument's |
| `unknown-param` | diagnostic | An unknown or retired parameter id or name was ignored |
| `clamped` | diagnostic | A value was clamped to the registry range |

## Concurrency and atomic writes

All functions are re-entrant; two plug-in instances (threads or processes) may use the same directories.
Every file write goes to a temporary file in the same directory, then `std::filesystem::rename`. On
POSIX the rename replaces the target atomically. On Windows the standard library replaces an existing
file (MoveFileEx replace semantics); a reader that holds the target open can make the rename fail with a
sharing violation, so it is retried up to five times 10 ms apart. A new preset is created exclusively
(hard link of the temporary file; a file system without hard links falls back to check and rename, with a
small race window). Directories (imported instruments) are renamed into place from a staging directory
next to their destination. Paths are `std::filesystem::path` built from UTF-8 (`pathFromUtf8`), so
non-ASCII user names and instrument names work on Windows.

## Provisional identifiers (D-3)

Decision D-3 (product name and identifiers) is open. These live only in `ids.hpp` (block `Library (D5)`):
`kDataDirName` (`pme`), `kPresetExtension` (`.pmepreset`), `kPresetFormat` (`pme-preset`). D2 replaces
them when D-3 is answered; the `.pmepreset` extension and format string are then part of the preset
contract, so change them before any preset is shipped.

The tests build these names from `kDataDirName`, `kPresetExtension` and `kPresetFormat`, so D2 needs no
test edits. One manual step remains: the fixture `plugin/common/library/tests/data/preset-v0.pmepreset`
is a frozen sample of an old file. Its file name and its `"format"` value are literals; when D-3
changes the extension or the format string, rename the fixture and edit its `format` member by hand
(`test_presets.cpp` finds it as `preset-v0` plus `kPresetExtension`).

## What D6 must do to connect it

1. Build `Locations` once at start-up with `defaultLocations()` (main thread, never in `process`).
2. On `setInstrumentReference(id)`: `loadLibraryInstrument(locations, id, ...)` (blobs verified), then
   `bodyCacheKeys(file.desc, overrides, engineVersionString, keys, report)`, then `getOrCompute` for the
   body and each local bridge with producers that call `compileBody` and `buildLocalBridge`, then
   `compileInstrument` and the swap. Run all of it on the controller's task runner thread.
3. Pass the preset's parameter overrides as `overrides` (they decide the effective `maxModes`).
4. Surface `Report::errors` and `diagnostics` to the host-visible log or to D4.
5. Call `pruneCache` occasionally (for example once per session at start-up) with
   `kDefaultCacheMaxBytes`.

## Not implemented here

Archive (single-file) import and export; writing into `factory`; a preset browser (D4); signing of
factory data (H2). The tests are `D5-R1 ...` to `D5-R5 ...` and `D5-T1` to `D5-T4` in
`plugin/common/library/tests/`, run by the `plugin` CI jobs (`ctest -R "^D[15]-"`).
