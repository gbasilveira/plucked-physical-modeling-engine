// SPDX-License-Identifier: MIT
#pragma once

/// @file ids.hpp
/// Provisional plug-in identifiers (D1-R1). Decision D-3 (product name and plug-in identifiers) is
/// open: every value here is provisional and is defined in this one header so that D2 can replace
/// it when D-3 is answered. Nothing else in the repository spells these strings.

namespace pme::plugin {

#ifndef PME_PLUGIN_VERSION_STRING
#define PME_PLUGIN_VERSION_STRING "0.0.0"
#endif

/// CLAP plug-in id (provisional, D-3). Reverse-DNS style once D-3 is answered.
inline constexpr const char* kPluginId = "pme.instrument";
/// Display name (provisional, D-3).
inline constexpr const char* kPluginName = "pme";
/// Vendor (provisional, D-3).
inline constexpr const char* kPluginVendor = "pme project";
/// Project URL (empty until D-3).
inline constexpr const char* kPluginUrl = "";
/// One-line description.
inline constexpr const char* kPluginDescription =
    "Physical-modelling piano and plucked-string instrument";
/// Plug-in version: the CMake project version (project(pme VERSION ...)).
inline constexpr const char* kPluginVersion = PME_PLUGIN_VERSION_STRING;

/// Number of keys: one voice (note group) per key until B1/C1 give the instrument's group count.
inline constexpr int kKeyCount = 128;

// ---- LV2 (D3) -------------------------------------------------------------------------------
// Each value below is a single-line string literal: plugin/lv2/CMakeLists.txt reads this header
// with file(STRINGS ...) to name the bundle directory and the module, and the Turtle generator
// writes the plug-in URI from it, so no other file spells these strings.

/// LV2 plug-in URI (provisional, D-3). The reserved `.invalid` domain never resolves to anyone.
inline constexpr const char* kLv2PluginUri = "https://pme.invalid/lv2/instrument";
/// LV2 bundle directory name (provisional, D-3).
inline constexpr const char* kLv2BundleDir = "pme.lv2";
/// LV2 module base name, without prefix or suffix (provisional, D-3).
inline constexpr const char* kLv2BinaryName = "pme_lv2";

// ---- Library (D5) ---------------------------------------------------------------------------
// Names of the instrument and preset library (plugin/common/library). Provisional until D-3.

/// Directory name under the per-system data, cache and factory roots (provisional, D-3).
inline constexpr const char* kDataDirName = "pme";
/// Preset file extension, with the dot (provisional, D-3).
inline constexpr const char* kPresetExtension = ".pmepreset";
/// Value of the `format` member of a preset file (provisional, D-3).
inline constexpr const char* kPresetFormat = "pme-preset";

}  // namespace pme::plugin
