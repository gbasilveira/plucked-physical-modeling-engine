// SPDX-License-Identifier: MIT
// Longitudinal modes of a string, their pair-sum forcing and the longitudinal bridge force
// (A7-R2, A7-R3, A7-R4).
//
// SPEC sections implemented: SPEC-GEO "Longitudinal modes", "Longitudinal force at the bridge",
// "Energy balance of the default tier" (SI-0015); SPEC-OSC "Bandwidth", "Difference form" and
// "Discrete energy" (the longitudinal oscillators); SPEC-EXEC step 8; SPEC-CONV (float64 at
// compile time, float32 run-time states, float64 reductions in fixed order).
//
// Units. Transverse coordinates q have unit m^(3/2) (orthonormal shapes, SPEC-STR), so do the
// longitudinal coordinates xi_m. `longitudinalForce` returns f_m^L per unit modal mass, in
// m^(3/2)/s^2, which is exactly the `force` argument of `osc::stepModes` (gamma f). Energies
// multiply the per-unit-mass H of SPEC-OSC by rho A. docs/dev/geometric.md.
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::string {

/// Compiled longitudinal modes of one string segment plus the geometry constants of its pair
/// sum and bridge force. Immutable once built (SPEC-GEO).
struct LongitudinalModes {
    double fs{};        ///< Sample rate the oscillators were compiled for (Hz).
    double L{};         ///< String length (m).
    double EA{};        ///< Core axial stiffness E_c pi d_c^2 / 4 (N), SPEC-GEO.
    double rhoA{};      ///< Full linear mass density (kg/m), core plus winding (SPEC-GEO).
    double tension{};   ///< Tension T (N).
    double eaMinusT{};  ///< EA - T (N), > 0.
    BridgeEnd end{BridgeEnd::AtL};  ///< Bridge end of the segment: where F_L is evaluated.
    int requested{};                ///< M_L asked for.
    /// Transverse wavenumbers k_n (rad/m), n = 1..N, copied from the `StringModes`.
    std::vector<double> k;
    std::vector<double> K;               ///< K_m = m pi / L (rad/m), m = 1..kept.
    std::vector<double> omega;           ///< omega_m^L = K_m sqrt(EA / rhoA) (rad/s).
    std::vector<double> sigma;           ///< sigma_m = b1L + b3L (omega_m^L)^2 (1/s).
    std::vector<osc::OscCoeffsF64> osc;  ///< Oscillator coefficients per kept mode.
    /// Pair-sum prefactor -(EA - T) K_m / (2 rhoA sqrt(2 L)) per kept mode (SPEC-GEO).
    std::vector<double> pairCoeff;
    /// EA sqrt(2/L) K_m s_m per kept mode, s_m = (-1)^m at the L end and 1 at the 0 end: the
    /// coefficient of xi_m in the axial force at the bridge end (SPEC-GEO F_L).
    std::vector<double> bridgeCoeff;
    /// sqrt(2/L) k_n s_n per transverse mode: the slope of mode n at the bridge end.
    std::vector<double> slopeCoeff;
    double staticCoeff{};  ///< (EA - T) / 2 (N), the coefficient of y_x^2 + z_x^2 in F_L.

    /// Number of longitudinal modes kept by the bandwidth rule (at most `requested`).
    [[nodiscard]] int kept() const noexcept { return static_cast<int>(K.size()); }
};

/// EA = E_c pi d_c^2 / 4 from the core (SPEC-GEO): the winding does not carry axial load.
/// Requires valid parameters. Thread: any. Allocates: no.
[[nodiscard]] double axialStiffness(const StringParams& params) noexcept;

/// Compile the longitudinal modes (A7-R2; SPEC-GEO "Longitudinal modes").
///
/// chi_m = sqrt(2/L) sin(K_m x), K_m = m pi / L, omega_m^L = K_m sqrt(EA / rhoA), sigma_m =
/// b1L + b3L (omega_m^L)^2; coefficients by `compileOscillatorF64`. The kept modes are the longest
/// prefix m = 1..M that passes the SPEC-OSC bandwidth rule at `fs` and `fMax`, at most `ML`; at
/// piano lengths the fundamental is 1 to 3 kHz, so fewer than `ML` may be kept, down to none
/// (still `Ok`: `out.kept()` reports it). `ML` = 0 gives an empty set that still carries the
/// geometry constants of the quasi-static F_L term.
/// `InvalidArgument` (out unchanged) for invalid string parameters, fs <= 0, fMax NaN, ML < 0,
/// b1L or b3L negative or not finite, empty mode tables, or EA - T <= 0. Thread: non-audio.
/// Allocates: yes.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): arguments in SPEC-GEO order
[[nodiscard]] Status compileLongitudinal(const StringModes& modes, const StringParams& params,
                                         int ML, double b1L, double b3L, double fs, double fMax,
                                         LongitudinalModes& out);

namespace detail {

/// Pair sum of one polarisation for longitudinal mode m (1-based, SPEC-GEO):
/// sum_{i,j>=1} k_i k_j q_i q_j [delta_{i+j,m} + delta_{|i-j|,m}] over ordered pairs, i, j <= N.
/// O(N): the sum over i with j = m - i, and twice the sum over j with i = j + m. Float64
/// accumulation in index order. Thread: audio. Allocates: no.
template <class S>
[[nodiscard]] double pairSum(std::span<const double> k, std::span<const S> q,
                             std::size_t m) noexcept {
    const std::size_t n = std::min(k.size(), q.size());
    double sum = 0.0;
    // delta_{i+j,m}: i = 1..m-1 and j = m - i, both <= N.
    const std::size_t iLo = (m > n) ? m - n : 1;
    const std::size_t iHi = std::min(m - 1, n);
    for (std::size_t i = iLo; i <= iHi; ++i) {
        const std::size_t j = m - i;
        sum +=
            (k[i - 1] * static_cast<double>(q[i - 1])) * (k[j - 1] * static_cast<double>(q[j - 1]));
    }
    // delta_{|i-j|,m}: (i, j) = (j + m, j) and (j, j + m), j = 1..N-m.
    double diff = 0.0;
    for (std::size_t j = 1; j + m <= n; ++j) {
        diff += (k[j - 1] * static_cast<double>(q[j - 1])) *
                (k[j + m - 1] * static_cast<double>(q[j + m - 1]));
    }
    return sum + 2.0 * diff;
}

}  // namespace detail

/// Modal forcing f_m^L per unit modal mass for the kept modes m = 1..min(kept, fL.size()) by the
/// SPEC-GEO pair sum over both polarisations (A7-R3):
/// f_m^L = -(EA - T) K_m / (2 rhoA sqrt(2L)) sum_P sum_{i,j} k_i k_j q_P,i q_P,j [...].
/// `qH` may be empty (single polarisation). O(N) per mode, fixed summation order (vertical sum
/// plus horizontal sum), float64 accumulation, result in `S`. Mandatory for M_L <= 16
/// (SPEC-GEO). Thread: audio. Allocates: no.
template <class S>
void longitudinalForce(const LongitudinalModes& lm, std::span<const S> qV, std::span<const S> qH,
                       std::span<S> fL) noexcept {
    const std::size_t count = std::min(static_cast<std::size_t>(lm.kept()), fL.size());
    const std::span<const double> k(lm.k);
    for (std::size_t m = 1; m <= count; ++m) {
        const double sv = detail::pairSum<S>(k, qV, m);
        const double sh = qH.empty() ? 0.0 : detail::pairSum<S>(k, qH, m);
        fL[m - 1] = static_cast<S>(lm.pairCoeff[m - 1] * (sv + sh));  // SPEC-GEO pair sum
    }
}

/// Slope of one polarisation at the bridge end, y_x(L) = sum_n q_n sqrt(2/L) k_n (-1)^n for a
/// segment bridged at L (without the sign at the 0 end). Thread: audio. Allocates: no.
template <class S>
[[nodiscard]] double slopeAtBridge(const LongitudinalModes& lm, std::span<const S> q) noexcept {
    const std::size_t n = std::min(lm.slopeCoeff.size(), q.size());
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s += lm.slopeCoeff[i] * static_cast<double>(q[i]);  // SPEC-GEO y_x at the bridge end
    }
    return s;
}

