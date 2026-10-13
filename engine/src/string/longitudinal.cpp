// SPDX-License-Identifier: MIT
// Longitudinal mode compilation (A7-R2). SPEC-GEO "Longitudinal modes"; SPEC-OSC "Bandwidth".
#include "pme/string/longitudinal.hpp"

#include <cmath>
#include <numbers>

#include "pme/string/string_model.hpp"

namespace pme::string {

namespace {

constexpr double kPi = std::numbers::pi;

bool finiteNonNegative(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

}  // namespace

double axialStiffness(const StringParams& params) noexcept {
    return params.ECore * kPi * params.dCore * params.dCore / 4.0;  // SPEC-GEO: the core's EA
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): arguments in SPEC-GEO order
Status compileLongitudinal(const StringModes& modes, const StringParams& params, int ML, double b1L,
                           double b3L, double fs, double fMax, LongitudinalModes& out) {
    if (validate(params) != Status::Ok || !(std::isfinite(fs) && fs > 0.0) || std::isnan(fMax) ||
        ML < 0 || !finiteNonNegative(b1L) || !finiteNonNegative(b3L) || modes.k.empty() ||
        !(modes.L > 0.0) || !(modes.rhoA > 0.0)) {
        return Status::InvalidArgument;
    }
    LongitudinalModes lm;
    lm.fs = fs;
    lm.L = modes.L;
    lm.EA = axialStiffness(params);
    lm.rhoA = modes.rhoA;  // the full linear mass: core plus winding (SPEC-GEO)
    lm.tension = modes.tension;
    lm.eaMinusT = lm.EA - lm.tension;
    if (!(lm.eaMinusT > 0.0) || !std::isfinite(lm.eaMinusT)) {
        return Status::InvalidArgument;  // EA - T <= 0: the string cannot be taut (SPEC-GEO)
    }
    lm.end = params.bridgeEnd;
    lm.requested = ML;
    lm.k = modes.k;

    const double length = modes.L;
    const double c = std::sqrt(lm.EA / lm.rhoA);  // SPEC-GEO: sqrt(EA / rho A)
    const double shape = std::sqrt(2.0 / length);
    const bool atL = lm.end == BridgeEnd::AtL;
    // Slope of transverse mode n at the bridge end: sqrt(2/L) k_n cos(k_n x_b), cos(n pi) = (-1)^n.
    lm.slopeCoeff.resize(modes.k.size());
    for (std::size_t n = 0; n < modes.k.size(); ++n) {
        const double sign = (atL && ((n + 1) % 2 == 1)) ? -1.0 : 1.0;
        lm.slopeCoeff[n] = sign * shape * modes.k[n];
    }
    lm.staticCoeff = 0.5 * lm.eaMinusT;  // SPEC-GEO F_L: (EA - T) / 2

    for (int m = 1; m <= ML; ++m) {
        const double K = static_cast<double>(m) * kPi / length;  // SPEC-GEO K_m
        const double omega = K * c;                              // SPEC-GEO omega_m^L
        const double sigma = b1L + b3L * omega * omega;          // SPEC-GEO own b1, b3
        // SPEC-OSC Bandwidth: modes are kept as the longest passing prefix.
        if (osc::passesBandwidthRule(omega, sigma, fs, fMax) != osc::BandwidthVerdict::Accepted) {
            break;
        }
        osc::OscCoeffsF64 coeffs;
        if (osc::compileOscillatorF64(omega, sigma, fs, coeffs) != Status::Ok) {
            // By design: a mode whose oscillator cannot be compiled ends the prefix silently, so
            // fewer longitudinal modes are kept (never an error).
            break;
        }
        lm.K.push_back(K);
        lm.omega.push_back(omega);
        lm.sigma.push_back(sigma);
        lm.osc.push_back(coeffs);
        // SPEC-GEO pair sum prefactor and F_L coefficient of xi_m.
        lm.pairCoeff.push_back(-lm.eaMinusT * K / (2.0 * lm.rhoA * std::sqrt(2.0 * length)));
        const double sign = (atL && (m % 2 == 1)) ? -1.0 : 1.0;
        lm.bridgeCoeff.push_back(lm.EA * shape * K * sign);
    }
    out = std::move(lm);
    return Status::Ok;
}

}  // namespace pme::string
