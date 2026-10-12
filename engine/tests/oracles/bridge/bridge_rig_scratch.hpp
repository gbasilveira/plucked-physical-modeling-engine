// SPDX-License-Identifier: MIT
// Scratch backend of the A4 oracle rig: the SPEC-BRG coupling written out from the specification
// alone, with no call into engine/src/bridge. It is NOT the code under test; it exists so that the
// oracle tests can be built and run before A4 is merged (and as a cross-check of the measurement
// chain afterwards). Everything below cites its SPEC paragraph.
//
//   SPEC-BRG "Modal equation with a moving support":
//       g_n = sqrt(2/L) (-1)^(n+1) / k_n   (bridge at x = L),   sqrt(2/L) / k_n   (bridge at x = 0)
//   SPEC-BRG "Bridge force":  a_n = rhoA omegaHat_n^2 g_n,   F_b = sum a_n q_n - (T/L) w_b
//   SPEC-BRG "Discrete coupling order":
//       1. F_b = sum over segments of F_b,s(q^n, w^n)
//       2. the bridge model advances with F_b to w^(n+1)
//       3. every segment advances its modes with the modal force  -g_n delta_tt w^n,
//          delta_tt w^n = (w^(n+1) - 2 w^n + w^(n-1)) / dt^2
//   SPEC-BODY "Local bridge model":  w_b = sum_k Phi_k r_k + w_R,  r_k driven by Phi_k F_b at step
//       n, w_R^(n+1) = w_R^n + dt G F_b^n.  A mass-spring-damper (m, k_b, r_b) is the one-mode
//       case Omega = sqrt(k_b/m), sigma = r_b/(2m), Phi = 1/sqrt(m).
//
// String modes run in float32 in the A1 ModeBank (the engine kernel); the bridge state is float64.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "bridge_rig.hpp"
#include "pme/osc.hpp"

