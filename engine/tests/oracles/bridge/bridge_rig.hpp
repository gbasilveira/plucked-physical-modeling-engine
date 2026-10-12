// SPDX-License-Identifier: MIT
// The rig the A4 oracle tests drive: one bridge point shared by several string segments, all in
// float32 as in the engine (A1 ModeBank), stepped in the order SPEC-BRG "Discrete coupling order"
// prescribes. The rig is the only place in these tests that touches a bridge implementation, so
// the oracle tests themselves (test_orc06_orc07.cpp, test_a4t3_reference.cpp) contain no call
// into engine/src/bridge. Two backends implement `Rig::step()`:
//
//   * PME_ORACLE_BRIDGE_A4 defined: bridge_rig_a4.hpp, a thin adapter onto the A4 `BridgePoint`
//     and `bridgeForce` (the code under test). Select it with -DPME_A4_ORACLE_BACKEND=A4 (or AUTO,
//     the default, once engine/src/bridge/include/pme/bridge/bridge_point.hpp exists).
//   * otherwise: bridge_rig_scratch.hpp, a self-contained scheme written from SPEC-BRG alone
//     (about 60 lines). It lets the tests build and run before A4 is merged, and it proves that
//     the measurement chain (initial conditions, partial tracking, expected values) is right
//     independently of the implementation under test.
//
// Both backends see the same description: compiled `StringModes` (public pme::compileString),
// the bridge end, and a bridge model (pure conductance G, or mass-spring-damper m, k_b, r_b),
// theta_b and G_h (default 0: only vertical motion is excited and G_h is irrelevant). The
// horizontal polarisation uses the same mode tables as the vertical one (the reference has no
// detuneH), and theta_b follows SI-0019: R(theta) = [[c, -s], [s, c]],
// F_par = c F_v - s F_h, F_perp = s F_v + c F_h, w_v = c w_par + s w_perp, w_h = -s w_par + c
// w_perp.
#pragma once

#include <cstddef>
#include <vector>

#include "pme/osc/mode_bank.hpp"
#include "pme/string.hpp"

namespace pme::oracle::brg {

/// The pluggable parallel-axis bridge model (SPEC-BRG "Polarisation coupling", SPEC-BODY "Local
/// bridge model"): a conductance, or a lumped mass-spring-damper (SPEC units: kg, N/m, N s/m).
struct BridgeModel {
    bool massSpringDamper{false};
    double conductance{0.0};  ///< G (s/kg) when !massSpringDamper
    double mass{0.0};         ///< m (kg)
    double stiffness{0.0};    ///< k_b (N/m)
    double damping{0.0};      ///< r_b (N s/m)

    static BridgeModel pureConductance(double g) {
        BridgeModel m;
        m.conductance = g;
        return m;
    }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
    static BridgeModel lumped(double mass, double stiffness, double damping) {
        BridgeModel m;
        m.massSpringDamper = true;
        m.mass = mass;
        m.stiffness = stiffness;
        m.damping = damping;
        return m;
    }
};

/// Inputs shared by both backends.
struct RigSetup {
    std::vector<pme::StringModes> segments;  ///< compiled vertical modes, order = segment index
    std::vector<pme::BridgeEnd> ends;        ///< same size as segments
    BridgeModel model;
    double fs{48000.0};
    double thetaB{0.0};        ///< bridge frame rotation theta_b (rad), SI-0019 sense
    double conductanceH{0.0};  ///< G_h of the perpendicular axis (0 = rigid)
};

}  // namespace pme::oracle::brg

#ifdef PME_ORACLE_BRIDGE_A4
#include "bridge_rig_a4.hpp"
#else
#include "bridge_rig_scratch.hpp"
#endif
