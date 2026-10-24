// SPDX-License-Identifier: MIT
// Parameter checks and the engagement follower (A6-R3, A6-R4). See pme/dampers/lift.hpp.
#include "pme/dampers/lift.hpp"

#include <cmath>

namespace pme::dmp {

Status validateHalfPedal(double p0, double p1) noexcept {
    if (!std::isfinite(p0) || !std::isfinite(p1) || !(p0 >= 0.0) || !(p1 <= 1.0) || !(p0 < p1)) {
        return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status validateParams(const DamperParams& p) noexcept {
    const bool finite = std::isfinite(p.xRatio) && std::isfinite(p.width) && std::isfinite(p.Kd) &&
                        std::isfinite(p.Rd) && std::isfinite(p.tau0) && std::isfinite(p.kRelease) &&
                        std::isfinite(p.tauUp) && std::isfinite(p.etaD);
    if (!finite || p.xRatio < 0.0 || p.xRatio > 1.0 || p.width < 0.0 || p.Kd < 0.0 || p.Rd < 0.0 ||
        !(p.tau0 > 0.0) || !(p.tauUp > 0.0) || p.kRelease < 0.0 || !(p.etaD > 0.0)) {
        return Status::InvalidArgument;
    }
    return Status::Ok;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): fs then initial e
void EngagementFollower::configure(const DamperParams& params, double fs,
                                   double initialE) noexcept {
    params_ = params;
    dt_ = 1.0 / fs;
    e_ = initialE;
    target_ = initialE;
    tauUp_ = params.tauUp;
    tauDown_ = landingTau(params.tau0, params.kRelease, 0.0);
    decayUp_ = std::exp(-dt_ / tauUp_);
    decayDown_ = std::exp(-dt_ / tauDown_);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): target then nu_rel
void EngagementFollower::setTarget(double target, double nuRel) noexcept {
    target_ = target;
    tauDown_ = landingTau(params_.tau0, params_.kRelease, nuRel);  // SPEC-DMP timing
    decayDown_ = std::exp(-dt_ / tauDown_);
}

bool EngagementFollower::step() noexcept {
    if (e_ == target_) {
        return false;
    }
    // SPEC-DMP timing: e^{n+1} = e_target + (e^n - e_target) exp(-dt / tau)
    const double decay = target_ < e_ ? decayUp_ : decayDown_;
    e_ = target_ + (e_ - target_) * decay;
    if (std::fabs(e_ - target_) < kEngagementSnap) {
        e_ = target_;
        return false;
    }
    return true;
}

double EngagementFollower::projected(int n) const noexcept {
    if (e_ == target_ || n <= 0) {
        return e_;
    }
    const double tau = target_ < e_ ? tauUp_ : tauDown_;
    const double e = target_ + (e_ - target_) * std::exp(-static_cast<double>(n) * dt_ / tau);
    return std::fabs(e - target_) < kEngagementSnap ? target_ : e;
}

}  // namespace pme::dmp
