// SPDX-License-Identifier: MIT
// Rank-one damper: one spring-dashpot pair in the SAV low-rank solve (A6-R2).
//
// SPEC sections implemented: SPEC-DMP "Damper model" (Rank-one); SPEC-NL "Discrete scheme",
// "Explicit low-rank solve".
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "pme/dampers.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::dmp {

/// The rank-one damper of one segment (SPEC-DMP "Rank-one").
///
/// Along c = phi-bar_d (vertical modal coordinates; 0 on every other coordinate) the pad is a
/// spring K_d on the centred average mu_t. y and a dashpot R_d on delta_t. y, y = c^T z, both
/// scaled by e. SPEC writes it as one low-rank pair h = gamma (.) e c / m, w = c (K_d / 2 +
/// R_d / (2 dt)). With A3's API unchanged this is the sum of
///   * one `nl::Dashpot` along c with R = e (R_d + K_d dt), whose pair is h = R gamma c / m,
///     w = c / (2 dt), so that h w^T = e gamma c c^T (K_d / 2 + R_d / (2 dt)) / m, and
///   * the explicit force -e K_d c (c^T z^{n-1}),
/// because e K_d mu_t. y = e K_d dt delta_t. y + e K_d y^{n-1} (derivation in docs/dev/dampers.md).
/// The damper uses one of the segment's three dashpot directions (SPEC-NL "Explicit low-rank
/// solve"). `S` is the state type (float in the engine, double in tests).
template <class S>
class RankOneDamper {
public:
    /// Build c over `coordinates` stacked coordinates: phi-bar_{d,n} (float64, rounded to S) on
    /// coordinates `firstCoordinate` .. `firstCoordinate + N - 1`, 0 elsewhere. `fs` is the
    /// sample rate. `InvalidArgument` for invalid parameters, empty tables or a range outside
    /// `coordinates`; unchanged then. Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const StringModes& modes, const DamperParams& params,
                                   std::size_t coordinates, std::size_t firstCoordinate = 0);

    /// c over the stacked coordinates (size `coordinates`).
    [[nodiscard]] std::span<const S> direction() const noexcept { return c_; }
    /// The same c in float64.
    [[nodiscard]] std::span<const double> direction64() const noexcept { return c64_; }

    /// The dashpot for engagement e: R = e (R_d + K_d dt) (A6-R2), `alongGradient` false.
    /// Valid while this object lives. Thread: audio. Allocates: no.
    [[nodiscard]] nl::Dashpot<S> dashpot(double e) const noexcept;

    /// y = c^T z for a stacked state array of size coordinates, summed in float64 in index order.
    /// Thread: audio. Allocates: no.
    [[nodiscard]] double pad(const S* z) const noexcept;

    /// Add the spring's explicit force -e K_d c (c^T z^{n-1}) (N per coordinate) to `force`,
    /// with z^{n-1} = q^n - u^n taken from the segment's present state. Call before `savStep`
    /// with the same e as `dashpot(e)`. `force` has the segment's size. Thread: audio.
    /// Allocates: no.
    void addSpringForce(double e, const nl::SavSegment<S>& seg, std::span<S> force) const noexcept;

    /// Energy of the spring after a step at constant e: E_d^{n+1/2} = (e K_d / 4)
    /// [(c^T z^{n+1})^2 + (c^T z^n)^2] (SPEC-DMP "Rank-one" energy; z^n from `seg.qPrev()`).
    /// Total energy = `nl::savEnergy(seg)` + this. Thread: audio. Allocates: no.
    [[nodiscard]] double springEnergy(double e, const nl::SavSegment<S>& seg) const noexcept;

    [[nodiscard]] double Kd() const noexcept { return kd_; }
    [[nodiscard]] double Rd() const noexcept { return rd_; }
    [[nodiscard]] double dt() const noexcept { return dt_; }

private:
    std::vector<S> c_;
    std::vector<double> c64_;
    double kd_{0.0};
    double rd_{0.0};
    double dt_{0.0};
};

extern template class RankOneDamper<float>;
extern template class RankOneDamper<double>;

}  // namespace pme::dmp
