// SPDX-License-Identifier: MIT
// A5 backend of the oracle rig: the same observable interface as ScratchRig (hammer_rig.hpp), but
// the note is advanced by the code under test, the A5 `pme::exciters::StruckNote<S>` with its
// `HammerPotential` and `HammerFelt` (engine/src/exciters/include/pme/exciters/hammer.hpp) and the
// A4 `BridgePoint`. Compiled with PME_ORACLE_HAMMER_A5 (CMake PME_A5_ORACLE_BACKEND = AUTO once
// the A5 header exists, or A5).
//
// Written against the A5 public header only. Mapping:
//   * StruckNoteDesc{strings, end, hammer, unaCorda, pedal, approachSign, shank, sav};
//     configure(desc, fs, BridgePoint<S>*); launch(nu, shift) with nu inverted from the velocity
//     map v_H = v_min + (v_max - v_min) nu^gamma_v so that the hammer arrives at the requested
//     speed; step(); segment() (SavSegment: q, qPrev, u, omegaHat2, mass, psi, counters; the
//     hammer is the last coordinate, no shank here); potential(); releasedEnergy();
//     resetEnergyRemoved().
//   * Strings are struck in the order given; the una corda weights come from A5's strike map
//     (UnaCordaParams::leavingString). A string with weight 0 stays in the note.
//   * No strings: A5's test-only RigidFeltPotential (the fixed rigid target of SPEC-HAM "Oracle")
//     on one free lumped coordinate through savStep.
// Not taken from A5, so the oracles stay independent: the energy functional (rebuilt from the raw
// state, the same formula as ScratchRig::energy(), with A5's releasedEnergy() added for a hammer
// that has left the coordinate set), the per-string bridge forces (SPEC-BRG formula from the
// state before the step), the penetration (A5's potential at z^n) and the SAV contact force
// g_H (psi^(n+1/2) + psi^(n-1/2)) / 2 (formed here from the potential's value and gradient).
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/exciters/hammer.hpp"

namespace pme::oracle::ham {

/// Stand-in window width for a point contact (see the FINDING in initNote()).
inline constexpr double kPointStandInWidth = 1.0e-6;

template <class S>
class A5Rig {
public:
    using Scalar = S;

    explicit A5Rig(const NoteSetup& cfg) : cfg_(cfg), dt_(1.0 / cfg.fs) {
        // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): needs the members below
        ok_ = cfg_.strings.empty() ? initFixedTarget() : initNote();
    }
    A5Rig(const A5Rig&) = delete;
    A5Rig& operator=(const A5Rig&) = delete;
    A5Rig(A5Rig&&) = delete;
    A5Rig& operator=(A5Rig&&) = delete;
    ~A5Rig() = default;

    [[nodiscard]] bool ok() const { return ok_; }

