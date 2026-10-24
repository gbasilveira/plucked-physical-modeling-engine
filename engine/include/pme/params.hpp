// SPDX-License-Identifier: MIT
#pragma once

/// @file params.hpp
/// Parameter registry (P0.4-R4; SPEC-API "Parameter classes").
///
/// The registry lists instrument-wide parameters that can arrive as `Param` events: every
/// continuous parameter named by SPEC-API plus the instrument-wide structural parameters whose
/// defaults SPEC-INST gives. Per-key and per-segment physical parameters live in the description
/// structs, not here. Ids are stable 32-bit numbers, never reused or renumbered; see
/// docs/dev/interfaces.md for the allocation scheme.

#include <cstdint>
#include <span>
#include <string_view>

namespace pme {

/// Stable numeric id of a parameter.
using ParamId = std::uint32_t;

/// How a parameter change is applied (SPEC-API "Parameter classes", SPEC-EXEC "Parameter changes").
enum class ParamClass : std::uint8_t {
    Continuous,  ///< Ramped per sample on the audio thread.
    Structural,  ///< Recompiled off the audio thread and swapped at a block start.
};

/// One registry entry. String fields point to static storage.
struct ParamInfo {
    ParamId id{};           ///< Stable id.
    std::string_view name;  ///< Dotted machine name, for example "pedal.sustain".
    std::string_view unit;  ///< SI symbol, "dB" for decibels, "1" for dimensionless.
    double min{};           ///< Smallest valid value.
    double max{};           ///< Largest valid value.
    double defaultValue{};  ///< Default value, within [min, max].
    ParamClass cls{ParamClass::Continuous};  ///< Continuous or structural.
    std::string_view specKey;                ///< SPEC key (and section) the entry comes from.
    std::string_view
        description;  ///< Short description; says "provisional" where the range is a choice.
};

/// All registry entries sorted by ascending id.
/// Thread: any. Allocates: no. Real-time safe. Returns a span of static storage.
[[nodiscard]] std::span<const ParamInfo> paramRegistry() noexcept;

/// Look up an entry by id (binary search).
/// Thread: any. Allocates: no. Real-time safe. Returns null for an unused id.
[[nodiscard]] const ParamInfo* findParam(ParamId id) noexcept;

}  // namespace pme
