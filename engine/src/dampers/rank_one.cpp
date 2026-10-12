// SPDX-License-Identifier: MIT
// Rank-one damper (A6-R2). See pme/dampers/rank_one.hpp.
#include "pme/dampers/rank_one.hpp"

#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/lift.hpp"

namespace pme::dmp {

template <class S>
Status RankOneDamper<S>::configure(const StringModes& modes, const DamperParams& params,
                                   std::size_t coordinates, std::size_t firstCoordinate) {
    if (modes.omega.empty() || modes.k.size() != modes.omega.size() || !(modes.fs > 0.0) ||
        !(modes.L > 0.0) || validateParams(params) != Status::Ok ||
        firstCoordinate + modes.omega.size() > coordinates) {
        return Status::InvalidArgument;
    }
    const std::vector<double> phi = padShape(modes, params);
    c64_.assign(coordinates, 0.0);
    c_.assign(coordinates, S{});
    for (std::size_t i = 0; i < phi.size(); ++i) {
        c64_[firstCoordinate + i] = phi[i];
        c_[firstCoordinate + i] = static_cast<S>(phi[i]);
    }
    kd_ = params.Kd;
    rd_ = params.Rd;
    dt_ = 1.0 / modes.fs;
    return Status::Ok;
}

template <class S>
nl::Dashpot<S> RankOneDamper<S>::dashpot(double e) const noexcept {
    nl::Dashpot<S> d;
    d.c = std::span<const S>(c_);
    d.R = e * (rd_ + kd_ * dt_);  // A6-R2: carries e (K_d / 2 + R_d / (2 dt)) in h w^T
    d.alongGradient = false;
    return d;
}

template <class S>
double RankOneDamper<S>::pad(const S* z) const noexcept {
    double y = 0.0;
    for (std::size_t i = 0; i < c_.size(); ++i) {
        y += static_cast<double>(c_[i]) * static_cast<double>(z[i]);
    }
    return y;
}

template <class S>
void RankOneDamper<S>::addSpringForce(double e, const nl::SavSegment<S>& seg,
                                      std::span<S> force) const noexcept {
    // z^{n-1} = q^n - u^n (difference form); y^{n-1} = c^T z^{n-1}
    const S* q = seg.q();
    const S* u = seg.u();
    double yPrev = 0.0;
    for (std::size_t i = 0; i < c_.size(); ++i) {
        yPrev +=
            static_cast<double>(c_[i]) * (static_cast<double>(q[i]) - static_cast<double>(u[i]));
    }
    // SPEC-DMP rank-one: e K_d mu_t. y = e K_d dt delta_t. y + e K_d y^{n-1}; the second term is
    // explicit and enters the force as -e K_d c y^{n-1}.
    const double scale = -e * kd_ * yPrev;
    for (std::size_t i = 0; i < c_.size(); ++i) {
        force[i] =
            static_cast<S>(static_cast<double>(force[i]) + scale * static_cast<double>(c_[i]));
    }
}

template <class S>
double RankOneDamper<S>::springEnergy(double e, const nl::SavSegment<S>& seg) const noexcept {
    const double yNext = pad(seg.q());
    const double yNow = pad(seg.qPrev());
    return 0.25 * e * kd_ * (yNext * yNext + yNow * yNow);  // SPEC-DMP rank-one energy
}

template class RankOneDamper<float>;
template class RankOneDamper<double>;

}  // namespace pme::dmp
