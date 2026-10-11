// SPDX-License-Identifier: MIT
// BridgePoint (A4-R2 to A4-R5): the bridge model of one bridge point, shared by all attached
// segments, with polarisation rotation and the sympathetic / longitudinal hooks.
//
// SPEC sections implemented: SPEC-BRG "Discrete coupling order", "Unison coupling",
// "Polarisation coupling"; SPEC-BODY "Local bridge model", "Sympathetic path"; SPEC-EXEC
// per-sample steps 2 to 4; SPEC-GEO "Transmission to the body" (hook only); SPEC-OSC "Difference
// form" and "Discrete energy" (bridge modes). Time convention e^{+i omega t} (SI-0012).
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/bridge/model.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/status.hpp"

namespace pme::bridge {

/// Where a `BridgePoint` is in the per-sample coupling order (SPEC-BRG "Discrete coupling
/// order"): segments add forces (Collecting), the point advances (Advanced), segments read the
/// accelerations. The next `addForce` / `addVerticalForce` returns it to Collecting.
enum class Stage : unsigned char { Collecting, Advanced };

/// One bridge point. `S` is the run-time state type (float in the engine, double in energy
/// tests). All per-sample members are `noexcept` and allocation free; `configure` allocates and
/// runs on a non-audio thread.
///
/// Per-sample use (SPEC-EXEC steps 2 to 4):
///   1. each segment s computes F_s from q^n and the point's `wV()` / `wH()` (= w^n), calls
///      `addForce(F_v, F_h)` in segment-index order (and `addVerticalForce` for the SPEC-GEO
///      hook);
///   2. `advance()` rotates, advances the parallel model and the perpendicular conductance with
///      the forces at step n to w^{n+1}, adds the external motion and rotates back;
///   3. each segment reads `accelV()` / `accelH()` (= delta_tt w^n) and applies -g_n delta_tt w^n.
template <class S>
class BridgePoint {
public:
    /// Compile the point at sample rate `fs` (SPEC-BODY "Contracts": the oscillator coefficients
    /// of the bridge modes are derived here). All states are zeroed. `InvalidArgument` for fs <=
    /// 0 or not finite, thetaB not finite, conductanceH < 0 or not finite, or an invalid mode in
    /// `model`; `Rejected` if a mode of a `MassSpringDamper` model fails the SPEC-OSC bandwidth
    /// rule (modes of a `ModalSet` that fail it are dropped, `droppedModes()`). The point is
    /// unchanged unless the result is `Ok`. Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const BridgePointDesc& desc, const ParallelModel& model,
                                   double fs) {
        if (!(std::isfinite(fs) && fs > 0.0) || !std::isfinite(desc.thetaB) ||
            !(std::isfinite(desc.conductanceH) && desc.conductanceH >= 0.0) ||
            !(std::isfinite(model.conductance) && model.conductance >= 0.0)) {
            return Status::InvalidArgument;
        }
        const double dt = 1.0 / fs;
        std::vector<osc::OscCoeffsF64> coeffs;
        std::vector<double> phis;
        std::size_t dropped = 0;
        for (const BridgeMode& m : model.modes) {
            if (!(std::isfinite(m.omega) && m.omega >= 0.0) ||
                !(std::isfinite(m.sigma) && m.sigma >= 0.0) || !std::isfinite(m.phi)) {
                return Status::InvalidArgument;
            }
            osc::OscCoeffsF64 c;
            const Status s = compileBridgeMode(m.omega, m.sigma, fs, c);
            if (s == Status::Ok) {
                coeffs.push_back(c);
                phis.push_back(m.phi);
            } else if (s == Status::Rejected && model.kind == ParallelModel::Kind::ModalSet) {
                ++dropped;  // SPEC-OSC Bandwidth: a rejected mode does not exist
            } else {
                return s;
            }
        }
        const std::size_t count = coeffs.size();
        beta_.assign(count, S{});
        kappa_.assign(count, S{});
        gamma_.assign(count, S{});
        phi_.assign(count, S{});
        omegaHat2_.assign(count, 0.0);
        r_.assign(count, S{});
        u_.assign(count, S{});
        force_.assign(count, S{});
        for (std::size_t i = 0; i < count; ++i) {
            beta_[i] = static_cast<S>(coeffs[i].beta);
            kappa_[i] = static_cast<S>(coeffs[i].kappa);
            gamma_[i] = static_cast<S>(coeffs[i].gamma);
            phi_[i] = static_cast<S>(phis[i]);
            omegaHat2_[i] = coeffs[i].omegaHat2;
        }
        dropped_ = dropped;
        dt_ = dt;
        invDt2_ = static_cast<S>(1.0 / (dt * dt));
        dtS_ = static_cast<S>(dt);
        gPar_ = static_cast<S>(dt * model.conductance);   // SPEC-BODY: w_R += dt G F
        gPerp_ = static_cast<S>(dt * desc.conductanceH);  // SPEC-BRG: w_perp += dt G_h F_perp
        conductance_ = model.conductance;
        conductanceH_ = desc.conductanceH;
        // R(theta_b): bridge-frame axes rotated by theta_b from the string polarisations
        // (SPEC-BRG "Polarisation coupling"); theta_b = 0 gives exactly c = 1, s = 0.
        cosTheta_ = static_cast<S>(std::cos(desc.thetaB));
        sinTheta_ = static_cast<S>(std::sin(desc.thetaB));
        reset();
        return Status::Ok;
    }

