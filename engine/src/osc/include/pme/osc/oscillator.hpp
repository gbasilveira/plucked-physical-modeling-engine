// SPDX-License-Identifier: MIT
// Internal oscillator compilation API (A1-R1, A1-R2). Public entry point: pme/osc.hpp.
//
// SPEC sections implemented: SPEC-OSC "Discrete scheme", "Bandwidth", "Contract"; SPEC-CONV.
#pragma once

#include <cstdint>

#include "pme/status.hpp"

namespace pme::osc {

/// Float64 oscillator coefficients before the float32 rounding of beta, kappa and gamma.
struct OscCoeffsF64 {
    double sigmaHatDt{};    ///< sigmaHat dt = tanh(sigma dt) (SPEC-OSC).
    double omegaHat2Dt2{};  ///< omegaHat^2 dt^2 (SPEC-OSC, SI-0008 evaluation).
    double beta{};          ///< (1 - sigmaHat dt) / (1 + sigmaHat dt).
    double kappa{};         ///< omegaHat^2 dt^2 / (1 + sigmaHat dt).
    double gamma{};         ///< dt^2 / (1 + sigmaHat dt), in s^2.
    double omegaHat2{};     ///< omegaHat^2 in rad^2/s^2.
    double sigmaHat{};      ///< sigmaHat in 1/s.
};

/// Verdict of the SPEC-OSC "Bandwidth" rule.
enum class BandwidthVerdict : std::uint8_t {
    Accepted,         ///< The mode is kept.
    InvalidArgument,  ///< fs <= 0, omega, sigma, fs not finite, sigma < 0, omega < 0 or fMax NaN.
    Overdamped,       ///< sigma >= omega.
    AboveNyquistMargin,  ///< omega_d dt > 0.95 pi.
    AboveFMax,           ///< omega_d / (2 pi) > fMax.
};

/// omega_d dt = dt sqrt((omega - sigma)(omega + sigma)) (SPEC-OSC "Discrete scheme"); the
/// product form avoids cancellation near sigma ~ omega. Requires 0 <= sigma <= omega.
/// Thread: any. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): omega, sigma, fs in SPEC-OSC order
[[nodiscard]] double dampedFrequencyTimesDt(double omega, double sigma, double fs) noexcept;

/// SPEC-OSC "Bandwidth": a mode is dropped if omega_d dt > 0.95 pi, if its frequency
/// omega_d / (2 pi) exceeds fMax, or if sigma >= omega. Checks are made in the order
/// overdamped, Nyquist margin, f_max. `fMax` may be +infinity.
/// Thread: any. Allocates: no.
[[nodiscard]] BandwidthVerdict passesBandwidthRule(double omega, double sigma, double fs,
                                                   double fMax) noexcept;

/// Compile one oscillator in float64 (SPEC-OSC "Discrete scheme", "Difference form").
/// Returns `InvalidArgument` or `Rejected` exactly when `compileOscillator` does; `out` is
/// unchanged unless the result is `Ok`.
/// Thread: non-audio. Allocates: no.
[[nodiscard]] Status compileOscillatorF64(double omega, double sigma, double fs,
                                          OscCoeffsF64& out) noexcept;

}  // namespace pme::osc
