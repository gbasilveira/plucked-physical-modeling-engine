// SPDX-License-Identifier: MIT
// Covers: ORC-13
//
// ORC-13 Damper (diagonal) (SPEC-ORC; SPEC-DMP "Damper model", "Diagonal").
//
// Setup: e = 1, single mode. Expected: sigma_n + e R_d phibar^2 / (2 rho A). Tolerance: 0.1 %.
//
// Method (independent of the engine sources and of A6):
//  - Strings (48 kHz, f_max = inf), SPEC-STR constants written out here: treble plain steel
//    (L 0.10 m, d 1 mm, f1 2 kHz), mid plain steel (L 0.65 m, d 1 mm, f1 262 Hz), bass wound
//    (L 1.9 m, d_c 1.6 mm, d_o 4.7 mm, rho_w 8960, eta_w 0.85, eps_w 0.1, f1 27.5 Hz). Losses
//    b1, b2, b3 = 0.4, 1e-3, 1e-11 (plain) and 0.3, 5e-4, 5e-12 (bass). rho A, EI, T, omega_n and
//    sigma_n = b1 + b2 sqrt(omega_n) + b3 omega_n^2 are computed here with the SPEC-STR formulas;
//    the public pme::compileString is used only for the number N of kept modes.
//  - One mode at a time (all other modal states are zero by construction: a one-mode ModeBank):
//    n = 1, a middle n (N/2, moved up to the next mode whose shape value is not nodal) and the
//    highest kept n (N, moved down while the damped mode is rejected by the bandwidth rule or the
//    shape value is nodal; the n actually used is printed).
//  - Damper: diagonal, e = 1, raised-cosine pad (unit area, full width b) at x_d = L/8 and
//    x_d = 0.37 L, widths 10 mm and 30 mm (a combination whose window leaves [0, L] is skipped:
//    the treble string at L/8 with 30 mm). phibar_{d,n} = integral of g(x - x_d) phi_n(x) dx by a
//    64-point Gauss-Legendre rule on 2 panels in float64, checked against 96 points on 3 panels.
//    The pad parameters are chosen per case so that r = e R_d phibar^2 / (2 rho A sigma_n) cycles
//    through 0.5, 3, 10 (R_d = r sigma_n 2 rho A / phibar^2) and e K_d phibar^2 / (rho A omega_n^2)
//    = 0.02 (K_d = 0.02 omega_n^2 rho A / phibar^2). The values are printed for every case.
//  - Expected sigma = sigma_n + e R_d phibar^2 / (2 rho A) from the formula above, and expected
//    omega_{n,d}^2 = omega_n^2 + e K_d phibar^2 / (rho A) (SPEC-DMP "Diagonal").
//  - Coefficients: for one mode, "the coefficients under test" (see A6 SEAM) at e = 1. The mode's
//    damped frequency is omega_d = sqrt(omega_{n,d}^2 - sigma^2) (SPEC-OSC).
//  - Test 1 (exact): the compiled float64 sigmaHat gives sigma = atanh(sigmaHat dt) / dt (SPEC-OSC:
//    sigmaHat dt = tanh(sigma dt)), which must equal the expected sigma within 1e-9 relative.
//  - Test 2 (measured, the SPEC row): the float32 pme::osc::ModeBank, released from q = 1, u = 0,
//    f = 0, runs min(10 s, 6/sigma) (at least 1 s; at least 2 time constants is checked) and the
//    decay rate is fitted with pme::analysis::track_partials (parametric damped-sinusoid fit).
//    Pass: |sigma_fit / sigma - 1| <= 1e-3 for every case; the worst case is printed.
//  - Test 3 (not part of the SPEC row): the fitted damped frequency follows the expected one
//    within 0.01 cent.
//
// A6 SEAM. A6's diagonal-damper per-mode coefficient function did not exist when this test was
// written, so by default `coefficientsUnderTest` is a SPEC-derived reference: omega_tot =
// sqrt(omega_n^2 + e K_d phibar^2 / rho A), sigma_tot = sigma_n + e R_d phibar^2 / (2 rho A), then
// the public pme::compileOscillator. To test A6 itself after merging: (1) write a6_adapter.hpp in
// this directory that includes orc13_input.hpp and defines
//   pme::Status pme::oracle::dmp::a6Coefficients(const ModeInput&, pme::OscCoeffs&)
// by calling A6's function that returns the coefficients of one mode at engagement ModeInput::e
// (A6 documents it in docs/dev/dampers.md); (2) add the A6 target to LIBRARIES in CMakeLists.txt;
// (3) enable the commented PME_ORC13_WIRE_A6 line there. Nothing else changes: expected values
// never come from the seam.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <pme/analysis/partials.hpp>
#include <pme/osc.hpp>
#include <pme/osc/mode_bank.hpp>
#include <pme/status.hpp>
#include <pme/string.hpp>
#include <pme/test/tolerance.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "orc13_input.hpp"
#include "string_oracle_common.hpp"

