// SPDX-License-Identifier: MIT
#pragma once

/// @file turtle.hpp
/// Turtle descriptors of the LV2 bundle (D3-R2), generated from `pme::paramRegistry()` and
/// ids.hpp so that ports and parameters cannot drift from the registry. No LV2 headers needed.

#include <string>
#include <string_view>

#include "pme/pme.hpp"

namespace pme::lv2 {

/// URI of the structural parameter `info`: `<plug-in URI>#param.<dotted name>`.
[[nodiscard]] std::string structuralParamUri(const ParamInfo& info);
/// URI of the instrument property: `<plug-in URI>#instrument`.
[[nodiscard]] std::string instrumentUri();
/// URI of the state key: `<plug-in URI>#state`.
[[nodiscard]] std::string stateUri();
/// LV2 symbol of a parameter: the dotted name with `.` replaced by `_`.
[[nodiscard]] std::string portSymbol(const ParamInfo& info);

/// `manifest.ttl` for a module file named `binaryFile` (for example `pme_lv2.so`).
[[nodiscard]] std::string generateManifest(std::string_view binaryFile);
/// The plug-in description (`pluginFileName()`). Returns an empty string and sets `error` when
/// a continuous registry entry is missing from `kControlPortParams`.
[[nodiscard]] std::string generatePlugin(std::string& error);
/// File name of the plug-in description inside the bundle.
[[nodiscard]] std::string pluginFileName();

}  // namespace pme::lv2
