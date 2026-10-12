// SPDX-License-Identifier: MIT
// Shared helpers of the A5 oracle tests ORC-08 (hammer case), ORC-09, ORC-19 and A5-T2. Everything
// here is derived from SPEC-HAM, SPEC-NL, SPEC-STR, SPEC-BRG and SPEC-OSC, never from the A5
// sources: the contact-time closed form, the raised-cosine window average by quadrature (not
// from A2's windowedShapes), the lifting-function coefficients g_n, the una corda smoothstep and
// weights, and the estimators (contact duration, peak force, refinement ladder). The engine is
// reached only through the rig of hammer_rig.hpp. Only public headers are used here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include "pme/exciters.hpp"
#include "pme/string.hpp"
#include "string_oracle_common.hpp"

namespace pme::oracle::ham {

namespace str = pme::oracle::str;
inline constexpr double kPi = str::kPi;

/// The rigid-bridge-free typical hammer of the SPEC-ORC rows: 8 g, K = 4.5e9 N/m^p, p = 2.5
/// (Chaigne and Askenfelt 1994, middle C), SPEC-HAM "Oracle".
inline constexpr double kHammerMass = 0.008;
inline constexpr double kFeltK = 4.5e9;
inline constexpr double kFeltP = 2.5;
inline constexpr double kLength = 0.65;  // m, speaking length of every A5 oracle string

/// SPEC-HAM "Oracle": contact time of a mass m at speed v on K [delta]_+^p (no dissipation):
/// t_c = 2 (delta_m / v) sqrt(pi) Gamma(1 + 1/(p+1)) / Gamma(1/2 + 1/(p+1)),
/// delta_m = ((p+1) m v^2 / (2 K))^(1/(p+1)).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline double analyticContactTime(double mass, double speed, double k, double p) {
    const double a = 1.0 / (p + 1.0);
    const double deltaM = std::pow((p + 1.0) * mass * speed * speed / (2.0 * k), a);
    return 2.0 * (deltaM / speed) * std::sqrt(kPi) * std::tgamma(1.0 + a) / std::tgamma(0.5 + a);
}

/// A plain steel string with SPEC-derived scalars. `modes` is the engine's compiled table
/// (public compileString) at the chosen fs and f_max.
struct OracleString {
    str::StringSpec spec;
    double tension{};  ///< SPEC-STR "Tension for pitch" from the spec's own f1
    double rhoA{};     ///< SPEC-STR "Wound strings"
    double z0{};       ///< sqrt(T rhoA) (SPEC-BRG)
    double f0{};       ///< (1/2L) sqrt(T/rhoA) (SPEC-STR "Modes")
    double b{};        ///< B = pi^2 EI / (T L^2)
    pme::StringModes modes;
};

/// Plain steel string of core diameter d and target f1, losses b1 and b3 (b2 = 0), pinned,
/// speaking length kLength. Returns false when compileString does not return Ok.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline bool makeString(double f1, double d, double b1, double b3, double fs, double fMax,
                       OracleString& out) {
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
    const std::vector<double> none;
    return pme::compileString(str::toParams(s, f1), fs, fMax, none, out.modes) == pme::Status::Ok;
}

/// Core diameter that gives inharmonicity B at pitch f1 on a plain steel string of length kLength:
/// from B T L^2 = pi^2 EI with T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2 (SPEC-STR "Tension for pitch"),
/// d^2 = 64 B L^4 rho f1^2 / (pi^2 (1 + B) E).
inline double diameterForInharmonicity(double bTarget, double f1) {
    const double rho = 7850.0;
    const double young = 2.0e11;
    const double l4 = std::pow(kLength, 4);
    return std::sqrt(64.0 * bTarget * l4 * rho * f1 * f1 / (kPi * kPi * (1.0 + bTarget) * young));
}

/// Window-averaged mode values phi-bar_n = integral g(x - x0) phi_n(x) dx for n = 1..N, g the
/// unit-area raised cosine of full width b (SPEC-STR "Forces and observations"), by Gauss-Legendre
/// quadrature (4 panels of 32 points, float64). b = 0 gives the point shape phi_n(x0) (W = 1).
inline std::vector<double> windowedShape(const pme::StringModes& m, double x0, double b) {
    static const str::GaussLegendre rule = str::gaussLegendre(32);
    std::vector<double> out(m.omega.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int n = static_cast<int>(i) + 1;
        if (b <= 0.0) {
            out[i] = str::modeShape(m.L, n, x0);
            continue;
        }
        const auto f = [&](double x) {
            return str::raisedCosine(x - x0, b) * str::modeShape(m.L, n, x);
        };
        out[i] = str::integrate(f, x0 - 0.5 * b, x0 + 0.5 * b, rule, 4);
    }
    return out;
}

