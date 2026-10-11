// SPDX-License-Identifier: MIT
// compileOscillator (A1-R1) and the bandwidth rule (A1-R2).
//
// SPEC sections implemented: SPEC-OSC "Discrete scheme", "Bandwidth", "Contract"; SPEC-CONV.
#include "pme/osc/oscillator.hpp"

#include <cmath>
#include <limits>
#include <numbers>

#include "pme/osc.hpp"

namespace pme::osc {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kNyquistMargin = 0.95 * kPi;  // SPEC-OSC "Bandwidth": omega_d dt <= 0.95 pi.

bool argumentsValid(double omega, double sigma, double fs) noexcept {
    return std::isfinite(omega) && std::isfinite(sigma) && std::isfinite(fs) && fs > 0.0 &&
           sigma >= 0.0 && omega >= 0.0;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): omega, sigma, fs in SPEC-OSC order
double dampedFrequencyTimesDt(double omega, double sigma, double fs) noexcept {
    // SPEC-OSC discrete scheme: omega_d dt = dt sqrt((omega - sigma)(omega + sigma)).
    const double dt = 1.0 / fs;
    return dt * std::sqrt((omega - sigma) * (omega + sigma));
}

BandwidthVerdict passesBandwidthRule(double omega, double sigma, double fs, double fMax) noexcept {
    // SPEC-OSC bandwidth.
    if (!argumentsValid(omega, sigma, fs) || std::isnan(fMax)) {
        return BandwidthVerdict::InvalidArgument;
    }
    if (sigma >= omega) {
        return BandwidthVerdict::Overdamped;
    }
    const double omegaDDt = dampedFrequencyTimesDt(omega, sigma, fs);
    if (omegaDDt > kNyquistMargin) {
        return BandwidthVerdict::AboveNyquistMargin;
    }
    const double frequencyHz = (omegaDDt * fs) / (2.0 * kPi);
    if (frequencyHz > fMax) {
        return BandwidthVerdict::AboveFMax;
    }
    return BandwidthVerdict::Accepted;
}

Status compileOscillatorF64(double omega, double sigma, double fs, OscCoeffsF64& out) noexcept {
    const BandwidthVerdict verdict =
        passesBandwidthRule(omega, sigma, fs, std::numeric_limits<double>::infinity());
    switch (verdict) {
        case BandwidthVerdict::Accepted:
            break;
        case BandwidthVerdict::InvalidArgument:
            return Status::InvalidArgument;
        case BandwidthVerdict::Overdamped:
        case BandwidthVerdict::AboveNyquistMargin:
        case BandwidthVerdict::AboveFMax:
            return Status::Rejected;
    }

    const double dt = 1.0 / fs;
    const double x = sigma * dt;
    const double y = dampedFrequencyTimesDt(omega, sigma, fs);

    // SPEC-OSC discrete scheme; evaluation per SI-0008:
    // sigmaHat dt = tanh(sigma dt),
    // omegaHat^2 dt^2 = 4 [sinh^2(sigma dt / 2) + sin^2(omega_d dt / 2)] / cosh(sigma dt).
    const double sigmaHatDt = std::tanh(x);
    const double sinhHalfX = std::sinh(0.5 * x);
    const double sinHalfY = std::sin(0.5 * y);
    const double omegaHat2Dt2 = 4.0 * (sinhHalfX * sinhHalfX + sinHalfY * sinHalfY) / std::cosh(x);

    // SPEC-OSC difference form.
    const double denominator = 1.0 + sigmaHatDt;
    out.sigmaHatDt = sigmaHatDt;
    out.omegaHat2Dt2 = omegaHat2Dt2;
    out.beta = (1.0 - sigmaHatDt) / denominator;
    out.kappa = omegaHat2Dt2 / denominator;
    out.gamma = (dt * dt) / denominator;
    out.omegaHat2 = omegaHat2Dt2 * fs * fs;
    out.sigmaHat = sigmaHatDt * fs;
    return Status::Ok;
}

}  // namespace pme::osc

namespace pme {

Status compileOscillator(double omega, double sigma, double fs, OscCoeffs& out) {
    // SPEC-OSC contract: beta, kappa, gamma stored float32 (round to nearest); omegaHat^2 and
    // sigmaHat kept float64.
    osc::OscCoeffsF64 c;
    const Status status = osc::compileOscillatorF64(omega, sigma, fs, c);
    if (status != Status::Ok) {
        return status;
    }
    out.beta = static_cast<float>(c.beta);
    out.kappa = static_cast<float>(c.kappa);
    out.gamma = static_cast<float>(c.gamma);
    out.omegaHat2 = c.omegaHat2;
    out.sigmaHat = c.sigmaHat;
    return Status::Ok;
}

}  // namespace pme
