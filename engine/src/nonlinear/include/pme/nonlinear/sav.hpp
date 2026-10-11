// SPDX-License-Identifier: MIT
// Scalar-auxiliary-variable (SAV) framework and explicit low-rank solve (A3-R1 to A3-R5).
//
// SPEC sections implemented: SPEC-NL "Coordinates", "SAV formulation", "Discrete scheme",
// "Explicit low-rank solve", "Skipping", "Drift control", "Contracts"; SPEC-OSC "Difference form"
// and "Discrete energy" (the oscillator of every coordinate); SPEC-CONV (float64 reductions).
//
// Internal to the engine (interfaces-v2 declares no nonlinear API). The step is written once as
// a template on the state type S: float in the engine, double in tests. Every reduction, psi, V,
// eps0 and the r x r solve are float64 whatever S is.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/status.hpp"

namespace pme::nl {

/// Largest number of dashpot directions one segment may carry (SPEC-NL "Explicit low-rank
/// solve": more than 3 must be lumped into the diagonal damper form by the caller).
inline constexpr std::size_t kMaxDashpots = 3;
/// Largest rank of the low-rank correction: the SAV pair plus kMaxDashpots dashpot pairs.
inline constexpr std::size_t kMaxRank = 1 + kMaxDashpots;

/// A conservative potential V(z) >= 0 (SPEC-NL "Contracts"). Several potentials on one segment
/// add and share one psi. Thread: audio. Both functions are noexcept and allocation-free.
template <class S>
class Potential {
public:
    Potential() = default;
    Potential(const Potential&) = default;
    Potential& operator=(const Potential&) = default;
    Potential(Potential&&) = default;
    Potential& operator=(Potential&&) = default;
    virtual ~Potential() = default;

    /// V(z) >= 0 over the stacked coordinates z (size nz). V must be exactly 0 when the
    /// potential is inactive (no contact): the skip rule relies on it.
    [[nodiscard]] virtual double value(std::span<const S> z) const noexcept = 0;
    /// Write the gradient dV/dz into `grad` (size nz), overwriting every element.
    virtual void gradient(std::span<const S> z, std::span<double> grad) const noexcept = 0;
};

/// A dashpot of coefficient R >= 0 along the contact direction c (SPEC-NL "Discrete scheme").
/// The caller (hammer, damper, barrier) sets `R` every sample.
template <class S>
struct Dashpot {
    std::span<const S> c;       ///< Direction over the stacked coordinates (size nz).
    double R{0.0};              ///< Coefficient R^n >= 0 in kg/s (Hunt-Crossley, damper felt).
    bool alongGradient{false};  ///< True when c is parallel to g (one contact): merged into rank 1.
};

/// Tunable parameters of a segment (defaults per SPEC-NL).
struct SavParams {
    double eps0{1.0e-12};            ///< epsilon_0 in J (SPEC-NL "SAV formulation").
    bool driftRelaxation{true};      ///< Persistent-contact relaxation (SPEC-NL "Drift control" 2).
    double driftAfterSeconds{0.05};  ///< Contact duration after which the relaxation applies.
    double driftTau{0.01};           ///< Relaxation time constant in s.
};

/// Diagnostic counters (A3-R5): monotonically increasing, plain members, read and cleared by the
/// runtime on the audio thread.
struct SavCounters {
    std::uint32_t resets{0};            ///< Reset-at-separation events that lowered psi.
    std::uint32_t driftCorrections{0};  ///< Persistent-contact relaxations applied.
    std::uint32_t solvesSkipped{0};     ///< Steps taken by the skip rule (free update, no solve).
};

/// One segment: stacked coordinates (modes, then lumped), their oscillators and masses, psi,
/// V^{n-1}, the contact counter and the scratch the step needs (SPEC-NL "Coordinates").
/// Everything is allocated by `configure` (non-real-time).
template <class S>
class SavSegment {
public:
    /// Allocate for `nModes` modal and `nLumped` lumped coordinates and set the state to rest
    /// (psi = sqrt(2 eps0), V^{n-1} = 0). `InvalidArgument` if fs <= 0, nModes + nLumped == 0,
    /// maxDashpots > kMaxDashpots or a parameter is not positive; the segment is unchanged then.
    /// Thread: non-audio. Allocates: yes.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): counts in SPEC-NL order
    [[nodiscard]] Status configure(std::size_t nModes, std::size_t nLumped, std::size_t maxDashpots,
                                   double fs, const SavParams& params = {});

    /// Oscillator and mass of coordinate `i` (< size()): the coefficients are rounded to S; the
    /// state is kept. `mass` > 0 (rho A for string modes, m_H for a hammer).
    /// `InvalidArgument` on a bad index or mass. Thread: any. Allocates: no.
    [[nodiscard]] Status setCoordinate(std::size_t i, const osc::OscCoeffsF64& c,
                                       double mass) noexcept;
    /// Free lumped mass at coordinate `i`: omegaHat = sigmaHat = 0, so beta = 1, kappa = 0 and
    /// gamma = dt^2 (SPEC-NL "Coordinates"). Thread: any. Allocates: no.
    [[nodiscard]] Status setLumped(std::size_t i, double mass) noexcept;

    /// Zero the state, set psi = sqrt(2 eps0), V^{n-1} = 0 and the contact counter to 0. The
    /// diagnostic counters are kept. Thread: any. Allocates: no.
    void resetState() noexcept;
    /// Set psi^{n-1/2} = sqrt(2 (V(z^n) + eps0)) and V^{n-1} = V(z^n) for the present q, so the
    /// first step starts consistent with the potentials. Thread: any. Allocates: no.
    void initialisePsi(std::span<const Potential<S>* const> potentials) noexcept;

