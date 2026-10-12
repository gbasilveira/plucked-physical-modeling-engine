// SPDX-License-Identifier: MIT
// Geometric tier selector (A7-R6). SPEC-CONV "Quality and numerical options"; SPEC-GEO
// "High-quality tier".
#include "pme/string/longitudinal_tier.hpp"

namespace pme::string {

Status selectGeometricTier(GeometricTier requested, GeometricQuality quality,
                           GeometricTier& resolved) noexcept {
    resolved = resolveGeometricTier(requested, quality);
    // The exact tier is phase A10: until it exists the selector reports NotImplemented.
    return resolved == GeometricTier::Exact ? Status::NotImplemented : Status::Ok;
}

}  // namespace pme::string
