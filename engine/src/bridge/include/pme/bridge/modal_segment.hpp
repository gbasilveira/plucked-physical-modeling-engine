// SPDX-License-Identifier: MIT
// ModalSegment: the bridge-facing half of one string segment (both polarisations): mode states,
// the bridge force F_b and the moving-support modal force -g_n delta_tt w. It is the reference
// implementation of the segment side of SPEC-BRG "Discrete coupling order" that tests, A5, A7
// and C1 can reuse or mirror; exciters, dampers and nonlinear terms are not part of it.
//
// SPEC sections implemented: SPEC-BRG "Modal equation with a moving support", "Bridge force",
// "Discrete coupling order" steps 1 and 3; SPEC-OSC "Difference form" and "Discrete energy";
// SPEC-STR "Energy" (modal mass rhoA).
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "pme/bridge/terms.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::bridge {

/// One string segment's modes on a bridge point. `S` is the state type. Coefficients are derived
/// in float64 from the compiled `StringModes` (g_n, a_n from float64; beta, kappa, gamma from the
/// stored float64 omegaHat^2 and sigmaHat) and rounded to `S`, so `S` = float reproduces
/// `compileBridgeTerms` exactly and `S` = double keeps the partitioned scheme's symmetry (energy
/// tests).
template <class S>
class ModalSegment {
public:
    /// Build from compiled modes. `InvalidArgument` as `compileBridgeTerms`. Unchanged unless
    /// `Ok`. States start at zero. Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const StringModes& modes, BridgeEnd end) {
        std::vector<double> g;
        std::vector<double> aV;
        std::vector<double> aHd;
        if (Status s = liftingOverlaps(modes, end, g); s != Status::Ok) {
            return s;
        }
        if (Status s = forceCoefficients(modes, end, Polarisation::Vertical, aV); s != Status::Ok) {
            return s;
        }
        if (Status s = forceCoefficients(modes, end, Polarisation::Horizontal, aHd);
            s != Status::Ok) {
            return s;
        }
        const std::size_t n = g.size();
        const double dt = 1.0 / modes.fs;
        g_.assign(n, S{});
        aV_.assign(n, S{});
        aH_.assign(n, S{});
        for (std::size_t i = 0; i < n; ++i) {
            g_[i] = static_cast<S>(g[i]);
            aV_[i] = static_cast<S>(aV[i]);
            aH_[i] = static_cast<S>(aHd[i]);
        }
        fillCoefficients(modes.oscV, dt, v_);
        fillCoefficients(modes.oscH, dt, h_);
        v_.q.assign(n, S{});
        v_.u.assign(n, S{});
        h_.q.assign(n, S{});
        h_.u.assign(n, S{});
        force_.assign(n, S{});
        rhoA_ = modes.rhoA;
        tOverL_ = modes.tension / modes.L;
        dt_ = dt;
        end_ = end;
        return Status::Ok;
    }

    /// Zero all mode states. Thread: audio. Allocates: no.
    void reset() noexcept {
        for (std::size_t i = 0; i < v_.q.size(); ++i) {
            v_.q[i] = v_.u[i] = h_.q[i] = h_.u[i] = S{};
        }
    }

    /// Mode states q_n, u_n = q_n - q_n^{prev} per polarisation (tests set initial conditions).
    [[nodiscard]] S* qV() noexcept { return v_.q.data(); }
    [[nodiscard]] S* uV() noexcept { return v_.u.data(); }
    [[nodiscard]] S* qH() noexcept { return h_.q.data(); }
    [[nodiscard]] S* uH() noexcept { return h_.u.data(); }
    [[nodiscard]] const S* qV() const noexcept { return v_.q.data(); }
    [[nodiscard]] const S* qH() const noexcept { return h_.q.data(); }
    [[nodiscard]] const S* gOverlaps() const noexcept { return g_.data(); }
    [[nodiscard]] std::size_t modeCount() const noexcept { return g_.size(); }
    [[nodiscard]] BridgeEnd end() const noexcept { return end_; }
    [[nodiscard]] double tOverL() const noexcept { return tOverL_; }
    [[nodiscard]] double rhoA() const noexcept { return rhoA_; }

    /// F_b,s for the vertical polarisation from q^n and w_v^n (SPEC-BRG "Bridge force").
    /// Thread: audio. Allocates: no.
    [[nodiscard]] S forceV(S wV) const noexcept {
        return bridgeForce<S, S>(std::span<const S>(aV_), tOverL_, v_.q.data(), wV);
    }
    /// F_b,s for the horizontal polarisation, with the horizontal discrete omegaHat^2.
    [[nodiscard]] S forceH(S wH) const noexcept {
        return bridgeForce<S, S>(std::span<const S>(aH_), tOverL_, h_.q.data(), wH);
    }

    /// Advance the modes with the modal force -g_n delta_tt w^n (SPEC-BRG "Discrete coupling
    /// order" step 3). `accV`, `accH` are `BridgePoint::accelV()` / `accelH()`. Thread: audio.
    void advance(S accV, S accH) noexcept {
        const std::size_t n = g_.size();
        for (std::size_t i = 0; i < n; ++i) {
            force_[i] = -g_[i] * accV;
        }
        osc::stepModes<S, S>(v_.q.data(), v_.u.data(), v_.beta.data(), v_.kappa.data(),
                             v_.gamma.data(), force_.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            force_[i] = -g_[i] * accH;
        }
        osc::stepModes<S, S>(h_.q.data(), h_.u.data(), h_.beta.data(), h_.kappa.data(),
                             h_.gamma.data(), force_.data(), n);
    }

    /// Energy of the segment at time level n + 1/2 after `advance`, in float64:
    /// rhoA sum_n [ ((u_n + g_n dw)/dt)^2 / 2 + omegaHat_n^2 q_n^n q_n^{n+1} / 2 ] over both
    /// polarisations plus (T/L) w^n w^{n+1} / 2 per polarisation. `wVnext` and `dwV` are
    /// `BridgePoint::wV()` and `incrementV()` after the `advance` (likewise for h). This is the
    /// per-segment part of the conserved energy of the partitioned scheme (docs/dev/bridge.md).
    /// Thread: any. Allocates: no.
    [[nodiscard]] double energy(double wVnext, double dwV, double wHnext,
                                double dwH) const noexcept {
        return polarisationEnergy(v_, wVnext, dwV) + polarisationEnergy(h_, wHnext, dwH);
    }

