// SPDX-License-Identifier: MIT
#pragma once

/// @file state.hpp
/// State serialiser (D1-R4). JSON through nlohmann/json (ADR-0005 permits it in non-real-time
/// code; this runs only on the main thread). Format, schema version 1:
///
///     {"instrument":"", "interfaces_version":2, "params":{"2001":-6.5},
///      "plugin_version":"0.0.0", "state_schema_version":1}
///
/// `params` holds only values that differ from the registry default, keyed by id.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pme/pme.hpp"

namespace pme::plugin {

/// Current `state_schema_version`.
inline constexpr int kStateSchemaVersion = 1;

/// Decoded state. Values are exactly as stored: clamping and unknown-id handling are the
/// controller's job.
struct StateData {
    int schemaVersion{kStateSchemaVersion};
    std::string pluginVersion;
    int interfacesVersion{interfacesVersionMajor};
    std::string instrument;                           ///< Instrument reference; empty until B1.
    std::vector<std::pair<ParamId, double>> params;   ///< Ascending id.
};

/// Outcome of `parseState`.
enum class StateParse : std::uint8_t {
    Ok,
    Malformed,    ///< Not JSON of the expected shape, or empty.
    NewerSchema,  ///< `state_schema_version` is greater than `kStateSchemaVersion`.
};

/// Serialise to compact JSON, keys in sorted order, doubles in their shortest exact form.
/// Main thread. Allocates. Never throws.
[[nodiscard]] std::string serializeState(const StateData& state) noexcept;

/// Parse. On any result other than `Ok`, `out` is unchanged. Main thread. Allocates. Never throws.
[[nodiscard]] StateParse parseState(std::string_view text, StateData& out) noexcept;

}  // namespace pme::plugin
