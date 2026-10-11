// SPDX-License-Identifier: MIT
// Covers: ORC-05
//
// ORC-05 Window shape (SPEC-ORC; SPEC-STR "Forces and observations", "Contract").
//
// Setup: raised-cosine W(k) vs quadrature, n up to 60. Expected: the same value. Tolerance: 10^-9
// relative.
//
// Method (independent of the engine sources):
//  - phibar_n = integral of g(x) phi_n(x) dx over [x0 - b/2, x0 + b/2] in float64, with
//    g(s) = (1/b)(1 + cos(2 pi s / b)) (unit area, full width b) and phi_n = sqrt(2/L) sin(k_n x),
//    k_n = n pi / L, by a 64-point Gauss-Legendre rule on 2 panels (string_oracle_common.hpp). The
//    integrand is analytic on each panel, so this is exact to round-off; the test checks it against
//    a 96-point, 3-panel rule (agreement 1e-13 of max |phibar|) before trusting it.
//  - Cases (L, b, x0): (1 m, 10 mm, 0.12 m); (1 m, 50 mm, 0.3 m), where u = k_n b / 2 = pi exactly
//    at n = 40, the removable singularity of W; (0.05 m, 5 mm, 0.006 m), where u = pi at n = 20 and
//    the raised cosine W has a zero at n = 40 (u = 2 pi). All windows stay inside [0, L].
//  - Float64 check: phi_n(x0) * windowFactor(RaisedCosine, k_n, b) vs the quadrature, n = 1..60.
//    Pass: 1e-9 relative. Where |phibar_n| < 1e-6 max_n |phibar_n| (next to a zero of sin(k_n x0)
//    or of W, where the quadrature itself only has absolute round-off accuracy) the error is taken
//    relative to max_n |phibar_n| instead, with the same 1e-9.
//  - Float32 check: windowedShapes(modes, x0, RaisedCosine, b, out) vs round32(quadrature):
//    |stored - round32(quad)| <= 1 ulp of round32(quad), plus 1e-12 max_n |phibar_n| of absolute
//    slack, for the entries next to a zero of sin(k_n x0) or of W, where both the quadrature and
//    the float64 evaluation of sin(n pi x0 / L) only have round-off-floor absolute accuracy (far
//    below float32 resolution of the peak, 6e-8).
//  - Direct W(k) check: W is also compared with its own quadrature, the integral of g(s) cos(k s)
//    over s (g is even so the sin(k x0) factor separates), at u = k b / 2 = 1e-10, 1e-4, 0.5, 1, 2,
//    pi -+ 1e-4, pi -+ 1e-8, pi, 4, 6, 10 and, for the rectangular window sin(u)/u, away from its
//    zeros. This probes the u -> 0 and u -> pi series branches of windowFactor.
//  - Point: windowFactor(Point, ...) == 1 and compileString's phiBar at x0 equals round32 of
//    phi_n(x0) within 1 ulp (+ 1e-12 max_n |phi_n| for entries at the round-off floor).
// The worst error and where it occurs are printed to stderr.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <pme/string.hpp>
#include <pme/test/tolerance.hpp>
#include <span>
#include <string>
#include <vector>

#include "string_internal.hpp"
#include "string_oracle_common.hpp"