#ifdef PME_ORC13_WIRE_A6
#include "a6_adapter.hpp"
#endif

namespace {

namespace str = pme::oracle::str;
using pme::oracle::dmp::ModeInput;
using str::kPi;

constexpr double kFs = 48000.0;
constexpr double kDt = 1.0 / kFs;
constexpr double kTolMeasured = 1e-3;     // SPEC-ORC tolerance of ORC-13 (0.1 %)
constexpr double kTolCoefficient = 1e-9;  // exact check of the compiled sigmaHat (brief)
constexpr double kTolCents = 0.01;        // frequency case (brief, not part of the SPEC row)
constexpr double kStiffnessRatio = 0.02;  // e K_d phibar^2 / (rho A omega_n^2)
constexpr double kMinShape = 0.05;        // |phibar| / sqrt(2/L) below this counts as nodal
constexpr std::array<double, 3> kLossRatios{0.5, 3.0, 10.0};  // r = e R_d phibar^2 / (2 rho A sigma_n)

/// SPEC reference of the diagonal damper's per-mode coefficients (default seam).
pme::Status referenceCoefficients(const ModeInput& in, pme::OscCoeffs& out) {
    const double shape2 = in.phiBar * in.phiBar;
    const double omegaTot = std::sqrt(in.omega * in.omega + in.e * in.Kd * shape2 / in.rhoA);
    const double sigmaTot = in.sigma + in.e * in.Rd * shape2 / (2.0 * in.rhoA);
    return pme::compileOscillator(omegaTot, sigmaTot, in.fs, out);
}

/// The seam: the coefficients whose decay ORC-13 judges.
pme::Status coefficientsUnderTest(const ModeInput& in, pme::OscCoeffs& out) {
#ifdef PME_ORC13_WIRE_A6
    return pme::oracle::dmp::a6Coefficients(in, out);
#else
    return referenceCoefficients(in, out);
#endif
}

struct StringCase {
    str::StringSpec spec;
};

std::vector<StringCase> stringCases() {
    return {
        {{"treble plain 2 kHz", 0.10, 1.0e-3, 7850.0, 2.0e11, 1.0e-3, 0.0, 0.0, 0.0, 2000.0, 0.4,
          1.0e-3, 1.0e-11}},
        {{"mid plain 262 Hz", 0.65, 1.0e-3, 7850.0, 2.0e11, 1.0e-3, 0.0, 0.0, 0.0, 262.0, 0.4,
          1.0e-3, 1.0e-11}},
        {{"bass wound 27.5 Hz", 1.9, 1.6e-3, 7850.0, 2.0e11, 4.7e-3, 8960.0, 0.85, 0.1, 27.5, 0.3,
          5.0e-4, 5.0e-12}},
    };
}

/// SPEC-STR "Modes": omega_n = sqrt((T k^2 + EI k^4) / rhoA), k = n pi / L.
double omegaOf(double tension, double ei, double rhoA, double k) {
    return std::sqrt((tension * k * k + ei * k * k * k * k) / rhoA);
}

/// SPEC-STR "Losses": sigma_n = b1 + b2 sqrt(omega_n) + b3 omega_n^2.
double sigmaOf(const str::StringSpec& s, double omega) {
    return s.b1 + s.b2 * std::sqrt(omega) + s.b3 * omega * omega;
}

/// phibar_{d,n} = integral of g(x - xd) phi_n(x) dx over the pad window (float64 quadrature).
double phiBarQuadrature(double length, int n, double xd, double b, int nodes, int panels) {
    const str::GaussLegendre rule = str::gaussLegendre(nodes);
    return str::integrate(
        [&](double x) { return str::raisedCosine(x - xd, b) * str::modeShape(length, n, x); },
        xd - 0.5 * b, xd + 0.5 * b, rule, panels);
}

struct CaseResult {
    std::string label;
    int n{};
    double r{};
    double phiBar{};
    double Kd{};
    double Rd{};
    double sigmaExpected{};
    double sigmaFromCoefficients{};
    double sigmaMeasured{};
    double freqExpectedHz{};
    double freqMeasuredHz{};
    double seconds{};
};

std::string sci(double v) {
    std::ostringstream out;
    out << std::scientific << std::setprecision(3) << v;
    return out.str();
}

/// Releases the mode from q = 1 in the float32 ModeBank and returns q^0 ... q^count.
std::vector<double> simulate(const pme::OscCoeffs& c, std::size_t count) {
    pme::osc::ModeBank bank;
    bank.resize(1);
    bank.setMode(0, c);
    bank.q()[0] = 1.0F;
    const std::array<float, 1> noForce{0.0F};
    std::vector<double> y;
    y.reserve(count + 1U);
    y.push_back(1.0);
    for (std::size_t i = 0; i < count; ++i) {
        bank.step(noForce);
        y.push_back(static_cast<double>(bank.q()[0]));
    }
    return y;
}

/// Builds the case for string `sc`, mode selector `sel` (0: n = 1, 1: middle, 2: highest), pad at
/// `xd` with width `b`. `index` picks r. `measure` runs the float32 simulation and fit.
bool buildCase(const StringCase& sc, int sel, double xd, double b, std::size_t index, bool measure,
               CaseResult& res) {
    const str::StringSpec& s = sc.spec;
    if (xd - 0.5 * b < 0.0 || xd + 0.5 * b > s.L) {
        return false;  // the pad window would leave the string
    }
    const double rhoA = str::linearDensity(s);
    const double ei = str::bendingStiffness(s);
    const double tension = str::tensionForPitch(s, s.f1);
    REQUIRE(tension > 0.0);

    pme::StringModes modes;
    const pme::Status st = pme::compileString(str::toParams(s, s.f1), kFs, str::kInf, {}, modes);
    REQUIRE(str::is(st, pme::Status::Ok));
    const int count = static_cast<int>(modes.omega.size());
    REQUIRE(count >= 3);

    const double r = kLossRatios[index % kLossRatios.size()];
    const int step = (sel == 2) ? -1 : 1;
    int n = (sel == 0) ? 1 : (sel == 1 ? count / 2 : count);
    ModeInput in;
    pme::OscCoeffs coeffs;
    for (; n >= 1 && n <= count; n += step) {
        const double omega = omegaOf(tension, ei, rhoA, static_cast<double>(n) * kPi / s.L);
        const double phi = phiBarQuadrature(s.L, n, xd, b, 64, 2);
        const double phiFine = phiBarQuadrature(s.L, n, xd, b, 96, 3);
        REQUIRE(std::fabs(phi - phiFine) <= 1e-11 * std::sqrt(2.0 / s.L));  // quadrature converged
        if (std::fabs(phi) < kMinShape * std::sqrt(2.0 / s.L)) {
            continue;  // nodal: the pad would not act on this mode
        }
        const double sigma = sigmaOf(s, omega);
        in = {kFs,
              1.0,
              omega,
              sigma,
              rhoA,
              phi,
              kStiffnessRatio * omega * omega * rhoA / (phi * phi),
              r * sigma * 2.0 * rhoA / (phi * phi)};
        if (coefficientsUnderTest(in, coeffs) == pme::Status::Ok) {
            break;
        }
        if (sel != 2) {
            FAIL("damped mode rejected by the bandwidth rule for n = " << n);
        }
    }
    REQUIRE(n >= 1);
    REQUIRE(n <= count);

    // Expected values from the SPEC-DMP formulas (never from the seam).
    const double shape2 = in.phiBar * in.phiBar;
    const double sigmaExp = in.sigma + in.e * in.Rd * shape2 / (2.0 * in.rhoA);
    const double omegaD2 = in.omega * in.omega + in.e * in.Kd * shape2 / in.rhoA;
    const double wd = std::sqrt(omegaD2 - sigmaExp * sigmaExp);

    res = {};
    res.label = s.name + ", n=" + std::to_string(n) + ", x_d=" + std::to_string(xd) +
                " m, b=" + std::to_string(b * 1000.0) + " mm";
    res.n = n;
    res.r = r;
    res.phiBar = in.phiBar;
    res.Kd = in.Kd;
    res.Rd = in.Rd;
    res.sigmaExpected = sigmaExp;
    res.sigmaFromCoefficients = std::atanh(coeffs.sigmaHat * kDt) / kDt;
    res.freqExpectedHz = wd / (2.0 * kPi);

    if (measure) {
        res.seconds = std::clamp(6.0 / sigmaExp, 1.0, 10.0);
        REQUIRE(res.seconds * sigmaExp >= 2.0);  // at least two time constants
        const auto steps = static_cast<std::size_t>(std::llround(res.seconds * kFs));
        const std::vector<double> y = simulate(coeffs, steps);
        pme::analysis::PartialTrackingOptions opt;
        opt.approx_frequencies_hz = {res.freqExpectedHz};
        opt.envelope_frame_s = 0.0;
        const pme::analysis::PartialTrackingResult fit = pme::analysis::track_partials(y, kFs, opt);
        REQUIRE(fit.converged);
        REQUIRE(fit.partials.size() == 1U);
        res.sigmaMeasured = fit.partials[0].sigma_per_s;
        res.freqMeasuredHz = fit.partials[0].frequency_hz;
    }
    return true;
}

/// Calls f(string, sel, xd, b, index) for every case of the sweep, in a fixed order.
template <class F>
void forEachCase(const F& f) {
    std::size_t index = 0;
    for (const StringCase& sc : stringCases()) {
        for (const double xRatio : {0.125, 0.37}) {
            for (const double b : {0.010, 0.030}) {
                for (int sel = 0; sel < 3; ++sel) {
                    f(sc, sel, xRatio * sc.spec.L, b, index);
                    ++index;
                }
            }
        }
    }
}

/// All measured cases (computed once per process).
const std::vector<CaseResult>& measuredCases() {
    static const std::vector<CaseResult> cases = [] {
        std::vector<CaseResult> v;
        forEachCase([&](const StringCase& sc, int sel, double xd, double b, std::size_t index) {
            CaseResult res;
            if (buildCase(sc, sel, xd, b, index, true, res)) {
                v.push_back(res);
            }
        });
        return v;
    }();
    return cases;
}

void reportCase(const char* tag, const CaseResult& c, double error, double limit) {
    std::cerr << tag << " " << c.label << ": r=" << c.r << " R_d=" << sci(c.Rd)
              << " kg/s K_d=" << sci(c.Kd) << " N/m phibar=" << sci(c.phiBar)
              << " sigma_expected=" << sci(c.sigmaExpected) << " /s error=" << sci(error)
              << " (limit " << sci(limit) << ")" << std::endl;  // NOLINT(performance-avoid-endl)
}

}  // namespace

