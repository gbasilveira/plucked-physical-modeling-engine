// SPDX-License-Identifier: MIT
// Covers: ORC-01
//
// ORC-01 Oscillator accuracy (SPEC-ORC; SPEC-OSC "Discrete scheme" and "Difference form").
//
// Setup: one mode at 27.5 Hz, 261.6 Hz and 4186 Hz; T60 = 20 s; fs = 48 kHz; float32 difference
// form. Expected: the frequency omega_d / 2 pi and the decay exp(-sigma t) of the continuous mode.
// Tolerance: 0.01 cent in frequency, T60 within 0.5 %.
//
// Method (independent of the engine sources):
//  - sigma = 3 ln(10) / T60 (amplitude down 60 dB after T60), omega = sqrt((2 pi f)^2 + sigma^2) so
//    that omega_d = 2 pi f exactly.
//  - Coefficients come from the public pme::compileOscillator (float32 beta, kappa, gamma).
//  - The state advances in float32 with the SPEC-OSC difference form
//    u^(n+1) = beta u^n - kappa q^n + gamma f^n, q^(n+1) = q^n + u^(n+1), f = 0, written in this
//    file from the SPEC (the engine's ModeBank kernel is the same arithmetic; it is not used here
//    because the A1 internal headers did not exist when this oracle was written).
//  - Initial condition: the exact damped cosine q^n = exp(-sigma n dt) cos(omega_d n dt). The SPEC
//    scheme has the continuous poles exp((-sigma +- i omega_d) dt), so this is an exact solution of
//    the discrete recursion: q^0 = 1 and u^0 = q^0 - q^(-1) = 1 - exp(sigma dt) cos(omega_d dt),
//    evaluated in float64 and rounded to float32.
//  - Run length: 4 s (192 000 steps) for every frequency; this is 110 periods at 27.5 Hz.
//  - q^n is converted to float64 and a damped cosine A exp(-s t) cos(w t + phi) is fitted by
//    Gauss-Newton least squares on growing windows (0.1 s, 0.5 s, 2 s, 4 s), each starting from the
//    previous result. The first window starts from the nominal parameters only to find the basin.
//  - Pass: |1200 log2(w / omega_d)| <= 0.01 cent and |T60_fit / 20 s - 1| <= 0.005, where
//    T60_fit = 3 ln(10) / s. The achieved errors are printed to stderr (SPEC's run: < 0.001 cent).

#include <cmath>
#include <cstddef>
#include <pme/osc.hpp>
#include <pme/test/tolerance.hpp>
#include <string>
#include <vector>

#include "osc_oracle_common.hpp"

namespace {

using pme::oracle::DampedCosineFit;
using pme::oracle::kPi;

constexpr double kFs = 48000.0;
constexpr double kT60 = 20.0;
constexpr double kSimSeconds = 4.0;

void runOrc01(double freqHz) {
    const double dt = 1.0 / kFs;
    const double sigma = pme::oracle::kSixtyDb / kT60;
    const double omegaD = 2.0 * kPi * freqHz;
    const double omega = std::sqrt(omegaD * omegaD + sigma * sigma);

    pme::OscCoeffs coeffs{};
    const bool compiled = pme::compileOscillator(omega, sigma, kFs, coeffs) == pme::Status::Ok;
    REQUIRE(compiled);

    pme::oracle::Oscillator<float> osc;
    osc.beta = coeffs.beta;
    osc.kappa = coeffs.kappa;
    osc.gamma = coeffs.gamma;
    osc.q = 1.0F;
    osc.u = static_cast<float>(1.0 - std::exp(sigma * dt) * std::cos(omegaD * dt));

    const auto steps = static_cast<std::size_t>(kSimSeconds * kFs);
    std::vector<double> y(steps + 1U);
    y[0] = static_cast<double>(osc.q);
    for (std::size_t n = 1; n <= steps; ++n) {
        osc.step(0.0F);
        y[n] = static_cast<double>(osc.q);
    }

    DampedCosineFit fit{1.0, sigma, omegaD, 0.0, 0.0, false};
    for (const double window : {0.1, 0.5, 2.0, 4.0}) {
        const auto count = static_cast<std::size_t>(window * kFs) + 1U;
        fit = pme::oracle::fitDampedCosine(y, count, dt, fit);
        REQUIRE(fit.converged);
    }

    const double centsErr = std::fabs(pme::test::cents(fit.omega, omegaD));
    const double t60Fit = pme::oracle::kSixtyDb / fit.sigma;
    const double t60Err = std::fabs(t60Fit / kT60 - 1.0);
    pme::oracle::report("ORC-01 f=" + std::to_string(freqHz) +
                        " Hz: fitted f=" + std::to_string(fit.omega / (2.0 * kPi)) +
                        " Hz, frequency error=" + pme::oracle::sci(centsErr) +
                        " cent (limit 0.01), T60=" + std::to_string(t60Fit) +
                        " s, T60 error=" + pme::oracle::sci(t60Err) +
                        " (limit 0.005), fit rms=" + pme::oracle::sci(fit.rms));

    PME_CHECK_CENTS(fit.omega / (2.0 * kPi), freqHz, 0.01);
    PME_CHECK_REL(t60Fit, kT60, 0.005);
}

}  // namespace

TEST_CASE("ORC-01 oscillator accuracy at 27.5 Hz") { runOrc01(27.5); }

TEST_CASE("ORC-01 oscillator accuracy at 261.6 Hz") { runOrc01(261.6); }

TEST_CASE("ORC-01 oscillator accuracy at 4186 Hz") { runOrc01(4186.0); }