namespace {

namespace str = pme::oracle::str;
using str::kPi;

constexpr double kTol = 1e-9;  // SPEC-ORC tolerance of ORC-05, relative
constexpr int kModes = 60;
constexpr double kFs = 48000.0;

struct WindowCase {
    std::string name;
    double L;
    double b;
    double x0;
    // String used to obtain a StringModes with at least 60 kept modes.
    double dCore;
    double f1;
    double ECore;  // Young's modulus (Pa); soft for the short string so that 60 modes stay in band
};

std::vector<WindowCase> windowCases() {
    return {
        {"L 1 m b 10 mm x0 0.12 m", 1.0, 0.010, 0.12, 0.2e-3, 100.0, 2.0e11},
        {"L 1 m b 50 mm x0 0.3 m (u = pi at n = 40)", 1.0, 0.050, 0.3, 0.2e-3, 100.0, 2.0e11},
        {"L 0.05 m b 5 mm x0 6 mm", 0.05, 0.005, 0.006, 0.03e-3, 300.0, 1.0e9},
    };
}

/// Quadrature phibar_n for n = 1..kModes (float64), SPEC-STR "Forces and observations".
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<double> quadratureShapes(const WindowCase& c, int nodes, int panels) {
    const str::GaussLegendre rule = str::gaussLegendre(nodes);
    std::vector<double> out;
    for (int n = 1; n <= kModes; ++n) {
        const double k = static_cast<double>(n) * kPi / c.L;
        const double sq = std::sqrt(2.0 / c.L);
        out.push_back(str::integrate(
            [&](double x) { return str::raisedCosine(x - c.x0, c.b) * sq * std::sin(k * x); },
            c.x0 - 0.5 * c.b, c.x0 + 0.5 * c.b, rule, panels));
    }
    return out;
}

double maxAbs(const std::vector<double>& v) {
    double m = 0.0;
    for (const double x : v) {
        m = std::max(m, std::fabs(x));
    }
    return m;
}

float ulpOf(float x) {
    const float a = std::fabs(x);
    return std::nextafter(a, std::numeric_limits<float>::infinity()) - a;
}

/// Compiles a plain steel string of the case's length with at least kModes kept modes, with one
/// registered point at x0.
pme::StringModes compileCase(const WindowCase& c) {
    str::StringSpec spec;
    spec.name = c.name;
    spec.L = c.L;
    spec.dCore = c.dCore;
    spec.dOuter = c.dCore;
    spec.ECore = c.ECore;
    spec.f1 = c.f1;
    const std::array<double, 1> x0s{c.x0};
    pme::StringModes modes;
    const pme::Status st = pme::compileString(str::toParams(spec, spec.f1), kFs, str::kInf,
                                              std::span<const double>(x0s), modes);
    REQUIRE(pme::oracle::str::is(st, pme::Status::Ok));
    REQUIRE(static_cast<int>(modes.omega.size()) >= kModes);
    return modes;
}

/// Quadrature of W(k) = integral of g(s) cos(k s) ds, g the raised cosine (or 1/b for the
/// rectangular window) of full width b.
double windowQuadrature(bool raised, double k, double b) {
    static const str::GaussLegendre rule = str::gaussLegendre(64);
    return str::integrate(
        [&](double s) { return (raised ? str::raisedCosine(s, b) : 1.0 / b) * std::cos(k * s); },
        -0.5 * b, 0.5 * b, rule, 2);
}

}  // namespace

TEST_CASE("ORC-05 window shape raised cosine vs quadrature in float64") {
    for (const WindowCase& c : windowCases()) {
        const std::vector<double> coarse = quadratureShapes(c, 64, 2);
        const std::vector<double> fine = quadratureShapes(c, 96, 3);
        const double peak = maxAbs(fine);
        REQUIRE(peak > 0.0);
        // The quadrature must be converged before it is used as the oracle.
        for (std::size_t i = 0; i < fine.size(); ++i) {
            REQUIRE(std::fabs(coarse[i] - fine[i]) <= 1e-13 * peak);
        }
        double worst = 0.0;
        int worstN = 0;
        for (int n = 1; n <= kModes; ++n) {
            const double k = static_cast<double>(n) * kPi / c.L;
            const double ref = fine[static_cast<std::size_t>(n - 1)];
            const double actual =
                str::modeShape(c.L, n, c.x0) *
                pme::string::windowFactor(pme::string::WindowKind::RaisedCosine, k, c.b);
            // Relative to |ref|, or to the peak where |ref| < 1e-6 peak (round-off floor).
            const double denom = std::fabs(ref) >= 1e-6 * peak ? std::fabs(ref) : peak;
            const double err = std::fabs(actual - ref) / denom;
            INFO(c.name << ": n = " << n << " u = " << (k * c.b / 2.0) << " engine = " << actual
                        << " quadrature = " << ref << " err = " << err);
            CHECK(err <= kTol);
            if (err > worst) {
                worst = err;
                worstN = n;
            }
        }
        str::report(
            "ORC-05 float64 " + c.name + ": worst error = " + str::sci(worst) +
            " (rel. to |phibar| or to the peak below 1e-6 peak) at n = " + std::to_string(worstN));
    }
}

TEST_CASE("ORC-05 window shape stored in float32 by windowedShapes") {
    for (const WindowCase& c : windowCases()) {
        const std::vector<double> ref = quadratureShapes(c, 96, 3);
        const double peak = maxAbs(ref);
        const pme::StringModes modes = compileCase(c);
        std::vector<float> out(modes.omega.size(), 0.0F);
        pme::string::windowedShapes(modes, c.x0, pme::string::WindowKind::RaisedCosine, c.b,
                                    std::span<float>(out));
        double worstUlps = 0.0;
        int worstN = 0;
        for (int n = 1; n <= kModes; ++n) {
            const auto i = static_cast<std::size_t>(n - 1);
            const auto ref32 = static_cast<float>(ref[i]);
            const double diff = std::fabs(static_cast<double>(out[i]) - static_cast<double>(ref32));
            const auto ulp = static_cast<double>(ulpOf(ref32));
            const double slack = 1e-12 * peak;
            INFO(c.name << ": n = " << n << " stored = " << out[i] << " round32(quad) = " << ref32
                        << " diff = " << diff << " ulp = " << ulp);
            CHECK(diff <= ulp + slack);
            if (std::fabs(ref[i]) >= 1e-6 * peak && diff / ulp > worstUlps) {
                worstUlps = diff / ulp;
                worstN = n;
            }
        }
        str::report("ORC-05 float32 " + c.name + ": worst difference = " + str::sci(worstUlps) +
                    " ulp (entries >= 1e-6 peak) at n = " + std::to_string(worstN));
    }
}

