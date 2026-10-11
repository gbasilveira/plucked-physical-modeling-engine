// SPDX-License-Identifier: MIT
// Shared helpers of the A2 oracle tests ORC-03, ORC-04, ORC-05 and A2-T3. Everything here is
// derived from SPEC-STR ("Stiff string model") and SPEC-CONV, not from the engine sources: the
// wound-string mass and stiffness, the closed-form tension, the stiff-string frequencies, a
// Gauss-Legendre quadrature and the raised-cosine window g(x). Only the public header
// pme/string.hpp is used here.
#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "pme/string.hpp"

namespace pme::oracle::str {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kInf = std::numeric_limits<double>::infinity();

/// Formats a value in scientific notation with four significant digits for the report lines.
inline std::string sci(double v) {
    std::ostringstream out;
    out << std::scientific << std::setprecision(3) << v;
    return out.str();
}

/// Writes one report line (achieved errors) to stderr; ctest shows it with --output-on-failure -V.
inline void report(const std::string& line) {
    std::cerr << line << std::endl;  // NOLINT(performance-avoid-endl)
}

/// Status comparison as a bool (doctest cannot print the enum class in a failed expression).
inline bool is(pme::Status actual, pme::Status expected) { return actual == expected; }

/// A string described by physical constants. The test derives rhoA, EI, T, f0 and B from these
/// with the SPEC-STR formulas written out below, never from the engine's helpers.
struct StringSpec {
    std::string name;
    double L{};
    double dCore{};
    double rhoCore{7850.0};
    double ECore{2.0e11};
    double dOuter{};  ///< Equal to dCore for a plain string.
    double rhoWind{0.0};
    double packing{0.0};
    double epsWind{0.0};
    double f1{};  ///< Target first-partial frequency (Hz).
    double b1{0.0};
    double b2{0.0};
    double b3{0.0};
};

/// SPEC-STR "Wound strings": rhoA = (pi/4) [rho_c d_c^2 + eta_w rho_w (d_o^2 - d_c^2)].
inline double linearDensity(const StringSpec& s) {
    return 0.25 * kPi *
           (s.rhoCore * s.dCore * s.dCore +
            s.packing * s.rhoWind * (s.dOuter * s.dOuter - s.dCore * s.dCore));
}

/// SPEC-STR "Wound strings": EI = (1 + eps_w) pi E_c d_c^4 / 64.
inline double bendingStiffness(const StringSpec& s) {
    return (1.0 + s.epsWind) * kPi * s.ECore * std::pow(s.dCore, 4) / 64.0;
}

/// SPEC-STR "Tension for pitch": T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2.
inline double tensionForPitch(const StringSpec& s, double f1) {
    return 4.0 * s.L * s.L * linearDensity(s) * f1 * f1 -
           kPi * kPi * bendingStiffness(s) / (s.L * s.L);
}

/// SPEC-STR "Modes": B = pi^2 EI / (T L^2).
inline double inharmonicityOf(const StringSpec& s, double tension) {
    return kPi * kPi * bendingStiffness(s) / (tension * s.L * s.L);
}

/// SPEC-STR "Modes": f0 = omega0 / 2 pi = (1 / 2L) sqrt(T / rhoA).
inline double f0Of(const StringSpec& s, double tension) {
    return std::sqrt(tension / linearDensity(s)) / (2.0 * s.L);
}

/// SPEC-STR "Modes": f_n = n f0 sqrt(1 + B n^2).
inline double stiffPartial(double f0, double B, int n) {
    return static_cast<double>(n) * f0 * std::sqrt(1.0 + B * static_cast<double>(n) * n);
}

/// SPEC-STR "Modes": Fletcher's factor for clamped ends, 1 + 2 sqrt(B)/pi + 4 B/pi^2.
inline double fletcherFactor(double B) {
    return 1.0 + 2.0 * std::sqrt(B) / kPi + 4.0 * B / (kPi * kPi);
}

/// Converts a StringSpec into the public StringParams (pinned ends, no override table).
inline pme::StringParams toParams(const StringSpec& s, double f1) {
    pme::StringParams p;
    p.L = s.L;
    p.dCore = s.dCore;
    p.rhoCore = s.rhoCore;
    p.ECore = s.ECore;
    p.dOuter = s.dOuter;
    p.rhoWind = s.rhoWind;
    p.packing = s.packing;
    p.epsWind = s.epsWind;
    p.f1Target = f1;
    p.b1 = s.b1;
    p.b2 = s.b2;
    p.b3 = s.b3;
    p.boundary = pme::Boundary::Pinned;
    return p;
}

/// MIDI key to frequency, equal temperament with A4 = 440 Hz (key 69).
inline double keyFrequency(int key) {
    return 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
}

/// The strings of ORC-03 and ORC-04. B is the inharmonicity the SPEC-STR formulas give (printed
/// by the tests): about 1.8e-3, 1e-2, wound, 1e-5.
inline std::vector<StringSpec> oracleStrings() {
    std::vector<StringSpec> v;
    // Plain steel, f1 = 110 Hz, B about 1.8e-3.
    v.push_back({"plain steel 110 Hz", 0.65, 1.0e-3, 7850.0, 2.0e11, 1.0e-3, 0.0, 0.0, 0.0, 110.0});
    // Plain steel, f1 = 2 kHz, B about 1e-2 (short and thick for its pitch).
    v.push_back(
        {"plain steel 2 kHz B 1e-2", 0.07, 0.5e-3, 7850.0, 2.0e11, 0.5e-3, 0.0, 0.0, 0.0, 2000.0});
    // Wound bass, f1 = 27.5 Hz, eps_w = 0.1, with intrinsic losses (the table omega is undamped).
    v.push_back({"wound bass 27.5 Hz", 1.2, 1.6e-3, 7850.0, 2.0e11, 4.7e-3, 8960.0, 0.85, 0.1, 27.5,
                 0.3, 0.0, 1.0e-9});
    // Nearly flexible steel string, f1 = 110 Hz, B about 1e-5.
    v.push_back({"thin steel 110 Hz B 1e-5", 0.65, 0.075e-3, 7850.0, 2.0e11, 0.075e-3, 0.0, 0.0,
                 0.0, 110.0});
    return v;
}

/// Nodes and weights of the n-point Gauss-Legendre rule on [-1, 1] (Newton iteration on P_n).
struct GaussLegendre {
    std::vector<double> x;
    std::vector<double> w;
};

inline GaussLegendre gaussLegendre(int n) {
    GaussLegendre g;
    g.x.resize(static_cast<std::size_t>(n));
    g.w.resize(static_cast<std::size_t>(n));
    const auto legendre = [n](double z, double& pn, double& dpn) {
        double p0 = 1.0;
        double p1 = z;
        for (int k = 2; k <= n; ++k) {
            const double p2 = ((2.0 * k - 1.0) * z * p1 - (k - 1.0) * p0) / k;
            p0 = p1;
            p1 = p2;
        }
        pn = p1;
        dpn = static_cast<double>(n) * (z * p1 - p0) / (z * z - 1.0);
    };
    for (int i = 0; i < (n + 1) / 2; ++i) {
        double z = std::cos(kPi * (static_cast<double>(i) + 0.75) / (static_cast<double>(n) + 0.5));
        double pn = 0.0;
        double dpn = 1.0;
        for (int it = 0; it < 100; ++it) {
            legendre(z, pn, dpn);
            const double dz = pn / dpn;
            z -= dz;
            if (std::fabs(dz) < 1e-16) {
                break;
            }
        }
        legendre(z, pn, dpn);
        const double wi = 2.0 / ((1.0 - z * z) * dpn * dpn);
        const auto lo = static_cast<std::size_t>(i);
        const auto hi = static_cast<std::size_t>(n - 1 - i);
        g.x[lo] = -z;
        g.x[hi] = z;
        g.w[lo] = wi;
        g.w[hi] = wi;
    }
    return g;
}

/// Integrates f over [a, b] with the given rule applied on `panels` equal panels.
template <class F>
double integrate(const F& f, double a, double b, const GaussLegendre& rule, int panels = 1) {
    double sum = 0.0;
    const double h = (b - a) / static_cast<double>(panels);
    for (int p = 0; p < panels; ++p) {
        const double lo = a + h * static_cast<double>(p);
        const double mid = lo + 0.5 * h;
        for (std::size_t i = 0; i < rule.x.size(); ++i) {
            sum += rule.w[i] * 0.5 * h * f(mid + 0.5 * h * rule.x[i]);
        }
    }
    return sum;
}

/// SPEC-STR "Forces and observations": the raised-cosine (Hann) window of full width b and unit
/// area, g(s) = (1/b) [1 + cos(2 pi s / b)] for |s| < b/2 (s measured from the centre x0).
inline double raisedCosine(double s, double b) {
    return (std::fabs(s) < 0.5 * b) ? (1.0 + std::cos(2.0 * kPi * s / b)) / b : 0.0;
}

/// Mode shape phi_n(x) = sqrt(2/L) sin(n pi x / L) (SPEC-STR "Modes"), argument formed as
/// n pi (x/L) to keep it exact at the ends.
inline double modeShape(double L, int n, double x) {
    return std::sqrt(2.0 / L) * std::sin(static_cast<double>(n) * kPi * (x / L));
}

}  // namespace pme::oracle::str
