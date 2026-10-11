// SPDX-License-Identifier: MIT
#pragma once

/// @file barriers.hpp
/// Distributed barriers the string collides with (SPEC-PLK "Distributed barriers"; SPEC-API).

#include <cstdint>
#include <vector>

namespace pme {

/// Barrier geometry family (SPEC-PLK "Barrier geometries"). The profile in `BarrierParams` is
/// the compiled truth; the kind records how it was derived and is available to editors.
enum class BarrierKind : std::uint8_t {
    Custom,      ///< Arbitrary profile.
    Fret,        ///< Narrow raised barrier at a fret position.
    Fretboard,   ///< Flat barrier at the action height.
    Jawari,      ///< Curved sitar or tanpura bridge surface.
    Thread,      ///< Tanpura thread: soft one-sided point barrier.
    Sawari,      ///< Shamisen or biwa nut barrier with gap g_s.
};

/// Barrier parameters (SPEC-API, SPEC-PLK).
struct BarrierParams {
    BarrierKind kind{BarrierKind::Custom};  ///< Geometry family.
    std::vector<double> x;  ///< Sample positions x_j along the string (m).
    std::vector<double> b;  ///< Profile b(x_j) (m).
    std::vector<double> w;  ///< Quadrature weights Delta x_j (m).
    int side{1};            ///< Side s_b: +1 or -1.
    double K{};             ///< Contact stiffness K_B (N/m^alpha per m).
    double alpha{};         ///< Contact exponent alpha (dimensionless).
    double muB{};           ///< Contact damping mu_B (s/m).
};

}  // namespace pme