TEST_CASE("ORC-05 window factor limits and removable singularities") {
    constexpr double b = 0.01;
    // u = k b / 2 -> k = 2 u / b. Values around 0 and around pi (W(pi) = 1/2 for the raised
    // cosine).
    const std::vector<double> us{1e-10,      1e-6,       1e-4, 0.5,        1.0,        2.0,
                                 kPi - 1e-4, kPi - 1e-8, kPi,  kPi + 1e-8, kPi + 1e-4, 4.0,
                                 5.0,        6.0,        7.0,  10.0,       15.0};
    double worstRaised = 0.0;
    double worstU = 0.0;
    for (const double u : us) {
        const double k = 2.0 * u / b;
        const double ref = windowQuadrature(true, k, b);
        const double actual =
            pme::string::windowFactor(pme::string::WindowKind::RaisedCosine, k, b);
        const double err = std::fabs(actual - ref) / std::max(std::fabs(ref), 1e-6);
        INFO("raised cosine u = " << u << " engine = " << actual << " quadrature = " << ref
                                  << " err = " << err);
        CHECK(err <= kTol);
        if (err > worstRaised) {
            worstRaised = err;
            worstU = u;
        }
    }
    // The raised cosine at u = pi is 1/2 exactly (limit of sin(u)/u * pi^2/(pi^2 - u^2)).
    PME_CHECK_REL(
        pme::string::windowFactor(pme::string::WindowKind::RaisedCosine, 2.0 * kPi / b, b), 0.5,
        kTol);
    PME_CHECK_REL(pme::string::windowFactor(pme::string::WindowKind::RaisedCosine, 1e-12, b), 1.0,
                  kTol);

    // Rectangular window sin(u)/u, away from its zeros at multiples of pi.
    const std::vector<double> rectU{1e-10, 1e-4, 0.5, 1.0, 2.0, 4.0, 5.0, 7.0, 10.0};
    double worstRect = 0.0;
    for (const double u : rectU) {
        const double k = 2.0 * u / b;
        const double ref = windowQuadrature(false, k, b);
        const double actual = pme::string::windowFactor(pme::string::WindowKind::Rectangular, k, b);
        const double err = std::fabs(actual - ref) / std::max(std::fabs(ref), 1e-6);
        INFO("rectangular u = " << u << " engine = " << actual << " quadrature = " << ref
                                << " err = " << err);
        CHECK(err <= kTol);
        worstRect = std::max(worstRect, err);
    }
    // Point: W = 1 whatever k and b.
    CHECK(pme::string::windowFactor(pme::string::WindowKind::Point, 123.0, b) == 1.0);
    CHECK(pme::string::windowFactor(pme::string::WindowKind::Point, 0.0, 0.0) == 1.0);
    str::report("ORC-05 W(k): worst raised-cosine error = " + str::sci(worstRaised) + " at u = " +
                str::sci(worstU) + ", worst rectangular error = " + str::sci(worstRect));
}

TEST_CASE("ORC-05 point shapes phiBar at the registered point") {
    for (const WindowCase& c : windowCases()) {
        const pme::StringModes modes = compileCase(c);
        REQUIRE(modes.phiBar.size() == 1);
        REQUIRE(modes.phiBar[0].size() == modes.omega.size());
        double peak = 0.0;
        for (int n = 1; n <= kModes; ++n) {
            peak = std::max(peak, std::fabs(str::modeShape(c.L, n, c.x0)));
        }
        std::vector<float> point(modes.omega.size(), 0.0F);
        pme::string::windowedShapes(modes, c.x0, pme::string::WindowKind::Point, 0.0,
                                    std::span<float>(point));
        for (int n = 1; n <= kModes; ++n) {
            const auto i = static_cast<std::size_t>(n - 1);
            const auto ref32 = static_cast<float>(str::modeShape(c.L, n, c.x0));
            const double slack = 1e-12 * peak;
            INFO(c.name << ": n = " << n << " phiBar = " << modes.phiBar[0][i]
                        << " round32(phi) = " << ref32);
            CHECK(std::fabs(static_cast<double>(modes.phiBar[0][i]) - static_cast<double>(ref32)) <=
                  static_cast<double>(ulpOf(ref32)) + slack);
            CHECK(std::fabs(static_cast<double>(point[i]) - static_cast<double>(ref32)) <=
                  static_cast<double>(ulpOf(ref32)) + slack);
        }
    }
}
