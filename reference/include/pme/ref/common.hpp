// SPDX-License-Identifier: MIT
// Shared definitions of the P0.5 reference solver: the error type and the physical quantities
// derived from a StringParams description. The reference solver is independent of the engine: it
// uses the description structures of engine/include/pme (header-only use) and no engine code.
#pragma once

#include <numbers>
#include <stdexcept>
#include <string>

#include "pme/string.hpp"

namespace pme::ref {

inline constexpr double kPi = std::numbers::pi;

// Thrown for every input the reference does not model and for every non-convergence. The message
// names the offending field or quantity.
class RefError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Physical quantities of one string (SPEC-STR). SI units.
struct StringPhysics {
    double length = 0.0;   // L (m)
    double rho_a = 0.0;    // rho A (kg/m)
    double ei = 0.0;       // EI (N m^2)
    double tension = 0.0;  // T (N)
    double ea = 0.0;       // EA, the core's axial stiffness E_c pi d_c^2 / 4 (N), SPEC-GEO
    double f1 = 0.0;       // target first-partial frequency (Hz)
    double b1 = 0.0;       // sigma_0 (1/s)
    double b3 = 0.0;       // sigma(omega) = b1 + b3 omega^2 (s)
    bool clamped = false;  // Boundary::Clamped
};

// SPEC-STR "Wound strings" and "Tension for pitch", re-implemented here (not called from the
// engine):
//   rhoA = pi/4 [rho_c d_c^2 + eta_w rho_w (d_o^2 - d_c^2)],  EI = (1 + eps_w) pi E_c d_c^4 / 64,
//   T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2.
// A zero outer diameter means "plain string" (d_o = d_c). Throws RefError for b2 != 0 (the
// sqrt(omega) loss has no local finite-difference form), for a per-partial sigma table, for a
// non-zero horizontal detune, for bridgeEnd At0 and for a non-positive tension.
StringPhysics derive_string(const StringParams& params);

// Linear loss of SPEC-STR "Losses" restricted to b2 = 0: sigma(omega) = b1 + b3 omega^2.
// Mapping to the finite-difference loss 2 sigma0 d_t y - 2 sigma1 d_t d_xx y + 2 sigma3 d_t d_xxxx
// y:
//   sigma0 = b1,  sigma1 = b3 T / rhoA,  sigma3 = b3 EI / rhoA.
struct StringLossCoefficients {
    double sigma0 = 0.0;
    double sigma1 = 0.0;
    double sigma3 = 0.0;
};
StringLossCoefficients loss_coefficients(const StringPhysics& physics);

// Continuous stiff-string partial frequency (Hz), SPEC-STR "Modes":
//   omega_n^2 = (T k_n^2 + EI k_n^4) / rhoA, k_n = n pi / L. Pinned ends.
double stiff_string_frequency_hz(const StringPhysics& physics, int n);

// Frequency (Hz) of partial n of the fully discrete scheme for a pinned-pinned string with N grid
// intervals, time step k and theta: the discrete sine modes are exact eigenvectors, so
//   lambda = (T/rhoA) a + (EI/rhoA) a^2,  a = (4/h^2) sin^2(n pi / (2 N)),
//   sin^2(omega k / 2) = (k^2 lambda / 4) / (1 + theta k^2 lambda)   (Bilbao 2009, section 7.2.2).
double string_scheme_frequency_hz(const StringPhysics& physics, int intervals, int n, double k,
                                  double theta);

}  // namespace pme::ref
