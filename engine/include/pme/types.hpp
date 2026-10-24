// SPDX-License-Identifier: MIT
#pragma once

/// @file types.hpp
/// Small vocabulary types shared by several modules.

namespace pme {

/// A point in three-dimensional space (SPEC-API `Point`; members from SPEC-BODY and SPEC-CONV).
/// Bridge points, plate outline, ribs, bridges and radiation channels are given in the body
/// frame (SPEC-BODY "Plate model", SI-0003): x and y span the plate's mid-surface in the
/// instrument's case frame defined by the body outline; the grain (L axis) lies at angle
/// theta_g from +x; z is normal to the plate and points towards the strings (the +y
/// polarisation axis). Bridge points and outline vertices use z = 0.
struct Point {
    double x{};  ///< Case-frame x, metres (SPEC-CONV).
    double y{};  ///< Case-frame y, metres (SPEC-CONV).
    double z{};  ///< Normal to the plate, towards the strings, metres (SPEC-CONV).
};

}  // namespace pme
