// SPDX-License-Identifier: MIT
// Input of the "coefficients under test" seam of ORC-13 (see "A6 SEAM" in
// test_orc13_damper_diagonal.cpp). The adapter a6_adapter.hpp, written after A6 is merged, includes
// this header and defines
//   pme::Status pme::oracle::dmp::a6Coefficients(const ModeInput&, pme::OscCoeffs&)
// by calling A6's diagonal-damper coefficient function for one mode at engagement ModeInput::e.
#pragma once

namespace pme::oracle::dmp {

/// Everything a diagonal damper needs to know about one mode (SPEC-DMP "Diagonal"). All SI,
/// float64; phiBar is the damper window's shape value phi-bar_{d,n} (m^(-1/2)) computed by the
/// test's own quadrature.
struct ModeInput {
    double fs{};      ///< Sample rate (Hz).
    double e{};       ///< Engagement e (1 in ORC-13).
    double omega{};   ///< Undamped angular frequency omega_n (rad/s), SPEC-STR "Modes".
    double sigma{};   ///< String decay rate sigma_n (1/s), SPEC-STR "Losses".
    double rhoA{};    ///< Linear mass density (kg/m).
    double phiBar{};  ///< phi-bar_{d,n} (m^(-1/2)).
    double Kd{};      ///< Pad stiffness K_d (N/m).
    double Rd{};      ///< Pad resistance R_d (kg/s).
};

}  // namespace pme::oracle::dmp
