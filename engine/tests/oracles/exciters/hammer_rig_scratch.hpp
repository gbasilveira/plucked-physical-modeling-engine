// SPDX-License-Identifier: MIT
// Scratch backend of the A5 oracle rig: a struck note written out from the specification alone.
// It is NOT the code under test. It reuses only A3's generic rank-r solve (pme::nl::savStep and
// SavSegment, merged and oracle-tested before A5) and the A1 coefficient compiler; the hammer
// potential, its gradient, the Hunt-Crossley dashpots, the una corda map, the launch, the bridge
// point and the energy functional are written here from SPEC. Paragraph references:
//
//   SPEC-NL "Coordinates": z = [modes of string 0, modes of string 1, ..., x_H]; masses rhoA for
//       string modes, m_H for the hammer; the hammer is a free lumped mass (beta = 1, kappa = 0).
//   SPEC-HAM "Contact with each struck string":
//       delta_s = x_H - s_E sum_n phibar_(s,n) q_(s,n),
//       V_H = sum_s w_s K_s / (p_s + 1) [delta_s]_+^(p_s + 1)
//   SPEC-HAM "Felt dissipation": dashpot per struck string along c_s = d delta_s / d z,
//       R_s^n = w_s K_s mu_HC [delta_s^n]_+^(p_s)
//   SPEC-HAM "Una corda (grand)": w and K_s(s), p_s(s) from hammer_oracle_common.hpp.
//   SPEC-HAM "Velocity map": the launch gap is v_H dt, so contact starts on the next sample.
//   SPEC-BRG "Bridge force", "Discrete coupling order", "Polarisation coupling" (SI-0019):
//       F_b,s = sum_n a_n q_n - (T/L) w_b, a_n = rhoA omegaHat_n^2 g_n; order: forces at
//       (q^n, w^n), bridge advance, modal force -rhoA g_n delta_tt w^n (newtons per coordinate,
//       because savStep divides by the mass), segment advance.
//   SPEC-BODY "Local bridge model": a mass-spring-damper is one mode, Omega = sqrt(k_b / m),
//       sigma = r_b / (2 m), Phi = 1 / sqrt(m).
//   docs/dev/bridge.md "Energy of the partitioned scheme": the energy functional of energy().
//   SPEC-NL "Discrete scheme": conserved energy = sum m_i H_i + psi^2 / 2 - eps0, with
//       SPEC-OSC "Discrete energy" H^(n+1/2) = (u^(n+1) / dt)^2 / 2 + omegaHat^2 q^n q^(n+1) / 2.
//
// The horizontal polarisation of every string is a second SavSegment without potentials (same
// tables: detuneH = 0), driven by the bridge through the rotation theta_b.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/osc/oscillator.hpp"

namespace pme::oracle::ham {

template <class S>
class ScratchRig {
public:
    using Scalar = S;

