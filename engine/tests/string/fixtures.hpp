// SPDX-License-Identifier: MIT
// Shared string descriptions for the A2 tests.
#pragma once

#include "pme/string.hpp"

namespace pme::string_test {

/// Plain steel string, d = 1.0 mm, rho = 7850 kg/m^3, E = 200 GPa (A2-T1 case a).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1
inline StringParams plainSteel(double L, double f1) {
    StringParams p;
    p.L = L;
    p.dCore = 1.0e-3;
    p.dOuter = 1.0e-3;
    p.rhoCore = 7850.0;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    p.b1 = 0.4;
    p.b2 = 1.0e-3;
    p.b3 = 1.0e-11;
    return p;
}

/// Wound bass string: core 1.6 mm, outer 4.7 mm, copper-like winding (A2-T1 case b).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1
inline StringParams woundBass(double L, double f1) {
    StringParams p;
    p.L = L;
    p.dCore = 1.6e-3;
    p.dOuter = 4.7e-3;
    p.rhoCore = 7850.0;
    p.rhoWind = 8960.0;
    p.packing = 0.85;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    p.b1 = 0.3;
    p.b2 = 5.0e-4;
    p.b3 = 5.0e-12;
    return p;
}

/// Treble string, f1 ~ 2 kHz, B ~ 1e-2.
inline StringParams treble() { return plainSteel(0.1, 2000.0); }
/// Mid string, f1 ~ 262 Hz.
inline StringParams mid() { return plainSteel(0.65, 262.0); }
/// Bass wound string, f1 ~ 27.5 Hz.
inline StringParams bass() { return woundBass(1.9, 27.5); }

}  // namespace pme::string_test
