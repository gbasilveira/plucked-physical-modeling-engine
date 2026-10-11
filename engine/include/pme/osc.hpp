// SPDX-License-Identifier: MIT
#pragma once

/// @file osc.hpp
/// Modal oscillator coefficients (SPEC-OSC; declared by SPEC-API "Compile functions").

#include "pme/status.hpp"

namespace pme {

/// Compiled coefficients of one discrete modal oscillator in difference form (SPEC-OSC
/// "Difference form" and "Contract"). Immutable once built.
struct OscCoeffs {
    float beta{};        ///< beta = (1 - sigmaHat dt) / (1 + sigmaHat dt), float32 (SPEC-OSC).
    float kappa{};       ///< kappa = omegaHat^2 dt^2 / (1 + sigmaHat dt), float32 (SPEC-OSC).
    float gamma{};       ///< gamma = dt^2 / (1 + sigmaHat dt) in s^2, float32 (SPEC-OSC).
    double omegaHat2{};  ///< omegaHat^2 in rad^2/s^2, float64 for energy monitors (SPEC-OSC).
    double sigmaHat{};   ///< sigmaHat = tanh(sigma dt) / dt in 1/s, float64 (SPEC-OSC).
};

/// Compile one oscillator (SPEC-OSC "Contract", SPEC-API).
///
/// @param omega Angular frequency omega in rad/s.
/// @param sigma Amplitude decay rate sigma in 1/s.
/// @param fs    Sample rate in Hz.
/// @param out   Receives beta, kappa, gamma, omegaHat^2, sigmaHat on success.
/// Thread: non-audio. Allocates: no. Failures: `Rejected` if the bandwidth rule rejects the mode
/// (omega_d dt > 0.95 pi, or sigma >= omega); `InvalidArgument` if fs <= 0; `NotImplemented` in
/// the P0.4 stub.
[[nodiscard]] Status compileOscillator(double omega, double sigma, double fs, OscCoeffs& out);

}  // namespace pme