/// Axial force excess at the bridge end (A7-R4; SPEC-GEO "Longitudinal force at the bridge"):
/// F_L = EA sum_m sqrt(2/L) K_m (-1)^m xi_m + (EA - T) / 2 (y_x^2 + z_x^2), evaluated at the
/// segment's bridge end (x = L for `BridgeEnd::AtL`; at x = 0 the sign (-1)^m is dropped, since
/// F_L is the tension excess xi_x + ... there). SPEC-GEO sums both polarisations in the
/// quadratic term (the PDE has y_x^2 + z_x^2); `qH` may be empty. Returns float64 (the
/// engine casts at the hook). `xi` holds the kept longitudinal coordinates. Thread: audio.
template <class S>
[[nodiscard]] double longitudinalBridgeForce(const LongitudinalModes& lm, std::span<const S> xi,
                                             std::span<const S> qV,
                                             std::span<const S> qH) noexcept {
    const std::size_t count = std::min(static_cast<std::size_t>(lm.kept()), xi.size());
    double lin = 0.0;
    for (std::size_t m = 0; m < count; ++m) {
        lin += lm.bridgeCoeff[m] * static_cast<double>(xi[m]);  // SPEC-GEO F_L, linear part
    }
    const double yx = slopeAtBridge<S>(lm, qV);
    const double zx = qH.empty() ? 0.0 : slopeAtBridge<S>(lm, qH);
    return lin + lm.staticCoeff * (yx * yx + zx * zx);  // SPEC-GEO F_L, (EA-T)/2 (y_x^2 + z_x^2)
}

