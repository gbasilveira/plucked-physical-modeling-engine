// SPDX-License-Identifier: MIT
// 0.5.5: hammer contact with a per-string gap. MVP-local copy-and-adapt of pme::exciters::HammerPotential
// and pme::exciters::HammerFelt (engine/src/exciters/hammer.cpp, SPEC-HAM "Contact with each struck
// string", "Felt dissipation"); the engine is not modified. The only difference is a constant
// per-string gap g_s >= 0 (metres): delta_s = x_H - (string_s + g_s). g_s = 0 reproduces the engine
// arithmetic exactly (x - 0.0 == x). A constant offset does not change the gradient directions c_s,
// so the SAV formulation and its passivity are untouched (V_H stays convex in delta_s).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>
#include "pme/exciters/hammer.hpp"

namespace piano_mvp_v4 {
class ContactPotential final : public pme::nl::Potential<double> {
public:
    using StrikeMap = pme::exciters::StrikeMap;
    using StringSlice = pme::exciters::StringSlice;
    pme::Status configure(std::vector<StringSlice> strings, std::size_t hammerIndex, double lever, double approachSign) {
        if (strings.empty() || strings.size() > pme::exciters::kMaxStruckStrings || !std::isfinite(lever) ||
            (approachSign != 1.0 && approachSign != -1.0)) return pme::Status::InvalidArgument;
        for (const StringSlice& s : strings) {
            if (s.phiBar.empty() || s.offset + s.phiBar.size() > hammerIndex) return pme::Status::InvalidArgument;
            for (const float p : s.phiBar) if (!std::isfinite(p)) return pme::Status::InvalidArgument;
        }
        strings_ = std::move(strings); hammerIndex_ = hammerIndex; lever_ = lever; sE_ = approachSign;
        active_ = false; map_ = StrikeMap{}; map_.strings = strings_.size(); gap_.fill(0.0);
        return pme::Status::Ok;
    }
    void setStrikeMap(const StrikeMap& map) noexcept { map_ = map; }
    // Per-string gap in metres (>= 0): string s is touched only after the head has travelled gap[s] further.
    void setGaps(const std::array<double, 3>& g) noexcept { gap_ = g; }
    const std::array<double, 3>& gaps() const noexcept { return gap_; }
    void setActive(bool a) noexcept { active_ = a; }
    bool active() const noexcept { return active_; }
    double value(std::span<const double> z) const noexcept override {
        if (!active_) return 0.0;
        const double xh = headPosition(z);
        double v = 0.0;
        for (std::size_t s = 0; s < strings_.size(); ++s) {
            const double w = map_.weight[s];
            if (!(w > 0.0)) continue;
            const double d = xh - stringPosition(z, s) - gap_[s];
            if (d > 0.0) v += w * map_.stiffness[s] / (map_.exponent[s] + 1.0) * d * std::pow(d, map_.exponent[s]);
        }
        return v;
    }
    void gradient(std::span<const double> z, std::span<double> grad) const noexcept override {
        std::fill(grad.begin(), grad.end(), 0.0);
        if (!active_) return;
        const double xh = headPosition(z);
        for (std::size_t s = 0; s < strings_.size(); ++s) {
            const double w = map_.weight[s];
            if (!(w > 0.0)) continue;
            const StringSlice& sl = strings_[s];
            const double d = xh - stringPosition(z, s) - gap_[s];
            if (d > 0.0) {
                const double f = w * map_.stiffness[s] * std::pow(d, map_.exponent[s]);
                grad[hammerIndex_] += f;
                if (lever_ != 0.0) grad[hammerIndex_ + 1] += lever_ * f;
                for (std::size_t n = 0; n < sl.phiBar.size(); ++n)
                    grad[sl.offset + n] += -sE_ * static_cast<double>(sl.phiBar[n]) * f;
            }
        }
    }
    double hammerGradient(std::span<const double> z) const noexcept {
        if (!active_) return 0.0;
        const double xh = headPosition(z);
        double g = 0.0;
        for (std::size_t s = 0; s < strings_.size(); ++s) {
            const double w = map_.weight[s];
            if (!(w > 0.0)) continue;
            const double d = xh - stringPosition(z, s) - gap_[s];
            if (d > 0.0) g += w * map_.stiffness[s] * std::pow(d, map_.exponent[s]);
        }
        return g;
    }
    double headPosition(std::span<const double> z) const noexcept {
        double x = z[hammerIndex_];
        if (lever_ != 0.0) x += lever_ * z[hammerIndex_ + 1];
        return x;
    }
    // Window-averaged string position along the hammer axis (without the gap).
    double stringPosition(std::span<const double> z, std::size_t s) const noexcept {
        const StringSlice& sl = strings_[s];
        double acc = 0.0;
        for (std::size_t n = 0; n < sl.phiBar.size(); ++n) acc += static_cast<double>(sl.phiBar[n]) * z[sl.offset + n];
        return sE_ * acc;
    }
    // delta_s including the gap.
    double penetration(std::span<const double> z, std::size_t s) const noexcept {
        return headPosition(z) - stringPosition(z, s) - gap_[s];
    }
    double maxPenetration(std::span<const double> z) const noexcept {
        const double xh = headPosition(z);
        double m = -1.0e300;
        for (std::size_t s = 0; s < strings_.size(); ++s)
            if (map_.weight[s] > 0.0) m = std::max(m, xh - stringPosition(z, s) - gap_[s]);
        return m;
    }
    std::size_t stringCount() const noexcept { return strings_.size(); }
    const StringSlice& slice(std::size_t s) const noexcept { return strings_[s]; }
    const StrikeMap& map() const noexcept { return map_; }
    std::size_t hammerIndex() const noexcept { return hammerIndex_; }
    double lever() const noexcept { return lever_; }
    double approachSign() const noexcept { return sE_; }
private:
    std::vector<StringSlice> strings_;
    std::size_t hammerIndex_{0};
    double lever_{0.0}, sE_{1.0};
    bool active_{false};
    StrikeMap map_{};
    std::array<double, 3> gap_{};
};

// Felt of ContactPotential: engine HammerFelt with the potential type replaced.
class ContactFelt {
public:
    using HammerParams = pme::HammerParams;
    pme::Status configure(const HammerParams& hammer, const ContactPotential& potential, std::size_t nz, double fs) {
        if (pme::exciters::validate(hammer) != pme::Status::Ok || !(fs > 0) || potential.stringCount() == 0 || potential.hammerIndex() >= nz)
            return pme::Status::InvalidArgument;
        law_ = hammer.law; muHC_ = hammer.muHC; eps_ = hammer.stulovEps;
        decay_ = hammer.law == pme::FeltLaw::Stulov ? std::exp(-1.0 / (fs * hammer.stulovTau)) : 0.0;
        const std::size_t ns = potential.stringCount();
        count_ = pme::exciters::hammerDashpotCount(hammer, ns);
        c_.assign(ns, std::vector<double>(nz, 0.0));
        const std::size_t hi = potential.hammerIndex();
        for (std::size_t s = 0; s < ns; ++s) {
            c_[s][hi] = 1.0;
            if (potential.lever() != 0.0) c_[s][hi + 1] = potential.lever();
            const auto& sl = potential.slice(s);
            for (std::size_t n = 0; n < sl.phiBar.size(); ++n)
                c_[s][sl.offset + n] = -potential.approachSign() * static_cast<double>(sl.phiBar[n]);
        }
        reset();
        return pme::Status::Ok;
    }
    void reset() noexcept {
        h_.fill(0.0); force_.fill(0.0);
        for (auto& d : dashpots_) { d.R = 0.0; d.alongGradient = false; }
    }
    void update(const ContactPotential& potential, std::span<const double> z, std::span<double> force) noexcept {
        const auto& map = potential.map();
        const std::size_t ns = potential.stringCount();
        std::array<double, 3> delta{}, dp{};
        std::size_t contributors = 0;
        for (std::size_t s = 0; s < ns; ++s) {
            const double d = potential.penetration(z, s);
            delta[s] = d;
            dp[s] = d > 0.0 ? std::pow(d, map.exponent[s]) : 0.0;
            if (d > 0.0 && map.weight[s] * map.stiffness[s] > 0.0) ++contributors;
        }
        if (law_ == pme::FeltLaw::HuntCrossley) {
            const bool along = contributors == 1;
            for (std::size_t s = 0; s < ns; ++s) {
                auto& d = dashpots_[s];
                d.c = std::span<const double>(c_[s]);
                d.R = delta[s] > 0.0 ? map.weight[s] * map.stiffness[s] * muHC_ * dp[s] : 0.0;
                d.alongGradient = along;
            }
            return;
        }
        for (std::size_t s = 0; s < ns; ++s) {
            h_[s] = decay_ * h_[s] + (1.0 - decay_) * dp[s];
            const double wk = map.weight[s] * map.stiffness[s];
            const double f = delta[s] > 0.0 ? std::min(wk * eps_ * h_[s], wk * dp[s]) : 0.0;
            force_[s] = f;
            if (f != 0.0)
                for (std::size_t i = 0; i < force.size(); ++i) force[i] += f * c_[s][i];
        }
    }
    std::span<const pme::nl::Dashpot<double>> dashpots() const noexcept { return {dashpots_.data(), count_}; }
private:
    pme::FeltLaw law_{pme::FeltLaw::HuntCrossley};
    double muHC_{0.0}, eps_{0.0}, decay_{0.0};
    std::size_t count_{0};
    std::vector<std::vector<double>> c_;
    std::array<pme::nl::Dashpot<double>, pme::exciters::kMaxStruckStrings> dashpots_{};
    std::array<double, 3> h_{}, force_{};
};
}
