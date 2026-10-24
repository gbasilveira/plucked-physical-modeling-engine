// SPDX-License-Identifier: MIT
// Scratch backend of the A7 oracle rig: SPEC-GEO written out from the specification alone, with no
// call into engine/src/string/longitudinal* or engine/src/nonlinear/tension*. It is NOT the code
// under test (see geometric_rig.hpp). Everything below cites its SPEC paragraph.
//
//   SPEC-GEO "Tension modulation": S = sum k_n^2 (q_v,n^2 + q_h,n^2), DeltaT = (EA - T) S / (2 L),
//       V_KC = (EA - T) S^2 / (8 L), dV/dq_n = DeltaT k_n^2 q_n. The scratch applies this force
//       explicitly at step n (the engine uses SAV; the difference is O(dt^2 DeltaT / T), far below
//       what the compared quantities resolve).
//   SPEC-GEO "Longitudinal modes": K_m = m pi / L, omega_m = K_m sqrt(EA / rhoA),
//       sigma_m = b1L + b3L omega_m^2, forced one-way by f_m^L (pair sum, specPairSumForce).
//   SPEC-GEO "Longitudinal force at the bridge":
//       F_L = EA sum_m sqrt(2/L) K_m (-1)^m xi_m + (EA - T)/2 (y_x(L)^2 + z_x(L)^2),
//       y_x(L) = sum_n q_n sqrt(2/L) k_n (-1)^n.
//   SPEC-OSC difference form: the modes run in the A1 ModeBank (float32 states, coefficients from
//       pme::compileOscillator); a longitudinal mode that fails the bandwidth rule ends the list.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "geometric_oracle_common.hpp"
#include "geometric_rig.hpp"
#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"

namespace pme::oracle::geo {

class GeoRig {
public:
    explicit GeoRig(const GeoSetup& setup)
        : dt_(1.0 / setup.fs),
          length_(setup.modes.L),
          ea_(setup.params.ECore * kPi * setup.params.dCore * setup.params.dCore / 4.0),
          tension_(setup.modes.tension),
          rhoA_(setup.modes.rhoA),
          tm_(setup.tensionModulation),
          gamma_(ea_ - tension_),
          ok_(gamma_ > 0.0) {
        const std::size_t n = setup.modes.omega.size();
        bankV_.resize(n);
        bankH_.resize(n);
        kSq_.resize(n);
        forceV_.assign(n, 0.0F);
        forceH_.assign(n, 0.0F);
        for (std::size_t i = 0; i < n; ++i) {
            bankV_.setMode(i, setup.modes.oscV[i]);
            bankH_.setMode(i, setup.modes.oscH[i]);
            kSq_[i] = setup.modes.k[i] * setup.modes.k[i];
        }
        const double c = std::sqrt(ea_ / rhoA_);
        std::vector<pme::OscCoeffs> coeffs;
        for (int m = 1; m <= setup.longitudinalModes; ++m) {
            const double km = static_cast<double>(m) * kPi / length_;
            const double omega = km * c;
            if (omega / (2.0 * kPi) > setup.fMax) {
                break;
            }
            pme::OscCoeffs osc;
            if (pme::compileOscillator(omega, setup.b1L + setup.b3L * omega * omega, setup.fs,
                                       osc) != pme::Status::Ok) {
                break;
            }
            coeffs.push_back(osc);
        }
        kept_ = static_cast<int>(coeffs.size());
        bankL_.resize(coeffs.size());
        for (std::size_t m = 0; m < coeffs.size(); ++m) {
            bankL_.setMode(m, coeffs[m]);
        }
        forceL_.assign(coeffs.size(), 0.0F);
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] int longitudinalKept() const { return kept_; }
    [[nodiscard]] std::size_t transverseModes() const { return kSq_.size(); }
    /// Transverse state (float32), vertical and horizontal polarisation.
    [[nodiscard]] float* qV() { return bankV_.q(); }
    [[nodiscard]] float* uV() { return bankV_.u(); }
    [[nodiscard]] float* qH() { return bankH_.q(); }
    [[nodiscard]] float* uH() { return bankH_.u(); }

    /// Zero-velocity start of the longitudinal modes at t = 0 (q^0 = 0) under the force of the
    /// present transverse state: the Taylor start q^(-1) = q^0 + (dt^2 / 2) f^0 gives
    /// u^0 = q^0 - q^(-1) = -gamma f^0 / 2. With u^0 = 0 the explicit scheme starts as if the force
    /// had been on for half a step already (q^1 = dt^2 f instead of dt^2 f / 2) and the front
    /// arrives about dt / 2 early. A test initial condition, not a request on the engine.
    void startLongitudinalAtRest() {
        std::vector<double> f(static_cast<std::size_t>(kept_));
        longitudinalForce(f);
        for (std::size_t m = 0; m < f.size(); ++m) {
            bankL_.u()[m] =
                static_cast<float>(-0.5 * static_cast<double>(bankL_.gamma()[m]) * f[m]);
        }
    }

