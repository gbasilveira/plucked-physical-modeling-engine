// SPDX-License-Identifier: MIT
// Covers: A1-T3
// A1-T3: a 1% frequency ramp over one sub-block tracks the analytic chirp phase.
#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/osc/ramp.hpp"

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

// Least-squares fit of q[k] ~ exp(-sigma t_k) (a cos(w t_k) + b sin(w t_k)) with t_k = (k - k0) dt,
// k over [first, last]. Returns the phase phi of A exp(-sigma t) cos(w t + phi) at t = 0 (k = k0).
double fitPhase(const std::vector<double>& q, int first, int last, int k0, double w, double sigma,
                double dt) {
    double saa = 0.0;
    double sab = 0.0;
    double sbb = 0.0;
    double sqa = 0.0;
    double sqb = 0.0;
    for (int k = first; k <= last; ++k) {
        const double t = static_cast<double>(k - k0) * dt;
        const double env = std::exp(-sigma * t);
        const double ca = env * std::cos(w * t);
        const double cb = env * std::sin(w * t);
        saa += ca * ca;
        sab += ca * cb;
        sbb += cb * cb;
        sqa += ca * q[static_cast<std::size_t>(k)];
        sqb += cb * q[static_cast<std::size_t>(k)];
    }
    const double det = saa * sbb - sab * sab;
    const double a = (sqa * sbb - sqb * sab) / det;
    const double b = (sqb * saa - sqa * sab) / det;
    return std::atan2(-b, a);
}

double wrap(double x) { return std::remainder(x, 2.0 * kPi); }

struct ChirpResult {
    double phaseAdvance;
    double errorRamped;
    double errorStep;
};

// One mode at f Hz, ramp to 1.01 f over one sub-block of S samples.
ChirpResult measure(double freqHz, int subBlock, bool ramp) {
    const double fs = 48000.0;
    const double dt = 1.0 / fs;
    const double sigma = 0.5;
    const double omega0 = 2.0 * kPi * freqHz;
    const double omega1 = 1.01 * omega0;

    pme::OscCoeffs c0;
    pme::OscCoeffs c1;
    REQUIRE(pme::compileOscillator(omega0, sigma, fs, c0) == pme::Status::Ok);
    REQUIRE(pme::compileOscillator(omega1, sigma, fs, c1) == pme::Status::Ok);
    pme::osc::OscCoeffsF64 d0;
    pme::osc::OscCoeffsF64 d1;
    REQUIRE(pme::osc::compileOscillatorF64(omega0, sigma, fs, d0) == pme::Status::Ok);
    REQUIRE(pme::osc::compileOscillatorF64(omega1, sigma, fs, d1) == pme::Status::Ok);
    const double wd0 = pme::osc::dampedFrequencyTimesDt(omega0, sigma, fs) / dt;
    const double wd1 = pme::osc::dampedFrequencyTimesDt(omega1, sigma, fs) / dt;

    const int pre = 6000;   // free oscillation before the ramp ("until steady")
    const int window = 3000;
    const int total = pre + subBlock + window;
    std::vector<double> q;
    q.reserve(static_cast<std::size_t>(total) + 1);
    double qs = 1.0;
    double us = 0.01;  // arbitrary initial velocity (difference-form u)
    q.push_back(qs);
    const double force = 0.0;
    const pme::osc::CoeffRamp plan =
        pme::osc::planRamp(c0, omega0, sigma, c1, omega1, sigma, subBlock);
    REQUIRE(plan.ramped);
    // State index n = pre is the ramp start.
    for (int n = 0; n < total; ++n) {
        float b = c0.beta;
        float k = c0.kappa;
        float g = c0.gamma;
        if (n >= pre && n < pre + subBlock) {
            if (ramp) {
                pme::osc::coefficientsAt(plan, n - pre, b, k, g);
            } else {
                b = c1.beta;
                k = c1.kappa;
                g = c1.gamma;
            }
        } else if (n >= pre + subBlock) {
            b = c1.beta;
            k = c1.kappa;
            g = c1.gamma;
        }
        pme::osc::stepModes<double, float>(&qs, &us, &b, &k, &g, &force, 1);
        q.push_back(qs);
    }

    // Phase of the free oscillation before the ramp, referred to the ramp start; and after the
    // ramp, referred to the ramp end.
    const double phi0 = fitPhase(q, pre - window, pre, pre, wd0, sigma, dt);
    const double phiEnd = fitPhase(q, pre + subBlock, pre + subBlock + window, pre + subBlock, wd1,
                                   sigma, dt);
    // Analytic linear chirp: f(t) from f to 1.01 f over S dt, phase = phi0 + 2 pi int f dt.
    const double advance = 0.5 * (wd0 + wd1) * static_cast<double>(subBlock) * dt;
    const double expected = phi0 + advance;
    ChirpResult r{};
    r.phaseAdvance = advance;
    (ramp ? r.errorRamped : r.errorStep) = std::fabs(wrap(phiEnd - expected)) / advance;
    return r;
}

}  // namespace

TEST_CASE("A1-T3 a 1 percent frequency ramp over one sub-block tracks the analytic chirp phase") {
    for (const double freq : {1000.0, 4000.0}) {
        for (const int subBlock : {16, 32}) {
            CAPTURE(freq);
            CAPTURE(subBlock);
            const ChirpResult ramped = measure(freq, subBlock, true);
            const ChirpResult stepped = measure(freq, subBlock, false);
            MESSAGE("f=" << freq << " Hz S=" << subBlock << ": phase advance "
                         << ramped.phaseAdvance << " rad; ramped relative phase error "
                         << ramped.errorRamped << "; step change (no ramp) relative phase error "
                         << stepped.errorStep);
            CHECK(ramped.errorRamped <= 1e-3);
        }
    }
}
