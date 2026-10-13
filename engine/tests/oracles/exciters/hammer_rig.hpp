// SPDX-License-Identifier: MIT
// The rig the A5 oracle tests drive: one struck note (the strings of a note, one hammer, one
// bridge point), stepped in the order SPEC-EXEC prescribes. The rig is the only place in these
// tests that touches a hammer implementation, so the oracle tests themselves contain no call into
// engine/src/exciters. Two backends implement the same class interface, templated on the state
// type S (float in the engine, double for energy tests):
//
//   * ScratchRig<S> (hammer_rig_scratch.hpp): the SPEC-HAM / SPEC-NL / SPEC-BRG scheme written
//     out from the specification (potential, gradient, dashpots, una corda, launch, bridge point,
//     energy), reusing only A3's generic savStep for the rank-r solve. It builds before A5 is
//     merged and proves that the measurement chain (initial conditions, energy functional,
//     estimators, expected values) is right independently of the code under test. It is always
//     compiled; its test cases carry "(SPEC scratch scheme)" in their names.
//   * A5Rig<S> (hammer_rig_a5.hpp): a thin adapter onto the A5 StruckNote and HammerPotential
//     (the code under test). Compiled when PME_ORACLE_HAMMER_A5 is defined (CMake option
//     PME_A5_ORACLE_BACKEND=A5); its test cases carry "(A5 StruckNote)". The lines to adapt to the
//     merged A5 headers are marked "ADAPT" in that file.
//
// Both backends take the same NoteSetup. Observables are defined once, here, so the tests
// read the same quantities from either backend:
//   step()                 advance one sample n -> n + 1 (launch has been done by the constructor)
//   penetration()          max over struck strings of delta_s^n (state before the step), metres
//   contactForce()         applied SAV contact force g_H^n (psi^(n+1/2) + psi^(n-1/2)) / 2, newtons
//   hammerPosition()       x_H^(n+1) (metres), hammerSpeed(): u_H^(n+1) / dt (m/s)
//   psi()                  psi^(n+1/2)
//   bridgeForceV(s)        F_b,s^n of string s, vertical polarisation, newtons
//   bridgeW()              vertical bridge displacement w_V^(n+1)
//   energy()               SPEC-NL / SPEC-OSC / docs/dev/bridge.md energy functional after the step
//   resets()               psi reset-at-separation counter
#pragma once

#include <cstddef>
#include <vector>

#include "pme/exciters.hpp"
#include "pme/string.hpp"

namespace pme::oracle::ham {

/// The parallel-axis bridge model (SPEC-BRG, SPEC-BODY "Local bridge model"): rigid (no motion),
/// or a lumped mass-spring-damper (SPEC units: kg, N/m, N s/m).
struct BridgeModel {
    bool lumped{false};
    double mass{0.0};
    double stiffness{0.0};
    double damping{0.0};

    static BridgeModel rigid() { return {}; }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
    static BridgeModel massSpringDamper(double mass, double stiffness, double damping) {
        BridgeModel m;
        m.lumped = true;
        m.mass = mass;
        m.stiffness = stiffness;
        m.damping = damping;
        return m;
    }
};

/// Inputs shared by both backends. `strings` empty means a fixed rigid target at x_H = 0 (the
/// SPEC-HAM "Oracle" setup): no modes, one contact with delta = x_H.
struct NoteSetup {
    std::vector<pme::StringModes> strings;  ///< compiled tables, order = string index
    pme::HammerParams hammer;               ///< mass, K, p, muHC, width, strikeRatio, law
    pme::UnaCordaParams una;                ///< s0, s1, kappaUC, deltaPUC, leavingString
    double shift{0.0};                      ///< una corda shift s in [0, 1]
    double hammerSpeed{0.0};                ///< v_H at contact (m/s); launch gap v_H dt
    double approachSign{1.0};               ///< s_E
    BridgeModel bridge;                     ///< shared bridge point
    double thetaB{0.0};                     ///< bridge frame rotation (SI-0019 sense)
    double fs{48000.0};
    bool driftRelaxation{false};  ///< SavParams::driftRelaxation (off in the energy tests)
    double eps0{1.0e-12};         ///< SavParams::eps0
};

}  // namespace pme::oracle::ham

#include "hammer_rig_scratch.hpp"
#ifdef PME_ORACLE_HAMMER_A5
#include "hammer_rig_a5.hpp"
#endif