    explicit ScratchRig(const NoteSetup& cfg)
        : cfg_(cfg),
          dt_(1.0 / cfg.fs),
          cosT_(std::cos(cfg.thetaB)),
          sinT_(std::sin(cfg.thetaB)),
          ns_(cfg.strings.size()) {
        const std::vector<double> weights =
            ns_ > 0 ? strikeWeights(ns_, cfg.una, cfg.shift) : std::vector<double>{1.0};
        const double kEff =
            ns_ > 0 ? feltStiffness(cfg.hammer.K, cfg.shift, cfg.una) : cfg.hammer.K;
        const double pEff = ns_ > 0 ? feltExponent(cfg.hammer.p, cfg.shift, cfg.una) : cfg.hammer.p;
        // Coordinates: modes of every string in order, then the hammer.
        std::size_t total = 0;
        offsets_.resize(ns_);
        for (std::size_t s = 0; s < ns_; ++s) {
            offsets_[s] = total;
            total += cfg.strings[s].omega.size();
        }
        total_ = total;
        hammerIndex_ = total;
        nz_ = total + 1;
        // Contacts (only strings with a non-zero strike weight take part in V_H).
        if (ns_ == 0) {
            contacts_.push_back({0, 0, {}, 1.0, kEff, pEff, 0});
        }
        for (std::size_t s = 0; s < ns_; ++s) {
            if (weights[s] > 0.0) {
                const double x0 = cfg.hammer.strikeRatio * cfg.strings[s].L;
                contacts_.push_back({offsets_[s], cfg.strings[s].omega.size(),
                                     windowedShape(cfg.strings[s], x0, cfg.hammer.width),
                                     weights[s], kEff, pEff, s});
            }
        }
        const bool useDashpots = cfg.hammer.muHC > 0.0;
        const std::size_t nDash = useDashpots ? contacts_.size() : 0;
        pme::nl::SavParams params;
        params.eps0 = cfg.eps0;
        params.driftRelaxation = cfg.driftRelaxation;
        ok_ = nDash <= pme::nl::kMaxDashpots;
        ok_ = ok_ && segV_.configure(total, 1, nDash, cfg.fs, params) == pme::Status::Ok;
        if (ns_ > 0) {
            ok_ = ok_ && segH_.configure(total, 0, 0, cfg.fs, params) == pme::Status::Ok;
        }
        if (!ok_) {
            return;
        }
        // Oscillators of the string modes (float64 coefficients, rounded to S by the segment).
        g_.resize(ns_);
        a_.resize(ns_);
        omegaHat2_.resize(ns_);
        for (std::size_t s = 0; s < ns_; ++s) {
            const pme::StringModes& m = cfg.strings[s];
            g_[s] = bridgeG(m);
            a_[s].resize(m.omega.size());
            omegaHat2_[s].resize(m.omega.size());
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                pme::osc::OscCoeffsF64 c;
                ok_ = ok_ && pme::osc::compileOscillatorF64(m.omega[i], m.sigma[i], cfg.fs, c) ==
                                 pme::Status::Ok;
                ok_ = ok_ && segV_.setCoordinate(offsets_[s] + i, c, m.rhoA) == pme::Status::Ok;
                ok_ = ok_ && segH_.setCoordinate(offsets_[s] + i, c, m.rhoA) == pme::Status::Ok;
                omegaHat2_[s][i] = c.omegaHat2;
                a_[s][i] = m.rhoA * c.omegaHat2 * g_[s][i];  // SPEC-BRG "Bridge force"
            }
        }
        ok_ = ok_ && segV_.setLumped(hammerIndex_, cfg.hammer.mass) == pme::Status::Ok;
        // Bridge point: one lumped mode (SPEC-BODY), double precision.
        if (cfg.bridge.lumped) {
            const double omega = std::sqrt(cfg.bridge.stiffness / cfg.bridge.mass);
            const double sigma = cfg.bridge.damping / (2.0 * cfg.bridge.mass);
            pme::osc::OscCoeffsF64 c;
            ok_ = ok_ && pme::osc::compileOscillatorF64(omega, sigma, cfg.fs, c) == pme::Status::Ok;
            bBeta_ = c.beta;
            bKappa_ = c.kappa;
            bGamma_ = c.gamma;
            bOmegaHat2_ = c.omegaHat2;
            bPhi_ = 1.0 / std::sqrt(cfg.bridge.mass);
            hasBridgeMode_ = true;
        }
        // Dashpot directions c_s over the stacked coordinates, in S.
        dashC_.assign(contacts_.size(), std::vector<S>(nz_, S{0}));
        for (std::size_t k = 0; k < contacts_.size(); ++k) {
            dashC_[k][hammerIndex_] = S{1};
            for (std::size_t i = 0; i < contacts_[k].count; ++i) {
                dashC_[k][contacts_[k].offset + i] =
                    static_cast<S>(-cfg.approachSign * contacts_[k].phi[i]);
            }
        }
        dash_.resize(nDash);
        for (std::size_t k = 0; k < nDash; ++k) {
            dash_[k].c = std::span<const S>(dashC_[k]);
            dash_[k].R = 0.0;
            dash_[k].alongGradient = (contacts_.size() == 1);
        }
        forceV_.assign(nz_, S{0});
        forceH_.assign(total, S{0});
        fVs_.assign(ns_, 0.0);
        pot_ = std::make_unique<Pot>(*this);
        // Launch (SPEC-HAM "Velocity map"): gap v_H dt from the string, moving at v_H.
        segV_.resetState();
        segV_.q()[hammerIndex_] = static_cast<S>(-cfg.hammerSpeed * dt_);
        segV_.u()[hammerIndex_] = static_cast<S>(cfg.hammerSpeed * dt_);
        const std::array<const pme::nl::Potential<S>*, 1> pots{pot_.get()};
        segV_.initialisePsi(std::span<const pme::nl::Potential<S>* const>(pots.data(), 1));
        if (ns_ > 0) {
            segH_.resetState();
        }
        psiPrev_ = segV_.psi();
    }

