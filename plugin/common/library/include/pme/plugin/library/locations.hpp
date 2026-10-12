// SPDX-License-Identifier: MIT
#pragma once

/// @file locations.hpp
/// Install locations (D5-R1). Non-audio; allocates; never throws.
///
/// SPEC sections used: SPEC-INST "Instrument file" (instruments, presets and body blobs live
/// outside the engine). Every library function takes a `Locations`; there is no global state, so
/// tests use temporary directories and never touch the real user folders.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "pme/plugin/library/error.hpp"
#include "pme/status.hpp"

namespace pme::plugin::library {

/// The three roots. `factory` is read-only for the library (H2 installs into it); `user` holds
/// imported instruments and presets; `cache` holds compiled body data.
struct Locations {
    std::filesystem::path factory;  ///< Bundled instruments and presets.
    std::filesystem::path user;     ///< User instruments and presets.
    std::filesystem::path cache;    ///< Compiled body data (cache/body/<key>.bin).

    [[nodiscard]] std::filesystem::path factoryInstruments() const { return factory / "instruments"; }
    [[nodiscard]] std::filesystem::path factoryPresets() const { return factory / "presets"; }
    [[nodiscard]] std::filesystem::path userInstruments() const { return user / "instruments"; }
    [[nodiscard]] std::filesystem::path userPresets() const { return user / "presets"; }
    [[nodiscard]] std::filesystem::path cacheBody() const { return cache / "body"; }
};

/// Where an entry comes from.
enum class Source : unsigned char {
    Factory,  ///< Library id prefix `factory/`.
    User,     ///< Library id prefix `user/`.
};

/// Systems whose rules can be evaluated from an environment lookup on any host (the Windows rules
/// need the known-folder API and exist only in `defaultLocations()` on Windows).
enum class Platform : unsigned char { Linux, MacOs };

/// Environment lookup: the value of a variable, or nothing if it is unset.
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

/// The location rules of the table in docs/dev/library.md for `platform`, reading the environment
/// through `env`. An unset or relative `HOME` (where the rules need it) is an error `io`. On Linux,
/// `XDG_*` values that are not absolute are ignored, as the XDG specification says. The factory
/// directory is the first existing `<dir>/<data dir>` of `XDG_DATA_DIRS`, or the first entry if
/// none exists yet.
[[nodiscard]] Status locationsFromEnvironment(Platform platform, const EnvLookup& env,
                                              Locations& out, Report& report);

/// The default locations of the current system (Linux/macOS: the process environment; Windows:
/// `SHGetKnownFolderPath`). Never guesses: failure is `InvalidArgument` with an `io` error.
[[nodiscard]] Status defaultLocations(Locations& out, Report& report);

/// A parsed library id `factory/<name>` or `user/<name>`.
struct LibraryId {
    Source source{Source::User};
    std::string name;  ///< Directory name under `instruments/`, or preset file stem.
};

/// Parse and validate an id. `<name>` is one or more of `[A-Za-z0-9._-]`, not starting with `.`;
/// anything else (`../`, absolute paths, separators, colons) is `invalid-id`, so an id can never
/// leave its root.
[[nodiscard]] Status parseLibraryId(std::string_view id, LibraryId& out, Report& report);

/// Format an id.
[[nodiscard]] std::string formatLibraryId(Source source, std::string_view name);

/// Path of `instrument.json` for an instrument id. `invalid-id` or `not-found` on failure.
[[nodiscard]] Status resolveInstrument(const Locations& locations, std::string_view id,
                                       std::filesystem::path& instrumentJson, Report& report);

/// Path of the preset file for a preset id (`factory/<name>` or `user/<name>`; the file is
/// `presets/<name><kPresetExtension>`). `invalid-id` or `not-found` on failure.
[[nodiscard]] Status resolvePreset(const Locations& locations, std::string_view id,
                                   std::filesystem::path& presetFile, Report& report);

}  // namespace pme::plugin::library