namespace pme::oracle::brg {

class Rig {
public:
    explicit Rig(const RigSetup& setup)
        : dt_(1.0 / setup.fs),
          cosT_(std::cos(setup.thetaB)),
          sinT_(std::sin(setup.thetaB)),
          gH_(setup.conductanceH) {
        const std::size_t count = setup.segments.size();
        banks_.resize(2 * count);  // [2 s] vertical, [2 s + 1] horizontal
        g_.resize(count);
        aV_.resize(count);
        aH_.resize(count);
        tOverL_.resize(count);
        force_.resize(count);
        for (std::size_t s = 0; s < count; ++s) {
            const pme::StringModes& m = setup.segments[s];
            const std::size_t n = m.omega.size();
            banks_[2 * s].resize(n);
            banks_[2 * s + 1].resize(n);
            g_[s].resize(n);
            aV_[s].resize(n);
            aH_[s].resize(n);
            force_[s].assign(n, 0.0F);
            tOverL_[s] = m.tension / m.L;
            for (std::size_t i = 0; i < n; ++i) {
                banks_[2 * s].setMode(i, m.oscV[i]);
                banks_[2 * s + 1].setMode(i, m.oscH[i]);
                const double sq = std::sqrt(2.0 / m.L);
                const double sign = (setup.ends[s] == pme::BridgeEnd::AtL)
                                        ? ((i % 2 == 0) ? 1.0 : -1.0)  // (-1)^(n+1), n = i + 1
                                        : 1.0;
                const double gn = sq * sign / m.k[i];
                g_[s][i] = static_cast<float>(gn);
                aV_[s][i] = static_cast<float>(m.rhoA * m.oscV[i].omegaHat2 * gn);
                aH_[s][i] = static_cast<float>(m.rhoA * m.oscH[i].omegaHat2 * gn);
            }
        }
        conductance_ = setup.model.conductance;
        if (setup.model.massSpringDamper) {
            const double mass = setup.model.mass;
            const double omega = std::sqrt(setup.model.stiffness / mass);
            const double sigma = setup.model.damping / (2.0 * mass);
            pme::OscCoeffs c;
            ok_ = pme::compileOscillator(omega, sigma, setup.fs, c) == pme::Status::Ok;
            beta_ = c.beta;
            kappa_ = c.kappa;
            gamma_ = c.gamma;
            phi_ = 1.0 / std::sqrt(mass);
            hasMode_ = true;
        }
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t segmentCount() const { return g_.size(); }
    [[nodiscard]] std::size_t modeCount(std::size_t s) const { return banks_[2 * s].size(); }
    /// Vertical-polarisation state of segment s.
    [[nodiscard]] float* q(std::size_t s) { return banks_[2 * s].q(); }
    [[nodiscard]] float* u(std::size_t s) { return banks_[2 * s].u(); }
    /// Horizontal-polarisation state of segment s.
    [[nodiscard]] float* qH(std::size_t s) { return banks_[2 * s + 1].q(); }
    [[nodiscard]] float* uH(std::size_t s) { return banks_[2 * s + 1].u(); }
    [[nodiscard]] double bridgeDisplacement() const { return wV_; }

    /// One sample in the order of SPEC-BRG "Discrete coupling order".
    void step() {
        double fV = 0.0;
        double fH = 0.0;
        for (std::size_t s = 0; s < g_.size(); ++s) {
            float accV = 0.0F;
            float accH = 0.0F;
            const float* qv = banks_[2 * s].q();
            const float* qh = banks_[2 * s + 1].q();
            for (std::size_t i = 0; i < g_[s].size(); ++i) {
                accV += aV_[s][i] * qv[i];
                accH += aH_[s][i] * qh[i];
            }
            fV += static_cast<double>(accV) - tOverL_[s] * wV_;
            fH += static_cast<double>(accH) - tOverL_[s] * wH_;
        }
        // SPEC-BRG "Polarisation coupling" with SI-0019: F_par = c F_v - s F_h, F_perp = s F_v + c
        // F_h.
        const double fPar = cosT_ * fV - sinT_ * fH;
        const double fPerp = sinT_ * fV + cosT_ * fH;
        if (hasMode_) {
            const double uNext = beta_ * ub_ - kappa_ * qb_ + gamma_ * phi_ * fPar;
            ub_ = uNext;
            qb_ += uNext;
        }
        wR_ += dt_ * conductance_ * fPar;
        wPerp_ += dt_ * gH_ * fPerp;
        const double wPar = phi_ * qb_ + wR_;
        const double wVNext = cosT_ * wPar + sinT_ * wPerp_;  // w = R^T [w_par; w_perp]
        const double wHNext = -sinT_ * wPar + cosT_ * wPerp_;
        const double dttV = (wVNext - 2.0 * wV_ + wVPrev_) / (dt_ * dt_);
        const double dttH = (wHNext - 2.0 * wH_ + wHPrev_) / (dt_ * dt_);
        wVPrev_ = wV_;
        wV_ = wVNext;
        wHPrev_ = wH_;
        wH_ = wHNext;
        for (std::size_t s = 0; s < g_.size(); ++s) {
            for (std::size_t i = 0; i < g_[s].size(); ++i) {
                force_[s][i] = static_cast<float>(-static_cast<double>(g_[s][i]) * dttV);
            }
            banks_[2 * s].step(std::span<const float>(force_[s]));
            for (std::size_t i = 0; i < g_[s].size(); ++i) {
                force_[s][i] = static_cast<float>(-static_cast<double>(g_[s][i]) * dttH);
            }
            banks_[2 * s + 1].step(std::span<const float>(force_[s]));
        }
    }

private:
    double dt_;
    double cosT_, sinT_, gH_;
    bool ok_{true};
    bool hasMode_{false};
    double conductance_{0.0};
    double beta_{0.0}, kappa_{0.0}, gamma_{0.0}, phi_{0.0};
    double qb_{0.0}, ub_{0.0}, wR_{0.0}, wPerp_{0.0};
    double wV_{0.0}, wVPrev_{0.0}, wH_{0.0}, wHPrev_{0.0};
    std::vector<pme::osc::ModeBank> banks_;
    std::vector<std::vector<float>> g_, aV_, aH_, force_;
    std::vector<double> tOverL_;
};

}  // namespace pme::oracle::brg