TEST_CASE("ORC-13 diagonal damper compiled coefficients give the SPEC decay rate") {
    int cases = 0;
    double worst = 0.0;
    forEachCase([&](const StringCase& sc, int sel, double xd, double b, std::size_t index) {
        CaseResult res;
        if (!buildCase(sc, sel, xd, b, index, false, res)) {
            return;
        }
        ++cases;
        const double err = pme::test::relative_error(res.sigmaFromCoefficients, res.sigmaExpected);
        worst = std::max(worst, err);
        INFO(res.label);
        PME_CHECK_REL(res.sigmaFromCoefficients, res.sigmaExpected, kTolCoefficient);
    });
    CHECK(cases >= 30);
    std::cerr << "ORC-13 coefficients: " << cases << " cases, worst relative error of "
              << "atanh(sigmaHat dt)/dt vs expected sigma = " << sci(worst) << " (limit "
              << sci(kTolCoefficient) << ")" << std::endl;  // NOLINT(performance-avoid-endl)
}

TEST_CASE("ORC-13 diagonal damper measured decay rate within 0.1 percent") {
    const std::vector<CaseResult>& cases = measuredCases();
    REQUIRE(cases.size() >= 30U);
    double worst = 0.0;
    std::string worstLabel;
    for (const CaseResult& c : cases) {
        const double err = pme::test::relative_error(c.sigmaMeasured, c.sigmaExpected);
        if (err > worst) {
            worst = err;
            worstLabel = c.label;
        }
        reportCase("ORC-13 decay", c, err, kTolMeasured);
        INFO(c.label);
        PME_CHECK_REL(c.sigmaMeasured, c.sigmaExpected, kTolMeasured);
    }
    std::cerr << "ORC-13 worst measured decay error " << sci(worst) << " (limit "
              << sci(kTolMeasured) << ") at " << worstLabel << std::endl;  // NOLINT
}

// Not part of the SPEC row ORC-13: SPEC-DMP's stiffness term omega_{n,d}^2 = omega_n^2 + e K_d
// phibar^2 / (rho A), checked on the fitted damped frequency, 0.01 cent.
TEST_CASE("ORC-13 frequency follows omega squared plus K phibar squared over rhoA") {
    const std::vector<CaseResult>& cases = measuredCases();
    REQUIRE(cases.size() >= 30U);
    double worst = 0.0;
    for (const CaseResult& c : cases) {
        const double cents = std::fabs(pme::test::cents(c.freqMeasuredHz, c.freqExpectedHz));
        worst = std::max(worst, cents);
        INFO(c.label);
        PME_CHECK_CENTS(c.freqMeasuredHz, c.freqExpectedHz, kTolCents);
    }
    std::cerr << "ORC-13 frequency worst error " << sci(worst) << " cent (limit "
              << sci(kTolCents) << ")" << std::endl;  // NOLINT(performance-avoid-endl)
}