/// The longitudinal oscillators of one segment: state, difference-form coefficients and energy.
/// `S` is float in the engine and double in tests (A1 `stepModes`, SPEC-OSC).
template <class S>
class LongitudinalBank {
public:
    /// Allocate and zero the state; coefficients from `lm`. Thread: non-audio. Allocates: yes.
    void configure(const LongitudinalModes& lm) {
        const std::size_t n = static_cast<std::size_t>(lm.kept());
        beta_.assign(n, S{});
        kappa_.assign(n, S{});
        gamma_.assign(n, S{});
        omegaHat2_.assign(n, 0.0);
        q_.assign(n, S{});
        u_.assign(n, S{});
        for (std::size_t i = 0; i < n; ++i) {
            beta_[i] = static_cast<S>(lm.osc[i].beta);
            kappa_[i] = static_cast<S>(lm.osc[i].kappa);
            gamma_[i] = static_cast<S>(lm.osc[i].gamma);
            omegaHat2_[i] = lm.osc[i].omegaHat2;
        }
        rhoA_ = lm.rhoA;
        dt_ = 1.0 / lm.fs;
    }

    /// Zero the state. Thread: audio. Allocates: no.
    void reset() noexcept {
        for (std::size_t i = 0; i < q_.size(); ++i) {
            q_[i] = S{};
            u_[i] = S{};
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return q_.size(); }
    [[nodiscard]] S* q() noexcept { return q_.data(); }
    [[nodiscard]] S* u() noexcept { return u_.data(); }
    [[nodiscard]] const S* q() const noexcept { return q_.data(); }
    [[nodiscard]] const S* u() const noexcept { return u_.data(); }

    /// Taylor start from rest (A7 start convention): with xi^0 = 0 and zero velocity, xi^-1 =
    /// dt^2 f^0 / 2 to second order, so u^0 = xi^0 - xi^-1 = -gamma f^0 / 2 (gamma = dt^2 to
    /// first order). Without it a start with u^0 = 0 makes the front arrive dt/2 early.
    /// `f0` is the pair-sum forcing of the initial transverse state. Thread: audio.
    void startFromRest(std::span<const S> f0) noexcept {
        for (std::size_t i = 0; i < q_.size() && i < f0.size(); ++i) {
            q_[i] = S{};
            u_[i] =
                static_cast<S>(-0.5 * static_cast<double>(gamma_[i]) * static_cast<double>(f0[i]));
        }
    }

    /// One difference-form sample with the explicit force `f` (per unit mass, size() entries)
    /// computed from the transverse state at step n (SPEC-EXEC step 8). Thread: audio.
    void step(std::span<const S> f) noexcept {
        osc::stepModes<S, S>(q_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(),
                             f.data(), q_.size());
    }

    /// E_L = sum_m rhoA H_m^L at time level n + 1/2 after `step` (SPEC-GEO energy note, SI-0015),
    /// float64, in index order. Thread: any. Allocates: no.
    [[nodiscard]] double energy() const noexcept {
        double e = 0.0;
        for (std::size_t i = 0; i < q_.size(); ++i) {
            const auto qNext = static_cast<double>(q_[i]);
            const auto uNext = static_cast<double>(u_[i]);
            e += osc::modeEnergy(omegaHat2_[i], qNext - uNext, qNext, uNext, dt_);
        }
        return rhoA_ * e;
    }

private:
    std::vector<S> beta_, kappa_, gamma_, q_, u_;
    std::vector<double> omegaHat2_;
    double rhoA_{0.0};
    double dt_{1.0};
};

}  // namespace pme::string
