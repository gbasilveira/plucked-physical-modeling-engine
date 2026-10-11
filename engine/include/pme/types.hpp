// SPDX-License-Identifier: MIT
#pragma once

/// @file types.hpp
/// Small vocabulary types shared by several modules.

namespace pme {

/// A point in three-dimensional space (SPEC-API `Point`; members from SPEC-BODY and SPEC-CONV).
/// Bridge points and radiation channels are given in the body's coordinate frame, which is
/// defined by the body description (SPEC-BODY, "Plate model"): x along the grain (L axis), y
/// across it (R axis), z normal to the plate towards the strings. Provisional: SPEC-BODY does not
/// spell out the frame; bridge points of planar bodies use z = 0.
struct Point {
    double x{};  ///< Coordinate along the grain, metres (SPEC-CONV). Provisional frame.
    double y{};  ///< Coordinate across the grain, metres (SPEC-CONV). Provisional frame.
    double z{};  ///< Coordinate normal to the plate, metres (SPEC-CONV). Provisional frame.
};

}  // namespace pme