private:
    struct Polarisation_ {
        std::vector<S> beta, kappa, gamma, q, u;
        std::vector<double> omegaHat2;
    };

    template <class Coeffs>
    static void fillCoefficients(const std::vector<Coeffs>& osc, double dt, Polarisation_& out) {
        const std::size_t n = osc.size();
        out.beta.assign(n, S{});
        out.kappa.assign(n, S{});
        out.gamma.assign(n, S{});
        out.omegaHat2.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            // SPEC-OSC Difference form from the stored float64 omegaHat^2 and sigmaHat.
            const double sd = osc[i].sigmaHat * dt;
            const double denom = 1.0 + sd;
            out.beta[i] = static_cast<S>((1.0 - sd) / denom);
            out.kappa[i] = static_cast<S>(osc[i].omegaHat2 * dt * dt / denom);
            out.gamma[i] = static_cast<S>(dt * dt / denom);
            out.omegaHat2[i] = osc[i].omegaHat2;
        }
    }

    [[nodiscard]] double polarisationEnergy(const Polarisation_& p, double wNext,
                                            double dw) const noexcept {
        double sum = 0.0;
        for (std::size_t i = 0; i < g_.size(); ++i) {
            const auto qNext = static_cast<double>(p.q[i]);
            const auto uNext = static_cast<double>(p.u[i]);
            const double v = (uNext + static_cast<double>(g_[i]) * dw) / dt_;
            sum += 0.5 * v * v + 0.5 * p.omegaHat2[i] * (qNext - uNext) * qNext;
        }
        return rhoA_ * sum + 0.5 * tOverL_ * (wNext - dw) * wNext;
    }

    std::vector<S> g_, aV_, aH_, force_;
    Polarisation_ v_, h_;
    double rhoA_{0.0};
    double tOverL_{0.0};
    double dt_{1.0};
    BridgeEnd end_{BridgeEnd::AtL};
};

}  // namespace pme::bridge
