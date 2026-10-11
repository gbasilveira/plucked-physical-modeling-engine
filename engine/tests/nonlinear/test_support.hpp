// SPDX-License-Identifier: MIT
// Shared helpers of the A3 tests: a deterministic generator and test potentials.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "pme/nonlinear/sav.hpp"

namespace pme::nl::test {

/// Deterministic uniform generator (the library distributions differ between standard libraries).
class Rng {
public:
    explicit Rng(std::uint64_t seed) : eng_(seed) {}
    /// Uniform in [0, 1).
    double uniform() { return static_cast<double>(eng_() >> 11U) * 0x1.0p-53; }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    std::size_t index(std::size_t n) {
        return static_cast<std::size_t>(uniform() * static_cast<double>(n));
    }

private:
    std::mt19937_64 eng_;
};

/// V = K/4 (c^T z)^4: one direction, gradient parallel to c.
template <class S>
class QuarticPotential final : public Potential<S> {
public:
    QuarticPotential(double k, std::vector<double> c) : k_(k), c_(std::move(c)) {}
    [[nodiscard]] double value(std::span<const S> z) const noexcept override {
        const double s = dot(z);
        return 0.25 * k_ * s * s * s * s;
    }
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override {
        const double s = dot(z);
        for (std::size_t i = 0; i < grad.size(); ++i) {
            grad[i] = k_ * s * s * s * c_[i];
        }
    }

private:
    [[nodiscard]] double dot(std::span<const S> z) const noexcept {
        double s = 0.0;
        for (std::size_t i = 0; i < c_.size(); ++i) {
            s += c_[i] * static_cast<double>(z[i]);
        }
        return s;
    }
    double k_;
    std::vector<double> c_;
};

/// V = K/4 (sum w_n z_n^2)^2: the tension-modulation shape.
template <class S>
class TensionPotential final : public Potential<S> {
public:
    TensionPotential(double k, std::vector<double> w) : k_(k), w_(std::move(w)) {}
    [[nodiscard]] double value(std::span<const S> z) const noexcept override {
        const double s = sum(z);
        return 0.25 * k_ * s * s;
    }
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override {
        const double s = sum(z);
        for (std::size_t i = 0; i < grad.size(); ++i) {
            grad[i] = k_ * s * w_[i] * static_cast<double>(z[i]);
        }
    }

private:
    [[nodiscard]] double sum(std::span<const S> z) const noexcept {
        double s = 0.0;
        for (std::size_t i = 0; i < w_.size(); ++i) {
            const auto zi = static_cast<double>(z[i]);
            s += w_[i] * zi * zi;
        }
        return s;
    }
    double k_;
    std::vector<double> w_;
};

/// One-sided wall on coordinate `index`: V = K/(p+1) [sign (z - wall)]_+^{p+1}; sign = +1 for a
/// wall above the rest position, -1 for a wall below.
template <class S>
class WallPotential final : public Potential<S> {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wall, sign, K, p in physical order
    WallPotential(std::size_t index, double wall, double sign, double k, double p)
        : index_(index), wall_(wall), sign_(sign), k_(k), p_(p) {}
    [[nodiscard]] double value(std::span<const S> z) const noexcept override {
        const double d = penetration(z);
        return d > 0.0 ? k_ / (p_ + 1.0) * std::pow(d, p_ + 1.0) : 0.0;
    }
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override {
        for (double& g : grad) {
            g = 0.0;
        }
        const double d = penetration(z);
        if (d > 0.0) {
            grad[index_] = sign_ * k_ * std::pow(d, p_);
        }
    }

private:
    [[nodiscard]] double penetration(std::span<const S> z) const noexcept {
        return sign_ * (static_cast<double>(z[index_]) - wall_);
    }
    std::size_t index_;
    double wall_, sign_, k_, p_;
};

}  // namespace pme::nl::test