    void step() {
        if (cfg_.strings.empty()) {
            stepFixed();
            return;
        }
        const pme::nl::SavSegment<S>& seg = note_.segment();
        const std::span<const S> z(seg.q(), seg.size());
        // Contact quantities at z^n, from A5's potential.
        const pme::exciters::HammerPotential<S>& pot = note_.potential();
        maxDelta_ = pot.maxPenetration(z);
        const double v = pot.value(z);
        const double gradH = pot.hammerGradient(z);
        const double gH = gradH / std::sqrt(2.0 * (v + cfg_.eps0));
        psiPrev_ = seg.psi();
        // Bridge forces of every string at (q^n, w^n), SPEC-BRG "Bridge force".
        const double wV = bridge_ ? static_cast<double>(point_.wV()) : 0.0;
        const double wH = bridge_ ? static_cast<double>(point_.wH()) : 0.0;
        for (std::size_t s = 0; s < cfg_.strings.size(); ++s) {
            const pme::StringModes& m = cfg_.strings[s];
            const std::size_t off = note_.modeOffset(s);
            double acc = 0.0;
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                acc += m.rhoA * seg.omegaHat2()[off + i] * g_[s][i] *
                       static_cast<double>(seg.q()[off + i]);
            }
            fVs_[s] = acc - m.tension / m.L * wV;
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                qHPrev_[s][i] = note_.qH(s)[i];
            }
        }
        wVPrev_ = wV;
        wHPrev_ = wH;
        note_.step();
        contactForce_ = gH * 0.5 * (note_.segment().psi() + psiPrev_);
    }

    [[nodiscard]] double penetration() const { return maxDelta_; }
    [[nodiscard]] double contactForce() const { return contactForce_; }
    [[nodiscard]] double hammerPosition() const {
        return cfg_.strings.empty() ? static_cast<double>(seg_.q()[0]) : note_.headPosition();
    }
    [[nodiscard]] double hammerSpeed() const {
        return cfg_.strings.empty() ? static_cast<double>(seg_.u()[0]) / dt_ : note_.headVelocity();
    }
    [[nodiscard]] double psi() const {
        return cfg_.strings.empty() ? seg_.psi() : note_.segment().psi();
    }
    [[nodiscard]] double bridgeForceV(std::size_t s) const { return fVs_[s]; }
    [[nodiscard]] double bridgeW() const {
        return bridge_ ? static_cast<double>(point_.wV()) : 0.0;
    }
    [[nodiscard]] int resets() const {
        const auto& seg = cfg_.strings.empty() ? seg_ : note_.segment();
        return static_cast<int>(seg.counters().resets);
    }
    [[nodiscard]] double resetEnergyRemoved() const {
        return cfg_.strings.empty() ? 0.0 : note_.resetEnergyRemoved();
    }
    [[nodiscard]] std::size_t strings() const { return cfg_.strings.size(); }

    /// Energy functional after the step, rebuilt from raw state (see the header comment).
    [[nodiscard]] double energy() const {
        const pme::nl::SavSegment<S>& seg = cfg_.strings.empty() ? seg_ : note_.segment();
        const double eps0 = cfg_.eps0;
        const std::size_t h = seg.size() - 1;
        const double uh = static_cast<double>(seg.u()[h]) / dt_;
        double e = 0.5 * cfg_.hammer.mass * uh * uh + 0.5 * seg.psi() * seg.psi() - eps0;
        if (cfg_.strings.empty()) {
            return e;
        }
        e += note_.releasedEnergy();
        const double wV = bridge_ ? static_cast<double>(point_.wV()) : 0.0;
        const double wH = bridge_ ? static_cast<double>(point_.wH()) : 0.0;
        const double dwV = wV - wVPrev_;
        const double dwH = wH - wHPrev_;
        for (std::size_t s = 0; s < cfg_.strings.size(); ++s) {
            const pme::StringModes& m = cfg_.strings[s];
            const std::size_t off = note_.modeOffset(s);
            double sum = 0.0;
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                const double uV = static_cast<double>(seg.u()[off + i]) + g_[s][i] * dwV;
                const auto qH = static_cast<double>(note_.qH(s)[i]);
                const auto qHp = static_cast<double>(qHPrev_[s][i]);
                const double uH = (qH - qHp) + g_[s][i] * dwH;
                const double w2 = seg.omegaHat2()[off + i];
                sum += (uV * uV + uH * uH) / (dt_ * dt_) +
                       w2 * (static_cast<double>(seg.qPrev()[off + i]) *
                                 static_cast<double>(seg.q()[off + i]) +
                             qHp * qH);
            }
            e += 0.5 * m.rhoA * sum + 0.5 * (m.tension / m.L) * (wVPrev_ * wV + wHPrev_ * wH);
        }
        if (bridge_) {
            e += point_.modeEnergy();
        }
        return e;
    }

