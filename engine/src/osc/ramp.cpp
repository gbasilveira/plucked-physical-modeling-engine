// SPDX-License-Identifier: MIT
// Coefficient ramp utility (A1-R6).
//
// SPEC sections implemented: SPEC-OSC "Time-varying parameters"; SPEC-CONV.
#include "pme/osc/ramp.hpp"

#include <cmath>

namespace pme::osc {

bool exceedsRampThreshold(double omegaInForce, double sigmaInForce, double omegaTarget,
                          double sigmaTarget) noexcept {
    // SPEC-OSC time-varying parameters: changes larger than 0.1% per sub-block are ramped.
    const bool omegaChanged =
        std::fabs(omegaTarget - omegaInForce) > kRampThreshold * std::fabs(omegaInForce);
    const bool sigmaChanged =
        std::fabs(sigmaTarget - sigmaInForce) > kRampThreshold * std::fabs(sigmaInForce);
    return omegaChanged || sigmaChanged;
}

CoeffRamp planRamp(const OscCoeffs& current, double omegaCurrent, double sigmaCurrent,
                   const OscCoeffs& target, double omegaTarget, double sigmaTarget,
                   int subBlock) noexcept {
    CoeffRamp r;
    r.beta0 = current.beta;
    r.kappa0 = current.kappa;
    r.gamma0 = current.gamma;
    r.beta1 = target.beta;
    r.kappa1 = target.kappa;
    r.gamma1 = target.gamma;
    r.length = subBlock;
    r.ramped = subBlock > 0 &&
               exceedsRampThreshold(omegaCurrent, sigmaCurrent, omegaTarget, sigmaTarget);
    return r;
}

namespace {

float lerpEnd(float c0, float c1, int sample, int length) noexcept {
    if (sample + 1 >= length) {
        return c1;  // ends exactly on the target
    }
    const double t = static_cast<double>(sample + 1) / static_cast<double>(length);
    return static_cast<float>(static_cast<double>(c0) +
                              (static_cast<double>(c1) - static_cast<double>(c0)) * t);
}

}  // namespace

void coefficientsAt(const CoeffRamp& ramp, int sample, float& beta, float& kappa,
                    float& gamma) noexcept {
    if (!ramp.ramped) {
        beta = ramp.beta1;
        kappa = ramp.kappa1;
        gamma = ramp.gamma1;
        return;
    }
    beta = lerpEnd(ramp.beta0, ramp.beta1, sample, ramp.length);
    kappa = lerpEnd(ramp.kappa0, ramp.kappa1, sample, ramp.length);
    gamma = lerpEnd(ramp.gamma0, ramp.gamma1, sample, ramp.length);
}

void applyRampsAt(ModeBank& bank, std::size_t firstMode, std::span<const CoeffRamp> ramps,
                  int sample) noexcept {
    for (std::size_t j = 0; j < ramps.size(); ++j) {
        float b = 0.0F;
        float k = 0.0F;
        float g = 0.0F;
        coefficientsAt(ramps[j], sample, b, k, g);
        bank.setCoefficients(firstMode + j, b, k, g);
    }
}

}  // namespace pme::osc