/// SPEC-BRG "Modal equation with a moving support", bridge at x = L:
/// g_n = sqrt(2/L) (-1)^(n+1) / k_n.
inline std::vector<double> bridgeG(const pme::StringModes& m) {
    std::vector<double> g(m.omega.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        g[i] = std::sqrt(2.0 / m.L) * sign / m.k[i];
    }
    return g;
}

/// SPEC-HAM "Una corda (grand)" smoothstep S between s0 and s1: the cubic 3t^2 - 2t^3 of
/// t = clamp((s - s0) / (s1 - s0), 0, 1) (the definition the A5 brief fixes).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): s, s0, s1 in SPEC order
inline double smoothstep(double s, double s0, double s1) {
    const double t = std::clamp((s - s0) / (s1 - s0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// SPEC-HAM table "String that leaves the hammer": w = 1 - S(s) on the leaving string, 1 on the
/// others; leaving < 0 (single string) gives all ones.
inline std::vector<double> strikeWeights(std::size_t count, const pme::UnaCordaParams& una,
                                         double shift) {
    std::vector<double> w(count, 1.0);
    if (una.leavingString >= 0 && static_cast<std::size_t>(una.leavingString) < count) {
        w[static_cast<std::size_t>(una.leavingString)] = 1.0 - smoothstep(shift, una.s0, una.s1);
    }
    return w;
}

/// SPEC-HAM: K_s(s) = K [1 - s (1 - kappa_uc)].
inline double feltStiffness(double k, double shift, const pme::UnaCordaParams& una) {
    return k * (1.0 - shift * (1.0 - una.kappaUC));
}

/// SPEC-HAM: p_s(s) = p + s Delta p_uc.
inline double feltExponent(double p, double shift, const pme::UnaCordaParams& una) {
    return p + shift * una.deltaPUC;
}

/// First contact interval of a penetration series delta^n (sample spacing dt): the interpolated
/// entry and exit crossings of delta = 0. `count` is the number of separate intervals found.
struct ContactInterval {
    bool found{false};
    double entry{0.0};     ///< fractional sample index of the entry crossing
    double exit{0.0};      ///< fractional sample index of the exit crossing
    double duration{0.0};  ///< (exit - entry) * dt, seconds
    int count{0};
};

inline ContactInterval firstContact(std::span<const double> delta, double dt) {
    ContactInterval c;
    bool inside = false;
    for (std::size_t i = 1; i < delta.size(); ++i) {
        if (!inside && delta[i - 1] <= 0.0 && delta[i] > 0.0) {
            const double entry =
                static_cast<double>(i - 1) + (-delta[i - 1]) / (delta[i] - delta[i - 1]);
            ++c.count;
            if (c.count == 1) {
                c.entry = entry;
            }
            inside = true;
        } else if (inside && delta[i - 1] > 0.0 && delta[i] <= 0.0) {
            if (c.count == 1) {
                c.exit = static_cast<double>(i - 1) + delta[i - 1] / (delta[i - 1] - delta[i]);
                c.found = true;
            }
            inside = false;
        }
    }
    c.duration = c.found ? (c.exit - c.entry) * dt : 0.0;
    return c;
}

/// Peak of a sampled force pulse: the vertex of the parabola through the maximum sample and its
/// two neighbours.
inline double parabolicPeak(std::span<const double> f) {
    std::size_t imax = 0;
    for (std::size_t i = 1; i < f.size(); ++i) {
        if (f[i] > f[imax]) {
            imax = i;
        }
    }
    if (imax == 0 || imax + 1 >= f.size()) {
        return f[imax];
    }
    const double a = f[imax - 1];
    const double b = f[imax];
    const double c = f[imax + 1];
    const double denom = a - 2.0 * b + c;
    if (denom >= 0.0) {
        return b;
    }
    return b - (c - a) * (c - a) / (8.0 * denom);
}

/// Richardson extrapolation of a second-order quantity from a ladder of three grids with ratio 2
/// (q0 coarsest, q2 finest): the extrapolated value q2 + (q2 - q1) / 3, the observed order
/// log2((q0 - q1) / (q1 - q2)) and the size of the correction relative to q*, which is the
/// reference's own error estimate.
struct Ladder {
    double value{0.0};
    double observedOrder{0.0};
    double correctionRel{0.0};
};

inline Ladder richardson(double q0, double q1, double q2) {
    Ladder l;
    l.value = q2 + (q2 - q1) / 3.0;
    const double d01 = q0 - q1;
    const double d12 = q1 - q2;
    l.observedOrder =
        (d01 * d12 > 0.0) ? std::log2(d01 / d12) : std::numeric_limits<double>::quiet_NaN();
    l.correctionRel = std::fabs((q2 - q1) / 3.0 / l.value);
    return l;
}

}  // namespace pme::oracle::ham
