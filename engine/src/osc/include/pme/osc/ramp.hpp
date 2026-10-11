// SPDX-License-Identifier: MIT
// Coefficient ramp utility (A1-R6): the 0.1%-per-sub-block rule of SPEC-OSC "Time-varying
// parameters". Real-time safe: every function is noexcept and allocation-free.
//
// SPEC sections implemented: SPEC-OSC "Time-varying parameters"; SPEC-CONV (sub-block S = 16 or
// 32).
#pragma once

#include <cstddef>
#include <span>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"

namespace pme::osc {

/// Relative change per sub-block above which coefficients are ramped (SPEC-OSC "Time-varying
/// parameters": 0.1%).
inline constexpr double kRampThreshold = 1.0e-3;

/// A change is "larger than 0.1%" when |d omega| > 1e-3 omega or |d sigma| > 1e-3 sigma, with
/// omega and sigma the values in force (SPEC-OSC "Time-varying parameters"). Any change of sigma
/// from 0 therefore ramps. Thread: any. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): values in force, then target
[[nodiscard]] bool exceedsRampThreshold(double omegaInForce, double sigmaInForce,
                                        double omegaTarget, double sigmaTarget) noexcept;

/// Plan for the beta, kappa, gamma of one mode over one sub-block of `length` samples.
struct CoeffRamp {
    float beta0{}, kappa0{}, gamma0{};  ///< Coefficients in force at the start of the sub-block.
    float beta1{}, kappa1{}, gamma1{};  ///< Target coefficients.
    int length{0};                      ///< Sub-block length S (16 or 32); 0 means "not planned".
    bool ramped{false};                 ///< True if the change exceeds the threshold.
};

/// Decide per A1-R6 whether the change from the coefficients in force (`current`, physical
/// parameters omegaCurrent, sigmaCurrent) to `target` (omegaTarget, sigmaTarget) must be
/// ramped over a sub-block of `subBlock` samples, and record the endpoints.
/// Thread: audio. Allocates: no.
[[nodiscard]] CoeffRamp planRamp(const OscCoeffs& current, double omegaCurrent, double sigmaCurrent,
                                 const OscCoeffs& target, double omegaTarget, double sigmaTarget,
                                 int subBlock) noexcept;

/// beta, kappa, gamma to use for the update of sample `sample` (0 <= sample < length) of the
/// sub-block. Ramped: linear in the sample index, value(s) = c0 + (c1 - c0) (s + 1) / S, so the
/// last sample of the sub-block uses exactly the target. Not ramped: the target from sample 0.
/// Thread: audio. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): beta, kappa, gamma in SPEC order
void coefficientsAt(const CoeffRamp& ramp, int sample, float& beta, float& kappa,
                    float& gamma) noexcept;

/// Write the coefficients of `ramps[j]` at `sample` into modes `firstMode + j` of `bank`
/// (state kept). Thread: audio. Allocates: no.
void applyRampsAt(ModeBank& bank, std::size_t firstMode, std::span<const CoeffRamp> ramps,
                  int sample) noexcept;

}  // namespace pme::osc
