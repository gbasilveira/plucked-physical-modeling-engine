// SPDX-License-Identifier: MIT
// Shared helpers of the A4 oracle tests ORC-06, ORC-07 and A4-T3. Expected values come from the
// SPEC-STR / SPEC-BRG formulas written out in string_oracle_common.hpp (the A2 test-author's
// SPEC-derived helpers, read only) and below; the engine is reached through the public
// pme::compileString and the rig of bridge_rig.hpp.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "bridge_rig.hpp"
#include "pme/analysis/partials.hpp"
#include "pme/string.hpp"
#include "string_oracle_common.hpp"

namespace pme::oracle::brg {

namespace str = pme::oracle::str;
using str::kPi;

constexpr double kFs = 48000.0;
constexpr double kLength = 0.65;  // m, speaking length used by every A4 oracle string

/// A string with SPEC-derived scalars, compiled at kFs with f_max = +inf, bridge at x = L.
struct OracleString {
    str::StringSpec spec;
    double tension{};  ///< SPEC-STR "Tension for pitch" from the spec's own f1
    double rhoA{};     ///< SPEC-STR "Wound strings"
    double z0{};       ///< sqrt(T rhoA) (SPEC-BRG)
    double f0{};       ///< (1/2L) sqrt(T/rhoA) (SPEC-STR "Modes")
    double b{};        ///< B = pi^2 EI / (T L^2)
    pme::StringModes modes;
};

/// Plain steel string of core diameter d and target f1, loss b1, b3, pinned. Returns false and
/// leaves `out` partly filled when compileString does not return Ok.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline bool makeString(double f1, double d, double b1, double b3, double fMax, OracleString& out,
                       std::span<const double> points = {}) {
    str::StringSpec s;
    s.name = "steel";
    s.L = kLength;
    s.dCore = d;
    s.dOuter = d;
    s.f1 = f1;
    s.b1 = b1;
    s.b3 = b3;
    out.spec = s;
    out.rhoA = str::linearDensity(s);
    out.tension = str::tensionForPitch(s, f1);
    out.z0 = std::sqrt(out.tension * out.rhoA);
    out.f0 = str::f0Of(s, out.tension);
    out.b = str::inharmonicityOf(s, out.tension);
    return pme::compileString(str::toParams(s, f1), kFs, fMax, points, out.modes) ==
           pme::Status::Ok;
}

/// Starts mode i of segment s at q^0 = q0 with the exact velocity of the free damped solution
/// q(t) = q0 e^(-sigma t) (cos w_d t + (sigma / w_d) sin w_d t) (zero initial velocity):
/// u^0 = q^0 - q^(-1). The coupling then perturbs it; any initial pair is a sum of the coupled
/// eigenmodes, so this only fixes the amplitudes.
inline void startMode(Rig& rig, std::size_t s, std::size_t i, const pme::StringModes& modes,
                      double q0) {
    const double dt = 1.0 / kFs;
    const double sigma = modes.sigma[i];
    const double wd = std::sqrt((modes.omega[i] - sigma) * (modes.omega[i] + sigma));
    const double qPrev =
        q0 * std::exp(sigma * dt) * (std::cos(wd * dt) - (sigma / wd) * std::sin(wd * dt));
    rig.q(s)[i] = static_cast<float>(q0);
    rig.u(s)[i] = static_cast<float>(q0 - qPrev);
}

/// SPEC-STR "Modes": f_n = n f0 sqrt(1 + B n^2); the damped value sqrt(w^2 - sigma^2) / 2 pi.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline double dampedPartialHz(const OracleString& s, int n, double sigma) {
    const double w = 2.0 * kPi * str::stiffPartial(s.f0, s.b, n);
    return std::sqrt((w - sigma) * (w + sigma)) / (2.0 * kPi);
}

/// Fits one damped sinusoid to `y` (sample rate kFs) starting from `approxHz`.
inline pme::analysis::PartialEstimate fitOne(const std::vector<double>& y, double approxHz) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = {approxHz};
    opt.envelope_frame_s = 0.0;
    const auto res = pme::analysis::track_partials(std::span<const double>(y), kFs, opt);
    return res.partials.at(0);
}

/// Fits several damped sinusoids jointly and returns the one at `index` of `approxHz`.
inline pme::analysis::PartialEstimate fitJoint(const std::vector<double>& y,
                                               const std::vector<double>& approxHz,
                                               std::size_t index) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = approxHz;
    opt.envelope_frame_s = 0.0;
    const auto res = pme::analysis::track_partials(std::span<const double>(y), kFs, opt);
    return res.partials.at(index);
}

}  // namespace pme::oracle::brg
