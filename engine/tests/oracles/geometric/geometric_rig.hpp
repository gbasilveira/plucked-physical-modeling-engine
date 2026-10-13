// SPDX-License-Identifier: MIT
// The rig the A7 oracle tests drive: one string segment with tension modulation and one-way
// longitudinal modes (SPEC-GEO), float32 states as in the engine (A1 ModeBank). The rig is the only
// place in these tests that touches a geometric-nonlinearity implementation, so the tests
// themselves (test_orc11_orc12.cpp, test_a7t3_reference.cpp) call nothing under engine/src/string/
// longitudinal* or engine/src/nonlinear/tension*. Two backends implement `GeoRig`:
//
//   * PME_ORACLE_GEO_A7 defined: geometric_rig_a7.hpp, the adapter onto the A7 code under test.
//     Every line that touches an A7 name is marked `ADAPT` (the A7 headers did not exist when it
//     was written; see the file). Select it with -DPME_A7_ORACLE_BACKEND=A7.
//   * otherwise: geometric_rig_scratch.hpp, a self-contained scheme written from SPEC-GEO alone
//     (explicit Kirchhoff-Carrier force on the transverse modes, pair-sum forcing of the
//     longitudinal modes). It lets the tests build and run before A7 is merged and proves that
//     the measurement chain (quadrature, estimator, initial conditions) is right independently of
//     the implementation under test.
//
// Both backends see the same description: the compiled transverse tables (public
// pme::compileString), the string parameters, the number of longitudinal modes and their losses.
#pragma once

#include <cstddef>
#include <span>

#include "pme/string.hpp"

namespace pme::oracle::geo {

/// Inputs shared by both backends.
struct GeoSetup {
    pme::StringModes modes;        ///< compiled vertical/horizontal tables (public compileString)
    pme::StringParams params;      ///< the same description (E_c, d_c give EA, SPEC-GEO)
    int longitudinalModes{16};     ///< requested M_L (SPEC-GEO: pair sum for M_L <= 16)
    double b1L{0.0};               ///< longitudinal loss b_1 (1/s); SPEC-GEO "their own b_1, b_3"
    double b3L{0.0};               ///< longitudinal loss b_3 (s)
    double fs{48000.0};            ///< sample rate (Hz)
    double fMax{1.0e30};           ///< highest kept longitudinal frequency (Hz)
    bool tensionModulation{true};  ///< transverse feedback of Delta T (A7-R1)
};

}  // namespace pme::oracle::geo

#ifdef PME_ORACLE_GEO_A7
#include "geometric_rig_a7.hpp"
#else
#include "geometric_rig_scratch.hpp"
#endif
