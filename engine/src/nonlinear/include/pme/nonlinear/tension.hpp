// SPDX-License-Identifier: MIT
// Kirchhoff-Carrier tension-modulation potential (A7-R1).
//
// SPEC sections implemented: SPEC-GEO "Tension modulation (Kirchhoff-Carrier)"; SPEC-NL
// "SAV formulation" (the potential joins the segment's single psi), "Skipping" and "Drift
// control" (V_KC > 0 whenever the string sounds, so the skip rule never applies and the
// persistent-contact relaxation does).
//
// The potential takes plain numbers (k_n^2 and (EA - T) / (2 L)) so that pme_nonlinear gains no
// dependency on pme_string. The coordinate layout is the segment's SAV layout: the N vertical
// modes first, then the N horizontal modes, then any lumped coordinates (hammer, plectrum ...),
// which the potential does not touch. docs/dev/geometric.md.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "pme/status.hpp"

namespace pme::nl {

/// V_KC = (EA - T) / (8 L) S^2 over both polarisations (SPEC-GEO), with
/// S = sum_n k_n^2 (q_v,n^2 + q_h,n^2). Gradient dV/dq_.,n = Delta T k_n^2 q_.,n with
/// Delta T = (EA - T) / (2 L) S. Several potentials of one segment add and share psi (rank 1).
/// `S` is the state type; every sum is float64 in index order (vertical, then horizontal).
template <class S>
class TensionModulationPotential final : public Potential<S> {
public:
    /// Store k_n^2 (m^-2, n = 1..N) and the stiffness coefficient c = (EA - T) / (2 L) in N/m.
    /// `InvalidArgument` (object unchanged) for an empty `k2`, a non-finite or negative entry, or
    /// a non-finite or non-positive `stiffness`. Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(std::span<const double> k2, double stiffness);

    /// Number of modes per polarisation N (0 before `configure`).
    [[nodiscard]] std::size_t modes() const noexcept { return k2_.size(); }
    /// c = (EA - T) / (2 L).
    [[nodiscard]] double stiffness() const noexcept { return c_; }

    /// S = sum_n k_n^2 (q_v,n^2 + q_h,n^2) over z[0, 2N) (SPEC-GEO). Thread: audio.
    [[nodiscard]] double sum(std::span<const S> z) const noexcept {
        const std::size_t n = k2_.size();
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto v = static_cast<double>(z[i]);
            s += k2_[i] * v * v;  // SPEC-GEO S, vertical polarisation
        }
        for (std::size_t i = 0; i < n; ++i) {
            const auto h = static_cast<double>(z[n + i]);
            s += k2_[i] * h * h;  // SPEC-GEO S, horizontal polarisation
        }
        return s;
    }

    /// Delta T = (EA - T) S / (2 L) in N (SPEC-GEO), the extra tension (kappa_T hook).
    [[nodiscard]] double deltaT(std::span<const S> z) const noexcept { return c_ * sum(z); }

    /// V_KC = (EA - T) S^2 / (8 L) = c S^2 / 4 in J (SPEC-GEO).
    [[nodiscard]] double value(std::span<const S> z) const noexcept override {
        const double s = sum(z);
        return 0.25 * c_ * s * s;
    }

    /// dV/dq_.,n = Delta T k_n^2 q_.,n (SPEC-GEO); entries beyond 2N (lumped) are set to 0.
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override {
        const std::size_t n = k2_.size();
        const double dT = c_ * sum(z);
        for (std::size_t i = 0; i < n; ++i) {
            grad[i] = dT * k2_[i] * static_cast<double>(z[i]);
            grad[n + i] = dT * k2_[i] * static_cast<double>(z[n + i]);
        }
        for (std::size_t i = 2 * n; i < grad.size(); ++i) {
            grad[i] = 0.0;
        }
    }

private:
    std::vector<double> k2_;
    double c_{0.0};
};

extern template class TensionModulationPotential<float>;
extern template class TensionModulationPotential<double>;

}  // namespace pme::nl