    /// Zero all states (bridge modes, conductance displacements, external motion, history) and
    /// the accumulators. Hooks (`setExternalVelocity`) are cleared too. Thread: audio. No alloc.
    void reset() noexcept {
        for (std::size_t i = 0; i < r_.size(); ++i) {
            r_[i] = S{};
            u_[i] = S{};
        }
        wR_ = wExt_ = wPerp_ = S{};
        vExt_ = S{};
        fv_ = fh_ = S{};
        wPar_ = S{};
        wV_ = wH_ = S{};
        dwV_ = dwH_ = S{};
        accV_ = accH_ = S{};
        prevDwPar_ = prevDwPerp_ = S{};
        prevFPar_ = prevFPerp_ = S{};
        dissipatedPar_ = dissipatedPerp_ = 0.0;
        stage_ = Stage::Collecting;
    }

    /// Add one segment's bridge force for the current sample: F_b,s for the vertical and the
    /// horizontal polarisation (SPEC-BRG "Discrete coupling order" step 1). Call once per
    /// segment in segment-index order: the sums are formed in call order, so the result is
    /// bit-reproducible for a fixed order (SPEC-BRG "Unison coupling"). Thread: audio.
    void addForce(S fv, S fh) noexcept {
        fv_ += fv;
        fh_ += fh;
        stage_ = Stage::Collecting;
    }

    /// Hook (A4-R5, SPEC-GEO "Transmission to the body", SPEC-EXEC step 2): extra vertical force
    /// kappa_L F_L + kappa_T dT of one segment, added to F_v. Inert unless called. Thread: audio.
    void addVerticalForce(S f) noexcept {
        fv_ += f;
        stage_ = Stage::Collecting;
    }

    /// Hook (A4-R5, SPEC-BODY "Sympathetic path"): external bridge velocity v_ext^n on the
    /// parallel axis, integrated as w_ext^{n+1} = w_ext^n + dt v_ext^n in `advance`. Persists
    /// until changed; 0 (the default) is inert. Thread: audio.
    void setExternalVelocity(S vExt) noexcept { vExt_ = vExt; }

    /// Advance to w^{n+1} (SPEC-BRG "Discrete coupling order" step 2; SPEC-EXEC step 3) and form
    /// delta_tt w^n per string polarisation (SPEC-EXEC step 4). Clears the force accumulators.
    /// Thread: audio. Allocates: no.
    void advance() noexcept {
        // SPEC-BRG Polarisation coupling: [F_par; F_perp] = R(theta_b) [F_v; F_h].
        const S fPar = cosTheta_ * fv_ + sinTheta_ * fh_;
        const S fPerp = cosTheta_ * fh_ - sinTheta_ * fv_;
        const std::size_t m = r_.size();
        // SPEC-BODY Local bridge model: modes advance with the force at step n (explicit).
        for (std::size_t i = 0; i < m; ++i) {
            force_[i] = phi_[i] * fPar;
        }
        osc::stepModes<S, S>(r_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(),
                             force_.data(), m);
        S modeSum = S{};
        S modeDelta = S{};
        for (std::size_t i = 0; i < m; ++i) {
            modeSum += phi_[i] * r_[i];
            modeDelta += phi_[i] * u_[i];
        }
        const S dwR = gPar_ * fPar;          // SPEC-BODY: w_R^{n+1} - w_R^n = dt G F^n
        const S dwExt = dtS_ * vExt_;        // SPEC-BODY Sympathetic path: dt v_ext^n
        const S dwPerp = gPerp_ * fPerp;     // SPEC-BRG: w_perp^{n+1} - w_perp^n = dt G_h F_perp^n
        wR_ += dwR;
        wExt_ += dwExt;
        wPerp_ += dwPerp;
        const S dwPar = modeDelta + dwR + dwExt;
        wPar_ = modeSum + wR_ + wExt_;
        // delta_tt w^n from the increments (no cancellation of large w values).
        const S accPar = (dwPar - prevDwPar_) * invDt2_;
        const S accPerp = (dwPerp - prevDwPerp_) * invDt2_;
        prevDwPar_ = dwPar;
        prevDwPerp_ = dwPerp;
        // SPEC-BRG: [w_v; w_h] = R(theta_b)^T [w_par; w_perp].
        wV_ = cosTheta_ * wPar_ - sinTheta_ * wPerp_;
        wH_ = sinTheta_ * wPar_ + cosTheta_ * wPerp_;
        dwV_ = cosTheta_ * dwPar - sinTheta_ * dwPerp;
        dwH_ = sinTheta_ * dwPar + cosTheta_ * dwPerp;
        accV_ = cosTheta_ * accPar - sinTheta_ * accPerp;
        accH_ = sinTheta_ * accPar + cosTheta_ * accPerp;
        // Dissipated power of the conductances, F times the centred velocity of w_R
        // (G (F^n + F^{n-1}) / 2), accumulated as energy.
        dissipatedPar_ += dt_ * static_cast<double>(fPar) * 0.5 * conductance_ *
                          (static_cast<double>(fPar) + static_cast<double>(prevFPar_));
        dissipatedPerp_ += dt_ * static_cast<double>(fPerp) * 0.5 * conductanceH_ *
                           (static_cast<double>(fPerp) + static_cast<double>(prevFPerp_));
        prevFPar_ = fPar;
        prevFPerp_ = fPerp;
        fv_ = fh_ = S{};
        stage_ = Stage::Advanced;
    }

