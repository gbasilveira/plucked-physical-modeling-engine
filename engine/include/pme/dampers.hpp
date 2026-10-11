// SPDX-License-Identifier: MIT
#pragma once

/// @file dampers.hpp
/// Damper description (SPEC-DMP; SPEC-API).

#include <cstdint>

namespace pme {

/// How a damper acts on the string (SPEC-DMP "Damper model").
enum class DamperMode : std::uint8_t {
    Diagonal,  ///< Extra per-mode damping and stiffness (default).
    RankOne,   ///< Exact rank-one spring and dashpot along the pad window.
};

/// Damper parameters (SPEC-API, SPEC-DMP). Defaults are SPEC-DMP's.
struct DamperParams {
    double xRatio{};       ///< Pad centre x_d / L (dimensionless) (SPEC-DMP "Damper model").
    double width{};        ///< Pad width b_d (m).
    double Kd{};           ///< Pad stiffness K_d (N/m).
    double Rd{};           ///< Pad resistance R_d (kg/s).
    double tau0{0.012};    ///< Landing time constant tau_0 (s); default 12 ms (SPEC-DMP "Timing").
    double kRelease{2.0};  ///< Release-velocity factor k_r (dimensionless); default 2.
    double tauUp{0.004};   ///< Lift time constant tau_up (s); 4 ms.
    double etaD{2.0};      ///< Contact shape exponent eta_d (dimensionless); default 2.
    bool hasDamper{true};  ///< False for the undamped top keys (SPEC-DMP "Undamped notes").
    DamperMode mode{DamperMode::Diagonal};  ///< Diagonal (default) or rank-one.
};

}  // namespace pme
