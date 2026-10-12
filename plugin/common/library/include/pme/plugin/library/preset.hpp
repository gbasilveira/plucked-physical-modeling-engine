// SPDX-License-Identifier: MIT
#pragma once

/// @file preset.hpp
/// Preset format, migration, import and export (D5-R2, D5-R3). Non-audio; allocates; never
/// throws; re-entrant.
///
/// SPEC sections used: SPEC-API "Parameter classes" (a preset holds registry parameters by id,
/// continuous and structural alike) and SPEC-INST "Instrument file" (the instrument is referenced,
/// never embedded). Format, versions and canonical form: docs/dev/library.md.

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pme/loader/instrument_file.hpp"
#include "pme/params.hpp"
#include "pme/plugin/library/error.hpp"
#include "pme/plugin/library/instruments.hpp"
#include "pme/plugin/library/locations.hpp"
#include "pme/plugin/state.hpp"
#include "pme/status.hpp"

namespace pme::plugin::library {

/// Current `presetSchemaVersion`.
inline constexpr int kPresetSchemaVersion = 1;

/// Parameter overrides by registry id, ascending id, each value different from the default.
using ParamOverrides = std::vector<std::pair<ParamId, double>>;

/// A decoded preset (schema version 1 in memory, whatever version was on disk).
struct Preset {
    std::string name;            ///< Display name.
    std::string instrumentId;    ///< Library id of the instrument (`factory/...` or `user/...`).
    std::string instrumentHash;  ///< `sha256:<hex>` of the instrument's canonical text at save
                                 ///< time; empty when unknown (a migrated version-0 preset).
    ParamOverrides params;       ///< Overrides, ascending id.

    friend bool operator==(const Preset&, const Preset&) = default;
};

/// Decode preset text of any supported version (D5-R2). Version 0 is migrated; a newer version is
/// the error `version` and nothing is loaded. Unknown or retired ids and unknown version-0 names
/// are dropped with the diagnostic `unknown-param`; out-of-range values are clamped with
/// `clamped`; non-finite values are the error `range`. Values equal to the default are dropped.
/// `fileStem` becomes the name when the file has none. `out` is unchanged on failure.
[[nodiscard]] Status parsePreset(std::string_view text, std::string_view fileStem, Preset& out,
                                 int& sourceVersion, Report& report);

/// Canonical text of the current version: compact JSON, keys sorted, shortest round-trip doubles,
/// LF, final newline. Values equal to the registry default and unknown ids are not written.
/// `range` error for a non-finite value.
[[nodiscard]] Status serializePreset(const Preset& preset, std::string& text, Report& report);

/// Read and decode a preset file; the stem is the file name without `kPresetExtension`.
[[nodiscard]] Status loadPresetFile(const std::filesystem::path& file, Preset& out,
                                    int& sourceVersion, Report& report);

/// Write the current version to `file` atomically.
[[nodiscard]] Status savePresetFile(const std::filesystem::path& file, const Preset& preset,
                                    Report& report);

/// Import a preset file of any supported version: migrate, validate against the registry and
/// write the current version to `user/presets/<name><kPresetExtension>`, `<name>` being
/// `reduceName(preset.name)` (a different preset with that name gets `-2`, `-3`, ...; an
/// identical one is reused). An instrument id that does not resolve still imports with the
/// diagnostic `not-found`. `id` receives the preset id (`user/<name>`).
[[nodiscard]] Status importPreset(const Locations& locations,
                                  const std::filesystem::path& sourceFile, std::string& id,
                                  Report& report);

/// Export the current version of a library preset to `targetFile`.
[[nodiscard]] Status exportPreset(const Locations& locations, std::string_view id,
                                  const std::filesystem::path& targetFile, Report& report);

/// Load a library preset and its instrument (blobs verified, D5-R5). Fails with `not-found` if
/// the instrument id does not resolve. The diagnostic `instrument-changed` is added when the
/// preset carries an instrument hash and the instrument's canonical hash differs.
[[nodiscard]] Status loadLibraryPreset(const Locations& locations, std::string_view id,
                                       Preset& preset, loader::InstrumentFile& instrument,
                                       Report& report);

/// List factory then user presets, sorted by id within each source; one bad entry does not fail
/// the listing.
[[nodiscard]] std::vector<ListEntry> listPresets(const Locations& locations);

/// A preset applied to a plug-in state: `instrument` = library id, `params` = overrides.
[[nodiscard]] StateData presetToState(const Preset& preset);

/// A plug-in state saved as a preset. `name` and `instrumentHash` come from the caller.
[[nodiscard]] Preset presetFromState(const StateData& state, std::string name,
                                     std::string instrumentHash);

}  // namespace pme::plugin::library