private:
    bool initNote() {
        namespace ex = pme::exciters;
        ex::StruckNoteDesc d;
        for (const pme::StringModes& m : cfg_.strings) {
            d.strings.push_back(&m);
        }
        d.end = pme::BridgeEnd::AtL;
        d.hammer = cfg_.hammer;
        // FINDING (A5-T2 point contact): A5's validate(HammerParams) requires width > 0, so
        // width = 0 (the point shape phi_n(x_c) of SPEC-STR, W = 1) is rejected. The adapter
        // substitutes b = 1 micrometre, whose window factor differs from 1 by under 1e-9 for every
        // mode below 20 kHz; the report flags the rejection.
        if (!(d.hammer.width > 0.0)) {
            d.hammer.width = kPointStandInWidth;
        }
        d.unaCorda = cfg_.una;
        d.pedal = ex::SoftPedal::UnaCorda;
        d.approachSign = cfg_.approachSign;
        d.sav.eps0 = cfg_.eps0;
        d.sav.driftRelaxation = cfg_.driftRelaxation;
        if (cfg_.bridge.lumped) {
            pme::BridgePointDesc bd;
            bd.thetaB = cfg_.thetaB;
            bd.conductanceH = 0.0;
            pme::bridge::ParallelModel model;
            if (pme::bridge::massSpringDamperModel(cfg_.bridge.mass, cfg_.bridge.stiffness,
                                                   cfg_.bridge.damping, model) != pme::Status::Ok ||
                point_.configure(bd, model, cfg_.fs) != pme::Status::Ok) {
                return false;
            }
            bridge_ = true;
        }
        if (note_.configure(d, cfg_.fs, bridge_ ? &point_ : nullptr) != pme::Status::Ok) {
            return false;
        }
        const std::size_t ns = cfg_.strings.size();
        g_.resize(ns);
        qHPrev_.resize(ns);
        fVs_.assign(ns, 0.0);
        for (std::size_t s = 0; s < ns; ++s) {
            g_[s] = bridgeG(cfg_.strings[s]);
            qHPrev_[s].assign(cfg_.strings[s].omega.size(), S{0});
        }
        // Invert the velocity map (SPEC-HAM "Velocity map") for the requested hammer speed.
        const pme::HammerParams& hp = cfg_.hammer;
        const double nu =
            std::pow((cfg_.hammerSpeed - hp.vMin) / (hp.vMax - hp.vMin), 1.0 / hp.gammaV);
        return note_.launch(nu, cfg_.shift) == pme::Status::Ok;
    }

    bool initFixedTarget() {
        pme::nl::SavParams params;
        params.eps0 = cfg_.eps0;
        params.driftRelaxation = cfg_.driftRelaxation;
        bool ok = seg_.configure(0, 1, 0, cfg_.fs, params) == pme::Status::Ok;
        ok = ok && seg_.setLumped(0, cfg_.hammer.mass) == pme::Status::Ok;
        rigid_ = std::make_unique<pme::exciters::RigidFeltPotential<S>>(cfg_.hammer.K,
                                                                        cfg_.hammer.p, 0, 0.0);
        seg_.resetState();
        seg_.q()[0] = static_cast<S>(-cfg_.hammerSpeed * dt_);
        seg_.u()[0] = static_cast<S>(cfg_.hammerSpeed * dt_);
        const std::array<const pme::nl::Potential<S>*, 1> pots{rigid_.get()};
        seg_.initialisePsi(std::span<const pme::nl::Potential<S>* const>(pots.data(), 1));
        return ok;
    }

    void stepFixed() {
        maxDelta_ = static_cast<double>(seg_.q()[0]);
        const std::array<const pme::nl::Potential<S>*, 1> pots{rigid_.get()};
        pme::nl::savStep<S>(seg_, std::span<const pme::nl::Potential<S>* const>(pots.data(), 1), {},
                            {});
    }

    NoteSetup cfg_;
    double dt_;
    bool ok_{false};
    bool bridge_{false};
    pme::bridge::BridgePoint<S> point_;
    pme::exciters::StruckNote<S> note_;
    pme::nl::SavSegment<S> seg_;  // fixed-target variant
    std::unique_ptr<pme::exciters::RigidFeltPotential<S>> rigid_;
    std::vector<std::vector<double>> g_;
    std::vector<std::vector<S>> qHPrev_;
    std::vector<double> fVs_;
    double wVPrev_{0.0};
    double wHPrev_{0.0};
    double psiPrev_{0.0};
    double maxDelta_{0.0};
    double contactForce_{0.0};
};

}  // namespace pme::oracle::ham
