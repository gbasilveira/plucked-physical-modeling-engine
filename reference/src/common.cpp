// SPDX-License-Identifier: MIT
#include "pme/ref/common.hpp"

#include <cmath>

namespace pme::ref {

StringPhysics derive_string(const StringParams& p) {
    if (!(p.L > 0.0) || !(p.dCore > 0.0) || !(p.rhoCore > 0.0) || !(p.ECore > 0.0) ||
        !(p.f1Target > 0.0)) {
        throw RefError("StringParams: L, dCore, rhoCore, ECore and f1Target must be positive");
    }
    if (p.b2 != 0.0) {
        throw RefError(
            "StringParams.b2: the sqrt(omega) loss has no local finite-difference form; the "
            "reference accepts only b2 = 0");
    }
    if (!p.sigmaOverride.empty()) {
        throw RefError("StringParams.sigmaOverride: a per-partial decay table is not modelled");
    }
    if (p.detuneH != 0.0) {
        throw RefError("StringParams.detuneH: the horizontal detune is not modelled");
    }
    if (p.bridgeEnd != BridgeEnd::AtL) {
        throw RefError("StringParams.bridgeEnd: only BridgeEnd::AtL is modelled");
    }
    if (p.b1 < 0.0 || p.b3 < 0.0) {
        throw RefError("StringParams: b1 and b3 must not be negative");
    }
    const double d_outer = p.dOuter > 0.0 ? p.dOuter : p.dCore;
    if (d_outer < p.dCore) {
        throw RefError("StringParams.dOuter must not be smaller than dCore");
    }
    StringPhysics out;
    out.length = p.L;
    // SPEC-STR "Wound strings".
    out.rho_a = kPi / 4.0 *
                (p.rhoCore * p.dCore * p.dCore +
                 p.packing * p.rhoWind * (d_outer * d_outer - p.dCore * p.dCore));
    out.ei = (1.0 + p.epsWind) * kPi * p.ECore * std::pow(p.dCore, 4) / 64.0;
    out.ea = p.ECore * kPi * p.dCore * p.dCore / 4.0;
    // SPEC-STR "Tension for pitch".
    out.tension =
        4.0 * p.L * p.L * out.rho_a * p.f1Target * p.f1Target - kPi * kPi * out.ei / (p.L * p.L);
    if (!(out.tension > 0.0)) {
        throw RefError("StringParams: the tension for the target pitch is not positive");
    }
    out.f1 = p.f1Target;
    out.b1 = p.b1;
    out.b3 = p.b3;
    out.clamped = p.boundary == Boundary::Clamped;
    return out;
}

StringLossCoefficients loss_coefficients(const StringPhysics& physics) {
    // SPEC-STR "Losses": sigma = b1 + b3 omega^2 with omega^2 = (T k^2 + EI k^4) / rhoA gives
    // sigma0 = b1, sigma1 = b3 T / rhoA, sigma3 = b3 EI / rhoA (Bilbao 2009, section 7.1.3).
    StringLossCoefficients c;
    c.sigma0 = physics.b1;
    c.sigma1 = physics.b3 * physics.tension / physics.rho_a;
    c.sigma3 = physics.b3 * physics.ei / physics.rho_a;
    return c;
}

double stiff_string_frequency_hz(const StringPhysics& physics, int n) {
    const double k = static_cast<double>(n) * kPi / physics.length;
    const double w2 = (physics.tension * k * k + physics.ei * k * k * k * k) / physics.rho_a;
    return std::sqrt(w2) / (2.0 * kPi);
}

double string_scheme_frequency_hz(const StringPhysics& physics, int intervals, int n, double k,
                                  double theta) {
    const double h = physics.length / intervals;
    const double sn = std::sin(n * kPi / (2.0 * intervals));
    const double a = 4.0 / (h * h) * sn * sn;
    const double lambda =
        (physics.tension / physics.rho_a) * a + (physics.ei / physics.rho_a) * a * a;
    const double s = k * k * lambda / 4.0;
    return (2.0 / k) * std::asin(std::sqrt(s / (1.0 + 4.0 * theta * s))) / (2.0 * kPi);
}

}  // namespace pme::ref