    /// Current bridge displacement w^n per string polarisation (after `advance`, the new w^{n+1}
    /// which is the w^n of the next sample), including the external motion. Thread: audio.
    [[nodiscard]] S wV() const noexcept { return wV_; }
    [[nodiscard]] S wH() const noexcept { return wH_; }
    /// delta_tt w^n = (w^{n+1} - 2 w^n + w^{n-1}) / dt^2 of the last `advance`, per string
    /// polarisation (SPEC-BRG "Discrete coupling order" step 3). Thread: audio.
    [[nodiscard]] S accelV() const noexcept { return accV_; }
    [[nodiscard]] S accelH() const noexcept { return accH_; }
    /// w^{n+1} - w^n of the last `advance`, per string polarisation (energy bookkeeping).
    [[nodiscard]] S incrementV() const noexcept { return dwV_; }
    [[nodiscard]] S incrementH() const noexcept { return dwH_; }
    /// Bridge-frame displacements: w_par (modes + w_R + w_ext) and w_perp.
    [[nodiscard]] S wParallel() const noexcept { return wPar_; }
    [[nodiscard]] S wPerpendicular() const noexcept { return wPerp_; }
    /// External-motion displacement w_ext (parallel axis).
    [[nodiscard]] S wExternal() const noexcept { return wExt_; }
    /// Stage of the per-sample coupling order.
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    /// Number of bridge modes M kept.
    [[nodiscard]] std::size_t modeCount() const noexcept { return r_.size(); }
    /// Modes of a `ModalSet` model dropped by the bandwidth rule at configure time.
    [[nodiscard]] std::size_t droppedModes() const noexcept { return dropped_; }
    /// Bridge-mode state access (tests, energy monitors).
    [[nodiscard]] const S* modeQ() const noexcept { return r_.data(); }
    [[nodiscard]] const S* modeU() const noexcept { return u_.data(); }

    /// Discrete energy of the bridge modes, SPEC-OSC "Discrete energy" per unit modal mass,
    /// H = sum_k [ (u_k/dt)^2 / 2 + omegaHat_k^2 q_k^n q_k^{n+1} / 2 ] with q^n = q - u, in
    /// float64, after `advance` (time level n + 1/2). Thread: any. Allocates: no.
    [[nodiscard]] double modeEnergy() const noexcept {
        double e = 0.0;
        for (std::size_t i = 0; i < r_.size(); ++i) {
            const double qNext = static_cast<double>(r_[i]);
            const double uNext = static_cast<double>(u_[i]);
            const double v = uNext / dt_;
            e += 0.5 * v * v + 0.5 * omegaHat2_[i] * (qNext - uNext) * qNext;
        }
        return e;
    }
    /// Energy absorbed so far by the parallel conductance G (and G_hf) and by the
    /// perpendicular conductance G_h: the integral of F times the centred velocity of the
    /// conductance displacement (float64). Always >= 0 for the sums of squares it is built from
    /// up to the half-step average. Thread: any. Allocates: no.
    [[nodiscard]] double conductanceDissipatedEnergy() const noexcept {
        return dissipatedPar_ + dissipatedPerp_;
    }
    /// Sample period dt (s).
    [[nodiscard]] double dt() const noexcept { return dt_; }

private:
    std::vector<S> beta_, kappa_, gamma_, phi_;
    std::vector<double> omegaHat2_;
    std::vector<S> r_, u_, force_;
    std::size_t dropped_{0};
    double dt_{1.0};
    double conductance_{0.0};
    double conductanceH_{0.0};
    S invDt2_{}, dtS_{}, gPar_{}, gPerp_{}, cosTheta_{1}, sinTheta_{0};
    S wR_{}, wExt_{}, wPerp_{}, vExt_{};
    S fv_{}, fh_{};
    S wPar_{}, wV_{}, wH_{}, dwV_{}, dwH_{}, accV_{}, accH_{};
    S prevDwPar_{}, prevDwPerp_{}, prevFPar_{}, prevFPerp_{};
    double dissipatedPar_{0.0}, dissipatedPerp_{0.0};
    Stage stage_{Stage::Collecting};
};

}  // namespace pme::bridge
