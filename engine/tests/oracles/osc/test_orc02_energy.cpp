// SPDX-License-Identifier: MIT
// Covers: ORC-02
//
// ORC-02 Oscillator energy (SPEC-ORC; SPEC-OSC "Discrete energy").
//
// Setup: lossless mode (sigma = 0), f = 0, 10 s at 48 kHz (480 000 steps).
// Expected: H^(n+1/2) = 1/2 (u^(n+1)/dt)^2 + 1/2 omegaHat^2 q^n q^(n+1) is constant.
// Tolerance: 1e-12 relative in float64, 1e-5 relative in float32 (max over n of
// |H^(n+1/2) - H^(1/2)| / |H^(1/2)|). The tolerances are stated in this file; the golden-feature
// default of pme_analysis (1e-9 absolute) is not used.
//
// Method (independent of the engine sources): coefficients from the SPEC-OSC formulas (SI-0008
// form) in float64, or from the public pme::compileOscillator for the float32 case; the difference
// form u^(n+1) = beta u^n - kappa q^n + gamma f^n, q^(n+1) = q^n + u^(n+1) is written here. Energy
// is always accumulated in float64. Frequencies: 27.5 Hz, 4186 Hz and 21 600 Hz (omega_d dt = 0.9
// pi, near the 0.95 pi bandwidth limit). Initial state: q^0 = 1, u^0 = 0.5 sqrt(kappa) (about half
// the peak velocity).
//
// The balance test checks H^(n+1/2) - H^(n-1/2) = dt [-2 sigmaHat (delta_t. q^n)^2 + f^n delta_t.
// q^n] for a damped, forced mode in float64, 1e-12 relative to H^(n+1/2).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <pme/osc.hpp>
#include <pme/test/tolerance.hpp>
#include <string>

#include "osc_oracle_common.hpp"

namespace {

using pme::oracle::Coeffs64;
using pme::oracle::kPi;
using pme::oracle::Oscillator;

constexpr double kFs = 48000.0;
constexpr double kDt = 1.0 / kFs;
constexpr std::size_t kSteps = 480000;  // 10 s
constexpr double kTolF64 = 1e-12;
constexpr double kTolF32 = 1e-5;

struct FreqCase {
    double hz;
    const char* label;
};

constexpr std::array<FreqCase, 3> kFreqs{{
    {27.5, "27.5 Hz"},
    {4186.0, "4186 Hz"},
    {0.45 * kFs, "21600 Hz (omega_d dt = 0.9 pi)"},
}};

/// Maximum over n of |H^(n+1/2) - H^(1/2)| / |H^(1/2)| for the float64 difference form with the
/// given float64 coefficients. `useKernel` selects the templated Oscillator or an independent
/// five-line recursion written inline.
double maxDriftF64(const Coeffs64& c, bool useKernel) {
    double q = 1.0;
    double u = 0.5 * std::sqrt(c.kappa);
    Oscillator<double> osc{c.beta, c.kappa, c.gamma, q, u};
    double h0 = 0.0;
    double worst = 0.0;
    for (std::size_t n = 0; n < kSteps; ++n) {
        const double qNow = useKernel ? osc.q : q;
        double uNext = 0.0;
        double qNext = 0.0;
        if (useKernel) {
            osc.step(0.0);
            uNext = osc.u;
            qNext = osc.q;
        } else {
            uNext = c.beta * u - c.kappa * q + c.gamma * 0.0;
            qNext = q + uNext;
            u = uNext;
            q = qNext;
        }
        const double h = pme::oracle::halfStepEnergy(uNext, qNow, qNext, c.omegaHat2, kDt);
        if (n == 0) {
            h0 = h;
        }
        worst = std::max(worst, std::fabs(h - h0) / std::fabs(h0));
    }
    return worst;
}

}  // namespace

TEST_CASE("ORC-02 energy is constant in float64") {
    for (const FreqCase& fc : kFreqs) {
        INFO("mode: " << fc.label);
        const double omega = 2.0 * kPi * fc.hz;
        const Coeffs64 c = pme::oracle::coefficients64(omega, 0.0, kFs);
        // The lossless scheme has beta = 1 exactly and kappa = omegaHat^2 dt^2 (SPEC-OSC).
        CHECK(c.beta == 1.0);
        CHECK(c.kappa == doctest::Approx(c.omegaHat2 * kDt * kDt).epsilon(1e-15));
        const double driftInline = maxDriftF64(c, false);
        const double driftKernel = maxDriftF64(c, true);
        pme::oracle::report(std::string("ORC-02 float64 ") + fc.label +
                            ": max relative drift inline=" + pme::oracle::sci(driftInline) +
                            " kernel=" + pme::oracle::sci(driftKernel));
        CHECK(driftInline <= kTolF64);
        CHECK(driftKernel <= kTolF64);
    }
}