    /// Overwrite psi^{n-1/2} (initial conditions and tests of the drift rules; psi > 0).
    void setPsi(double psi) noexcept { psi_ = psi; }

    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    [[nodiscard]] std::size_t modes() const noexcept { return nModes_; }
    [[nodiscard]] std::size_t lumped() const noexcept { return n_ - nModes_; }
    [[nodiscard]] std::size_t maxDashpots() const noexcept { return maxDashpots_; }
    [[nodiscard]] double dt() const noexcept { return dt_; }

    [[nodiscard]] S* q() noexcept { return q_.data(); }
    [[nodiscard]] S* u() noexcept { return u_.data(); }
    [[nodiscard]] const S* q() const noexcept { return q_.data(); }
    [[nodiscard]] const S* u() const noexcept { return u_.data(); }
    /// q^n and u^n of the last step (the state before it).
    [[nodiscard]] const S* qPrev() const noexcept { return qPrev_.data(); }
    [[nodiscard]] const S* uPrev() const noexcept { return uPrev_.data(); }
    [[nodiscard]] const S* beta() const noexcept { return beta_.data(); }
    [[nodiscard]] const S* kappa() const noexcept { return kappa_.data(); }
    [[nodiscard]] const S* gamma() const noexcept { return gamma_.data(); }
    [[nodiscard]] const double* omegaHat2() const noexcept { return omegaHat2_.data(); }
    [[nodiscard]] const double* mass() const noexcept { return mass_.data(); }
    /// Length of the padded arrays (multiple of 16); padding lanes stay zero.
    [[nodiscard]] std::size_t paddedSize() const noexcept { return padded_; }

    [[nodiscard]] double psi() const noexcept { return psi_; }
    [[nodiscard]] double potentialPrev() const noexcept { return vPrev_; }
    [[nodiscard]] std::uint32_t contactSamples() const noexcept { return contactSamples_; }
    [[nodiscard]] const SavParams& params() const noexcept { return params_; }
    [[nodiscard]] SavCounters& counters() noexcept { return counters_; }
    [[nodiscard]] const SavCounters& counters() const noexcept { return counters_; }
    /// Change the tunables (derived constants are recomputed). Thread: any. Allocates: no.
    void setParams(const SavParams& params) noexcept;

private:
    template <class T>
    friend void savStep(SavSegment<T>&, std::span<const Potential<T>* const>,
                        std::span<const Dashpot<T>>, std::span<const T>) noexcept;

    void deriveConstants() noexcept;

    std::size_t n_{0};
    std::size_t nModes_{0};
    std::size_t padded_{0};
    std::size_t maxDashpots_{0};
    double dt_{0.0};
    double fs_{0.0};
    SavParams params_{};
    std::uint32_t driftThresholdSamples_{0};
    double driftDecay_{0.0};
    double psi_{0.0};
    double vPrev_{0.0};
    std::uint32_t contactSamples_{0};
    SavCounters counters_{};

    osc::AlignedArray<S> q_, u_, qPrev_, uPrev_, beta_, kappa_, gamma_, force_;
    osc::AlignedArray<double> omegaHat2_, mass_;
    // Scratch (allocated in configure): z^{n-1}, a / z^{n+1}, g, gradient accumulation, columns.
    osc::AlignedArray<double> zPrev_, a_, g_, gradTmp_;
    std::array<osc::AlignedArray<double>, kMaxRank> h_, w_;
};

/// Solve z = a - H (I_r + W^T H)^{-1} W^T a (SPEC-NL "Explicit low-rank solve"). `h[j]` and
/// `w[j]` point to the n entries of column j, j < r <= kMaxRank. r = 1 is the closed form
/// z = a - h (w^T a) / (1 + w^T h); r = 2..4 forms the r x r matrix and solves it by Gaussian
/// elimination with partial pivoting, all in float64 with fixed reduction order. `z` may alias
/// `a`. Thread: audio. Allocates: no.
void lowRankSolve(double* z, const double* a, std::size_t n, std::size_t r, const double* const* h,
                  const double* const* w) noexcept;

/// One sample of the SAV update (SPEC-NL "Discrete scheme"), including the psi update, the reset
/// rule, the persistent-contact relaxation and the skip rule. `explicitForce` is F^n per
/// coordinate in N (size nz, or empty for none); `dashpots` has at most maxDashpots entries.
/// Afterwards q holds z^{n+1}, u holds u^{n+1}, qPrev/uPrev hold q^n/u^n and psi() is
/// psi^{n+1/2}. Thread: audio. Allocates: no.
template <class S>
void savStep(SavSegment<S>& seg, std::span<const Potential<S>* const> potentials,
             std::span<const Dashpot<S>> dashpots, std::span<const S> explicitForce) noexcept;

/// Conserved energy after a step (SPEC-NL "Discrete scheme"): sum_i m_i H_i^{n+1/2}
/// (SPEC-OSC "Discrete energy") + psi^2/2 - eps0, in float64, in index order.
template <class S>
[[nodiscard]] double savEnergy(const SavSegment<S>& seg) noexcept;

/// Energy removed by the dashpots in the last step: dt sum_j R_j (c_j^T delta_t. z^n)^2 with
/// delta_t. z^n = (u^{n+1} + u^n) / (2 dt) (SPEC-NL "Discrete scheme"). `dashpots` must be the
/// span given to that step. Thread: audio. Allocates: no.
template <class S>
[[nodiscard]] double savDissipation(const SavSegment<S>& seg,
                                    std::span<const Dashpot<S>> dashpots) noexcept;

}  // namespace pme::nl
