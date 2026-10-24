// SPDX-License-Identifier: MIT
// Geometric tier selector (A7-R6; D-13).
//
// SPEC sections implemented: SPEC-CONV "Quality and numerical options" (`numerics.geometricTier`:
// Auto, Default, Exact; Auto is Default under Realtime and Exact under HighQuality); SPEC-GEO
// "High-quality tier" (the exact tier is phase A10).
//
// Until ICR-0002 is applied these are internal enums whose names and values match the ICR
// (`GeometricTier`, `EngineQuality`); the string module owns them for A7 and A10.
#pragma once

#include <cstdint>

#include "pme/status.hpp"

namespace pme::string {

/// `numerics.geometricTier` (ICR-0002).
enum class GeometricTier : std::uint8_t {
    Auto,     ///< Take the tier the quality level implies.
    Default,  ///< Tension modulation plus one-way longitudinal modes (this phase).
    Exact,    ///< The geometrically exact string (phase A10).
};

/// `EngineConfig::quality` (ICR-0002).
enum class GeometricQuality : std::uint8_t { Realtime, HighQuality };

/// Resolve `Auto` against the quality level (SPEC-CONV): Realtime gives Default, HighQuality gives
/// Exact. An explicit value is returned as given. Thread: any. Allocates: no.
[[nodiscard]] constexpr GeometricTier resolveGeometricTier(GeometricTier requested,
                                                           GeometricQuality quality) noexcept {
    if (requested != GeometricTier::Auto) {
        return requested;
    }
    return quality == GeometricQuality::HighQuality ? GeometricTier::Exact : GeometricTier::Default;
}

/// Select the tier for a segment with geometric features: `Ok` with `resolved` = Default, or
/// `NotImplemented` (resolved = Exact) until phase A10 provides the exact tier. Thread:
/// non-audio. Allocates: no.
[[nodiscard]] Status selectGeometricTier(GeometricTier requested, GeometricQuality quality,
                                         GeometricTier& resolved) noexcept;

}  // namespace pme::string