namespace {

void runFloat32(const FreqCase& fc) {
    INFO("mode: " << fc.label);
    const double omega = 2.0 * kPi * fc.hz;
    pme::OscCoeffs oc{};
    const bool compiled = pme::compileOscillator(omega, 0.0, kFs, oc) == pme::Status::Ok;
    REQUIRE(compiled);
    // Cross-check of the float64 omegaHat^2 the monitor uses against the SPEC formula.
    const Coeffs64 ref = pme::oracle::coefficients64(omega, 0.0, kFs);
    PME_CHECK_REL(oc.omegaHat2, ref.omegaHat2, 1e-13);

    Oscillator<float> osc;
    osc.beta = oc.beta;
    osc.kappa = oc.kappa;
    osc.gamma = oc.gamma;
    osc.q = 1.0F;
    osc.u = static_cast<float>(0.5 * std::sqrt(ref.kappa));

    double h0 = 0.0;
    double worst = 0.0;
    for (std::size_t n = 0; n < kSteps; ++n) {
        const auto qNow = static_cast<double>(osc.q);
        osc.step(0.0F);
        const double h = pme::oracle::halfStepEnergy(static_cast<double>(osc.u), qNow,
                                                     static_cast<double>(osc.q), oc.omegaHat2, kDt);
        if (n == 0) {
            h0 = h;
        }
        worst = std::max(worst, std::fabs(h - h0) / std::fabs(h0));
    }
    pme::oracle::report(std::string("ORC-02 float32 ") + fc.label +
                        ": max relative drift=" + pme::oracle::sci(worst) + " (limit 1e-05)");
    CHECK(worst <= kTolF32);
}

}  // namespace

TEST_CASE("ORC-02 energy is constant in float32 at 27.5 Hz") { runFloat32(kFreqs[0]); }

TEST_CASE("ORC-02 energy is constant in float32 at 4186 Hz") { runFloat32(kFreqs[1]); }

TEST_CASE("ORC-02 energy is constant in float32 at 21600 Hz") { runFloat32(kFreqs[2]); }

TEST_CASE("ORC-02 energy balance with loss and forcing") {
    // Damped (T60 = 2 s), forced mode at 440 Hz, float64, 1 s.
    const double sigma = pme::oracle::kSixtyDb / 2.0;
    const double omegaD = 2.0 * kPi * 440.0;
    const double omega = std::sqrt(omegaD * omegaD + sigma * sigma);
    const Coeffs64 c = pme::oracle::coefficients64(omega, sigma, kFs);

    Oscillator<double> osc{c.beta, c.kappa, c.gamma, 1.0, 0.0};
    double worst = 0.0;
    for (std::size_t n = 0; n < 48000; ++n) {
        const double t = static_cast<double>(n) * kDt;
        const double f =
            40000.0 * std::sin(2.0 * kPi * 400.0 * t) + 15000.0 * std::cos(2.0 * kPi * 97.0 * t);
        const double qNow = osc.q;
        const double uNow = osc.u;
        const double qPrev = qNow - uNow;  // q^(n-1) from u^n = q^n - q^(n-1)
        osc.step(f);
        const double uNext = osc.u;
        const double qNext = osc.q;
        const double hPlus = pme::oracle::halfStepEnergy(uNext, qNow, qNext, c.omegaHat2, kDt);
        const double hMinus = pme::oracle::halfStepEnergy(uNow, qPrev, qNow, c.omegaHat2, kDt);
        const double centredVelocity = (uNext + uNow) / (2.0 * kDt);  // delta_t. q^n
        const double balance =
            kDt * (-2.0 * c.sigmaHat * centredVelocity * centredVelocity + f * centredVelocity);
        const double residual = std::fabs((hPlus - hMinus) - balance) / std::fabs(hPlus);
        worst = std::max(worst, residual);
    }
    pme::oracle::report("ORC-02 balance: max relative residual=" + pme::oracle::sci(worst) +
                        " (limit 1e-12)");
    CHECK(worst <= kTolF64);
}