    ScratchRig(const ScratchRig&) = delete;
    ScratchRig& operator=(const ScratchRig&) = delete;
    ScratchRig(ScratchRig&&) = delete;
    ScratchRig& operator=(ScratchRig&&) = delete;
    ~ScratchRig() = default;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] double penetration() const { return maxDelta_; }
    [[nodiscard]] double contactForce() const { return contactForce_; }
    [[nodiscard]] double hammerPosition() const {
        return static_cast<double>(segV_.q()[hammerIndex_]);
    }
    [[nodiscard]] double hammerSpeed() const {
        return static_cast<double>(segV_.u()[hammerIndex_]) / dt_;
    }
    [[nodiscard]] double psi() const { return segV_.psi(); }
    [[nodiscard]] double bridgeForceV(std::size_t s) const { return fVs_[s]; }
    [[nodiscard]] double bridgeW() const { return wV_; }
    [[nodiscard]] int resets() const { return static_cast<int>(segV_.counters().resets); }
    /// Energy removed so far by the reset-at-separation rule: sum of (psi_before^2 - 2 eps0) / 2.
    [[nodiscard]] double resetEnergyRemoved() const { return resetRemoved_; }
    [[nodiscard]] std::size_t strings() const { return ns_; }

    /// One sample n -> n + 1 in the order of SPEC-EXEC / SPEC-BRG "Discrete coupling order".
    void step() {
        // Step 2: bridge forces of every string from (q^n, w^n), both polarisations.
        double fV = 0.0;
        double fH = 0.0;
        for (std::size_t s = 0; s < ns_; ++s) {
            double accV = 0.0;
            double accH = 0.0;
            const double tOverL = cfg_.strings[s].tension / cfg_.strings[s].L;
            for (std::size_t i = 0; i < a_[s].size(); ++i) {
                accV += a_[s][i] * static_cast<double>(segV_.q()[offsets_[s] + i]);
                accH += a_[s][i] * static_cast<double>(segH_.q()[offsets_[s] + i]);
            }
            fVs_[s] = accV - tOverL * wV_;
            fV += fVs_[s];
            fH += accH - tOverL * wH_;
        }
        // Steps 3 and 4: the bridge point advances; SPEC-BRG "Polarisation coupling" with SI-0019.
        const double fPar = cosT_ * fV - sinT_ * fH;
        if (hasBridgeMode_) {
            bQPrev_ = bQ_;
            const double uNext = bBeta_ * bU_ - bKappa_ * bQ_ + bGamma_ * bPhi_ * fPar;
            bU_ = uNext;
            bQ_ += uNext;
        }
        const double wPar = bPhi_ * bQ_;
        const double wVNext = cosT_ * wPar;
        const double wHNext = -sinT_ * wPar;
        const double dttV = (wVNext - 2.0 * wV_ + wVPrev_) / (dt_ * dt_);
        const double dttH = (wHNext - 2.0 * wH_ + wHPrev_) / (dt_ * dt_);
        wVPrev_ = wV_;
        wV_ = wVNext;
        wHPrev_ = wH_;
        wH_ = wHNext;
        // Step 5: explicit modal forces -rhoA g_n delta_tt w (newtons per coordinate).
        for (std::size_t s = 0; s < ns_; ++s) {
            for (std::size_t i = 0; i < g_[s].size(); ++i) {
                const double scale = -cfg_.strings[s].rhoA * g_[s][i];
                forceV_[offsets_[s] + i] = static_cast<S>(scale * dttV);
                forceH_[offsets_[s] + i] = static_cast<S>(scale * dttH);
            }
        }
        // Contact quantities at z^n: penetrations, g_H, Hunt-Crossley R.
        const std::span<const S> z(segV_.q(), nz_);
        double v = 0.0;
        double gradH = 0.0;
        maxDelta_ = -1.0e300;
        for (std::size_t k = 0; k < contacts_.size(); ++k) {
            const double d = pot_->delta(contacts_[k], z);
            maxDelta_ = std::max(maxDelta_, d);
            const double dPlus = d > 0.0 ? d : 0.0;
            v += contacts_[k].weight * contacts_[k].k / (contacts_[k].p + 1.0) *
                 std::pow(dPlus, contacts_[k].p + 1.0);
            gradH += contacts_[k].weight * contacts_[k].k * std::pow(dPlus, contacts_[k].p);
            if (!dash_.empty()) {
                dash_[k].R = contacts_[k].weight * contacts_[k].k * cfg_.hammer.muHC *
                             std::pow(dPlus, contacts_[k].p);
            }
        }
        const double gH = gradH / std::sqrt(2.0 * (v + cfg_.eps0));
        psiPrev_ = segV_.psi();
        const auto resetsBefore = segV_.counters().resets;
        const std::array<const pme::nl::Potential<S>*, 1> pots{pot_.get()};
        pme::nl::savStep<S>(segV_, std::span<const pme::nl::Potential<S>* const>(pots.data(), 1),
                            std::span<const pme::nl::Dashpot<S>>(dash_.data(), dash_.size()),
                            std::span<const S>(forceV_));
        contactForce_ = gH * 0.5 * (segV_.psi() + psiPrev_);
        if (segV_.counters().resets > resetsBefore) {
            resetRemoved_ += 0.5 * psiPrev_ * psiPrev_ - cfg_.eps0;
        }
        // Step 7: the horizontal polarisation is linear.
        if (ns_ > 0) {
            pme::nl::savStep<S>(segH_, {}, {}, std::span<const S>(forceH_));
        }
    }

    /// Energy functional after the step (state n + 1; w^n = wVPrev, w^(n+1) = wV): the sum over
    /// strings and both polarisations of rhoA/2 sum_n [((u_n + g_n dw) / dt)^2 + omegaHat_n^2
    /// q_n^n q_n^(n+1)] + (T/2L) w^n w^(n+1), the bridge mode (u/dt)^2/2 + omegaHat_b^2 r^n
    /// r^(n+1)/2, the hammer m_H (u_H/dt)^2/2, and psi^2/2 - eps0. The perpendicular bridge
    /// axis is a rigid conductance G_h = 0 and carries no energy.
    [[nodiscard]] double energy() const {
        double e = 0.0;
        for (std::size_t s = 0; s < ns_; ++s) {
            const pme::StringModes& m = cfg_.strings[s];
            const double dwV = wV_ - wVPrev_;
            const double dwH = wH_ - wHPrev_;
            double sum = 0.0;
            for (std::size_t i = 0; i < g_[s].size(); ++i) {
                const std::size_t idx = offsets_[s] + i;
                const double uV = static_cast<double>(segV_.u()[idx]) + g_[s][i] * dwV;
                const double uH = static_cast<double>(segH_.u()[idx]) + g_[s][i] * dwH;
                sum += (uV * uV + uH * uH) / (dt_ * dt_) +
                       omegaHat2_[s][i] * (static_cast<double>(segV_.qPrev()[idx]) *
                                               static_cast<double>(segV_.q()[idx]) +
                                           static_cast<double>(segH_.qPrev()[idx]) *
                                               static_cast<double>(segH_.q()[idx]));
            }
            e += 0.5 * m.rhoA * sum + 0.5 * (m.tension / m.L) * (wVPrev_ * wV_ + wHPrev_ * wH_);
        }
        if (hasBridgeMode_) {
            e += 0.5 * (bU_ / dt_) * (bU_ / dt_) + 0.5 * bOmegaHat2_ * bQPrev_ * bQ_;
        }
        const double uh = static_cast<double>(segV_.u()[hammerIndex_]) / dt_;
        e += 0.5 * cfg_.hammer.mass * uh * uh + 0.5 * segV_.psi() * segV_.psi() - cfg_.eps0;
        return e;
    }

