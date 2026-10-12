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
    explicit Rig(const RigSetup& setup) : dt_(1.0 / setup.fs) {
        const std::size_t count = setup.segments.size();
        banks_.resize(count);
        g_.resize(count);
        a_.resize(count);
        tOverL_.resize(count);
        force_.resize(count);
        for (std::size_t s = 0; s < count; ++s) {
            const pme::StringModes& m = setup.segments[s];
            const std::size_t n = m.omega.size();
            banks_[s].resize(n);
            g_[s].resize(n);
            a_[s].resize(n);
            force_[s].assign(n, 0.0F);
            tOverL_[s] = m.tension / m.L;
            for (std::size_t i = 0; i < n; ++i) {
                banks_[s].setMode(i, m.oscV[i]);
                const double sq = std::sqrt(2.0 / m.L);
                const double sign = (setup.ends[s] == pme::BridgeEnd::AtL)
                                        ? ((i % 2 == 0) ? 1.0 : -1.0)  // (-1)^(n+1), n = i + 1
                                        : 1.0;
                const double gn = sq * sign / m.k[i];
                g_[s][i] = static_cast<float>(gn);
                a_[s][i] = static_cast<float>(m.rhoA * m.oscV[i].omegaHat2 * gn);
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
    [[nodiscard]] std::size_t segmentCount() const { return banks_.size(); }
    [[nodiscard]] std::size_t modeCount(std::size_t s) const { return banks_[s].size(); }
    [[nodiscard]] float* q(std::size_t s) { return banks_[s].q(); }
    [[nodiscard]] float* u(std::size_t s) { return banks_[s].u(); }
    [[nodiscard]] double bridgeDisplacement() const { return wCur_; }

    /// One sample in the order of SPEC-BRG "Discrete coupling order".
    void step() {
        double total = 0.0;
        for (std::size_t s = 0; s < banks_.size(); ++s) {
            float acc = 0.0F;
            const float* q = banks_[s].q();
            for (std::size_t i = 0; i < banks_[s].size(); ++i) {
                acc += a_[s][i] * q[i];
            }
            total += static_cast<double>(acc) - tOverL_[s] * wCur_;
        }
        if (hasMode_) {
            const double uNext = beta_ * ub_ - kappa_ * qb_ + gamma_ * phi_ * total;
            ub_ = uNext;
            qb_ += uNext;
        }
        wR_ += dt_ * conductance_ * total;
        const double wNext = phi_ * qb_ + wR_;
        const double dtt = (wNext - 2.0 * wCur_ + wPrev_) / (dt_ * dt_);
        wPrev_ = wCur_;
        wCur_ = wNext;
        for (std::size_t s = 0; s < banks_.size(); ++s) {
            for (std::size_t i = 0; i < banks_[s].size(); ++i) {
                force_[s][i] = static_cast<float>(-static_cast<double>(g_[s][i]) * dtt);
            }
            banks_[s].step(std::span<const float>(force_[s]));
        }
    }

private:
    double dt_;
    bool ok_{true};
    bool hasMode_{false};
    double conductance_{0.0};
    double beta_{0.0}, kappa_{0.0}, gamma_{0.0}, phi_{0.0};
    double qb_{0.0}, ub_{0.0}, wR_{0.0}, wCur_{0.0}, wPrev_{0.0};
    std::vector<pme::osc::ModeBank> banks_;
    std::vector<std::vector<float>> g_, a_, force_;
    std::vector<double> tOverL_;
};

}  // namespace pme::oracle::brg
