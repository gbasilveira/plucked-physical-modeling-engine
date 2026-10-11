// SPDX-License-Identifier: MIT
// ModeBank storage, update and energy sums (A1-R3, A1-R5).
//
// SPEC sections implemented: SPEC-OSC "Difference form", "Discrete energy"; SPEC-CONV.
#include "pme/osc/mode_bank.hpp"

#include <cassert>

namespace pme::osc {

void ModeBank::resize(std::size_t modes) {
    const std::size_t padded = ((modes + kBankLane - 1) / kBankLane) * kBankLane;
    q_.allocate(padded);
    u_.allocate(padded);
    beta_.allocate(padded);
    kappa_.allocate(padded);
    gamma_.allocate(padded);
    omegaHat2_.allocate(padded);
    sigmaHat_.allocate(padded);
    count_ = modes;
    padded_ = padded;
}

void ModeBank::setMode(std::size_t i, const OscCoeffs& c) noexcept {
    assert(i < count_);
    beta_.data()[i] = c.beta;
    kappa_.data()[i] = c.kappa;
    gamma_.data()[i] = c.gamma;
    omegaHat2_.data()[i] = c.omegaHat2;
    sigmaHat_.data()[i] = c.sigmaHat;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): beta, kappa, gamma in SPEC order
void ModeBank::setCoefficients(std::size_t i, float beta, float kappa, float gamma) noexcept {
    assert(i < count_);
    beta_.data()[i] = beta;
    kappa_.data()[i] = kappa;
    gamma_.data()[i] = gamma;
}

void ModeBank::clearState() noexcept {
    for (std::size_t i = 0; i < padded_; ++i) {
        q_.data()[i] = 0.0F;
        u_.data()[i] = 0.0F;
    }
}

void ModeBank::step(std::span<const float> force, std::span<float> qPrev,
                    std::span<float> uPrev) noexcept {
    assert(force.size() == count_);
    assert(qPrev.empty() || qPrev.size() == count_);
    assert(uPrev.empty() || uPrev.size() == count_);
    // SPEC-OSC difference form (scalar reference kernel).
    stepModes<float, float>(q_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(),
                            force.data(), count_, qPrev.empty() ? nullptr : qPrev.data(),
                            uPrev.empty() ? nullptr : uPrev.data());
}

double bankEnergy(const ModeBank& bank, std::span<const float> qPrev, double dt) noexcept {
    assert(qPrev.size() == bank.size());
    // SPEC-OSC discrete energy, fixed index order.
    double sum = 0.0;
    for (std::size_t i = 0; i < bank.size(); ++i) {
        sum += modeEnergy(bank.omegaHat2()[i], static_cast<double>(qPrev[i]),
                          static_cast<double>(bank.q()[i]), static_cast<double>(bank.u()[i]), dt);
    }
    return sum;
}

double bankEnergyBalance(const ModeBank& bank, std::span<const float> force,
                         std::span<const float> uPrev, double dt) noexcept {
    assert(force.size() == bank.size() && uPrev.size() == bank.size());
    // SPEC-OSC discrete energy balance, fixed index order.
    double sum = 0.0;
    for (std::size_t i = 0; i < bank.size(); ++i) {
        sum += energyBalance(bank.sigmaHat()[i], static_cast<double>(force[i]),
                             static_cast<double>(bank.u()[i]), static_cast<double>(uPrev[i]), dt);
    }
    return sum;
}

}  // namespace pme::osc