    /// One sample: forces from the state at step n (SPEC-EXEC), then all modes advance.
    void step() {
        const double dT = deltaT();
        for (std::size_t i = 0; i < kSq_.size(); ++i) {
            const double fv =
                tm_ ? -dT * kSq_[i] * static_cast<double>(bankV_.q()[i]) / rhoA_ : 0.0;
            const double fh =
                tm_ ? -dT * kSq_[i] * static_cast<double>(bankH_.q()[i]) / rhoA_ : 0.0;
            forceV_[i] = static_cast<float>(fv);
            forceH_[i] = static_cast<float>(fh);
        }
        std::vector<double> f(static_cast<std::size_t>(kept_));
        longitudinalForce(f);
        for (std::size_t m = 0; m < f.size(); ++m) {
            forceL_[m] = static_cast<float>(f[m]);
        }
        bankV_.step(forceV_);
        bankH_.step(forceH_);
        bankL_.step(forceL_);
    }

    /// S = sum k_n^2 (q_v,n^2 + q_h,n^2) (float64, index order).
    [[nodiscard]] double S() const {
        double s = 0.0;
        for (std::size_t i = 0; i < kSq_.size(); ++i) {
            const double v = bankV_.q()[i];
            const double h = bankH_.q()[i];
            s += kSq_[i] * (v * v + h * h);
        }
        return s;
    }
    /// DeltaT = (EA - T) S / (2 L).
    [[nodiscard]] double deltaT() const { return gamma_ * S() / (2.0 * length_); }
    /// f_m^L for m = 1..f.size() by the SPEC-GEO pair sum, from the present transverse state.
    void longitudinalForce(std::span<double> f) const {
        const std::size_t n = kSq_.size();
        const std::span<const float> qv(bankV_.q(), n);
        const std::span<const float> qh(bankH_.q(), n);
        const auto r =
            specPairSumForce(length_, ea_, tension_, rhoA_, qv, qh, static_cast<int>(f.size()));
        std::copy(r.begin(), r.end(), f.begin());
    }
    /// F_L (N) at the bridge end x = L.
    [[nodiscard]] double bridgeForceL() const {
        double modal = 0.0;
        for (int m = 1; m <= kept_; ++m) {
            const double km = static_cast<double>(m) * kPi / length_;
            modal += std::sqrt(2.0 / length_) * km * ((m % 2 == 0) ? 1.0 : -1.0) *
                     static_cast<double>(bankL_.q()[m - 1]);
        }
        double yv = 0.0;
        double yh = 0.0;
        for (std::size_t i = 0; i < kSq_.size(); ++i) {
            const double kn = std::sqrt(kSq_[i]);
            const double sign = (i % 2 == 0) ? -1.0 : 1.0;  // (-1)^n, n = i + 1
            yv += static_cast<double>(bankV_.q()[i]) * std::sqrt(2.0 / length_) * kn * sign;
            yh += static_cast<double>(bankH_.q()[i]) * std::sqrt(2.0 / length_) * kn * sign;
        }
        return ea_ * modal + 0.5 * gamma_ * (yv * yv + yh * yh);
    }
    /// Transverse energy: modes (sum rhoA H_n, SPEC-OSC discrete energy) plus V_KC, float64.
    [[nodiscard]] double transverseEnergy() const {
        double e = 0.0;
        for (std::size_t i = 0; i < kSq_.size(); ++i) {
            e += modeEnergyOf(bankV_, i) + modeEnergyOf(bankH_, i);
        }
        const double s = S();
        return e + (tm_ ? gamma_ * s * s / (8.0 * length_) : 0.0);
    }
    /// Longitudinal energy E_L = sum_m rhoA H_m^L (SPEC-GEO, SI-0015).
    [[nodiscard]] double longitudinalEnergy() const {
        double e = 0.0;
        for (std::size_t m = 0; m < static_cast<std::size_t>(kept_); ++m) {
            e += modeEnergyOf(bankL_, m);
        }
        return e;
    }

private:
    /// rhoA H = rhoA [ (u/dt)^2 / 2 + omegaHat^2 q^n q^(n-1) / 2 ], q^(n-1) = q^n - u^n.
    [[nodiscard]] double modeEnergyOf(const pme::osc::ModeBank& bank, std::size_t i) const {
        const double q = bank.q()[i];
        const double u = bank.u()[i];
        const double v = u / dt_;
        return rhoA_ * (0.5 * v * v + 0.5 * bank.omegaHat2()[i] * q * (q - u));
    }

    double dt_;
    double length_;
    double ea_;
    double tension_;
    double rhoA_;
    bool tm_;
    double gamma_;
    bool ok_;
    int kept_{0};
    pme::osc::ModeBank bankV_, bankH_, bankL_;
    std::vector<double> kSq_;
    std::vector<float> forceV_, forceH_, forceL_;
};

}  // namespace pme::oracle::geo