private:
    struct Contact {
        std::size_t offset{0};
        std::size_t count{0};
        std::vector<double> phi;  ///< window-averaged mode values (empty for the fixed target)
        double weight{1.0};
        double k{0.0};
        double p{0.0};
        std::size_t string{0};
    };

    /// V_H of SPEC-HAM and its gradient over the stacked coordinates (SPEC-NL "Contracts").
    class Pot final : public pme::nl::Potential<S> {
    public:
        explicit Pot(const ScratchRig& rig) : rig_(&rig) {}
        [[nodiscard]] double delta(const Contact& c, std::span<const S> z) const {
            double sum = 0.0;
            for (std::size_t i = 0; i < c.count; ++i) {
                sum += c.phi[i] * static_cast<double>(z[c.offset + i]);
            }
            return static_cast<double>(z[rig_->hammerIndex_]) - rig_->cfg_.approachSign * sum;
        }
        [[nodiscard]] double value(std::span<const S> z) const noexcept override {
            double v = 0.0;
            for (const Contact& c : rig_->contacts_) {
                const double d = delta(c, z);
                if (d > 0.0) {
                    v += c.weight * c.k / (c.p + 1.0) * std::pow(d, c.p + 1.0);
                }
            }
            return v;
        }
        void gradient(std::span<const S> z, std::span<double> grad) const noexcept override {
            for (double& g : grad) {
                g = 0.0;
            }
            for (const Contact& c : rig_->contacts_) {
                const double d = delta(c, z);
                if (d > 0.0) {
                    const double f = c.weight * c.k * std::pow(d, c.p);
                    grad[rig_->hammerIndex_] += f;
                    for (std::size_t i = 0; i < c.count; ++i) {
                        grad[c.offset + i] -= rig_->cfg_.approachSign * c.phi[i] * f;
                    }
                }
            }
        }

    private:
        const ScratchRig* rig_;
    };

    NoteSetup cfg_;
    double dt_;
    double cosT_;
    double sinT_;
    std::size_t ns_;
    bool ok_{true};
    std::size_t total_{0};
    std::size_t hammerIndex_{0};
    std::size_t nz_{0};
    std::vector<std::size_t> offsets_;
    std::vector<Contact> contacts_;
    std::vector<std::vector<double>> g_, a_, omegaHat2_;
    std::vector<std::vector<S>> dashC_;
    std::vector<pme::nl::Dashpot<S>> dash_;
    std::vector<S> forceV_, forceH_;
    std::vector<double> fVs_;
    pme::nl::SavSegment<S> segV_;
    pme::nl::SavSegment<S> segH_;
    std::unique_ptr<Pot> pot_;
    bool hasBridgeMode_{false};
    double bBeta_{0.0}, bKappa_{0.0}, bGamma_{0.0}, bOmegaHat2_{0.0}, bPhi_{0.0};
    double bQ_{0.0}, bU_{0.0}, bQPrev_{0.0};
    double wV_{0.0}, wVPrev_{0.0}, wH_{0.0}, wHPrev_{0.0};
    double psiPrev_{0.0};
    double maxDelta_{0.0};
    double contactForce_{0.0};
    double resetRemoved_{0.0};
};

}  // namespace pme::oracle::ham
