// SPDX-License-Identifier: MIT
// A7 backend of the oracle rig: the same `GeoRig` as geometric_rig_scratch.hpp, but the geometric
// terms are computed by the code under test (phase A7). Compiled only with PME_ORACLE_GEO_A7
// (CMake PME_A7_ORACLE_BACKEND=A7, or AUTO once PME_A7_ADAPTER_VERIFIED is ON).
//
// WRITTEN BEFORE THE A7 HEADERS EXISTED. It follows the names the A7 implementer brief fixes
// (docs/agents/briefs/A7-implementer.md: A7-R1 `TensionModulationPotential<S> : nl::Potential<S>`
// exposing S and Delta T, A7-R2 `compileLongitudinal(const StringModes&, const StringParams&, int
// ML, double b1L, double b3L, double fs, double fMax, LongitudinalModes& out)`, A7-R3
// `longitudinalForce`, A7-R4 `longitudinalBridgeForce`) and the existing A3 API (SavSegment,
// savStep, Potential). Every line that names something the brief leaves open is marked `ADAPT`:
// search for it, fix the line against engine/src/string/include/pme/string/longitudinal*.hpp and
// engine/src/nonlinear/include/pme/nonlinear/tension*.hpp (headers only), then set
// PME_A7_ADAPTER_VERIFIED to ON in CMakeLists.txt. Nothing else in the tests changes.
//
// Precision. The rig runs the segment with S = double (as A3's tests do), because ORC-11 asks for
// 1e-9 relative and a float32 result carries 6e-8. The transverse state pointers are therefore
// double*. The arrival time of A7-T3 does not depend on the state type.
//
// Layout. The SAV segment stacks the vertical modes, then the horizontal modes (A7-R1), so
// qV() = q(), qH() = q() + N. The order of a sample follows SPEC-EXEC: forces from step n, then
// the SAV step of the transverse modes, then the longitudinal modes.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "geometric_oracle_common.hpp"
#include "geometric_rig.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/nonlinear/tension.hpp"  // ADAPT: header name (brief: tension*.hpp)
#include "pme/osc/oscillator.hpp"
#include "pme/string/longitudinal.hpp"  // ADAPT: header name (brief: longitudinal*.hpp)

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
          n_(setup.modes.omega.size()) {
        gamma_ = ea_ - tension_;
        ok_ = gamma_ > 0.0;
        // Transverse SAV segment: N vertical then N horizontal modes, mass rhoA (A3).
        ok_ = ok_ && seg_.configure(2 * n_, 0, 0, setup.fs) == pme::Status::Ok;
        std::vector<double> kSq(n_);
        for (std::size_t i = 0; i < n_; ++i) {
            kSq[i] = setup.modes.k[i] * setup.modes.k[i];
            pme::osc::OscCoeffsF64 cv;
            pme::osc::OscCoeffsF64 ch;
            ok_ = ok_ &&
                  pme::osc::compileOscillatorF64(setup.modes.omega[i], setup.modes.sigma[i],
                                                 setup.fs, cv) == pme::Status::Ok &&
                  pme::osc::compileOscillatorF64(setup.modes.omega[i], setup.modes.sigma[i],
                                                 setup.fs, ch) == pme::Status::Ok;
            ok_ = ok_ && seg_.setCoordinate(i, cv, rhoA_) == pme::Status::Ok &&
                  seg_.setCoordinate(n_ + i, ch, rhoA_) == pme::Status::Ok;
        }
        // ADAPT: constructor of the potential: k_n^2 (float64 span) and (EA - T) / (2 L) as plain
        // numbers (brief: "takes k_n^2 (float64 span) and (EA - T)/(2L) as plain numbers").
        pot_ = pme::nl::TensionModulationPotential<double>(kSq, gamma_ / (2.0 * length_));
        // ADAPT: compile call as in brief A7-R2; `out` type name and member names are unknown.
        ok_ = ok_ && pme::string::compileLongitudinal(
                         setup.modes, setup.params, setup.longitudinalModes, setup.b1L, setup.b3L,
                         setup.fs, setup.fMax, lon_) == pme::Status::Ok;
        kept_ = lon_.count;  // ADAPT: member holding the number of kept modes
        xi_.assign(static_cast<std::size_t>(kept_), 0.0);
        uxi_.assign(static_cast<std::size_t>(kept_), 0.0);
        seg_.resetState();
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] int longitudinalKept() const { return kept_; }
    [[nodiscard]] std::size_t transverseModes() const { return n_; }
    [[nodiscard]] double* qV() { return seg_.q(); }
    [[nodiscard]] double* uV() { return seg_.u(); }
    [[nodiscard]] double* qH() { return seg_.q() + n_; }
    [[nodiscard]] double* uH() { return seg_.u() + n_; }

    /// Zero-velocity start of the longitudinal modes at t = 0 (q^0 = 0): u^0 = -gamma f^0 / 2
    /// (Taylor start q^(-1) = q^0 + dt^2 f^0 / 2). See geometric_rig_scratch.hpp.
    /// Also makes psi consistent with the potential for the transverse state just set (A3
    /// initialisePsi); the tests call this once, after setting the transverse state.
    void startLongitudinalAtRest() {
        if (tm_) {
            const pme::nl::Potential<double>* p = &pot_;
            seg_.initialisePsi(std::span<const pme::nl::Potential<double>* const>(&p, 1));
        }
        std::vector<double> f(static_cast<std::size_t>(kept_));
        longitudinalForce(f);
        for (std::size_t m = 0; m < f.size(); ++m) {
            uxi_[m] = -0.5 * lon_.osc[m].gamma * f[m];  // ADAPT: per-mode float64 coefficients
        }
    }

    /// One sample (SPEC-EXEC): longitudinal forcing from q^n, SAV step of the transverse modes,
    /// then the longitudinal difference-form update with the forcing at step n.
    void step() {
        std::vector<double> f(static_cast<std::size_t>(kept_));
        longitudinalForce(f);
        std::vector<const pme::nl::Potential<double>*> pots;
        if (tm_) {
            pots.push_back(&pot_);
        }
        pme::nl::savStep<double>(seg_, std::span<const pme::nl::Potential<double>* const>(pots),
                                 std::span<const pme::nl::Dashpot<double>>{},
                                 std::span<const double>{});
        for (std::size_t m = 0; m < f.size(); ++m) {
            const auto& c =
                lon_.osc[m];  // ADAPT: per-mode float64 coefficients (beta, kappa, gamma)
            const double u = c.beta * uxi_[m] - c.kappa * xi_[m] + c.gamma * f[m];
            uxi_[m] = u;
            xi_[m] += u;
        }
    }

    /// S = sum k_n^2 (q_v,n^2 + q_h,n^2) from the code under test.
    [[nodiscard]] double S() const {
        return pot_.slopeIntegral(std::span<const double>(seg_.q(), 2 * n_));  // ADAPT: accessor
    }
    /// Delta T = (EA - T) S / (2 L) from the code under test.
    [[nodiscard]] double deltaT() const {
        return pot_.deltaT(std::span<const double>(seg_.q(), 2 * n_));  // ADAPT: accessor
    }
    /// f_m^L for m = 1..f.size() from the code under test (A7-R3), from the present state.
    void longitudinalForce(std::span<double> f) const {
        // ADAPT: signature (brief: `longitudinalForce(...)`, result per unit modal mass).
        pme::string::longitudinalForce<double>(lon_, std::span<const double>(seg_.q(), n_),
                                               std::span<const double>(seg_.q() + n_, n_), f);
    }
    /// F_L (N) at x = L from the code under test (A7-R4).
    [[nodiscard]] double bridgeForceL() const {
        // ADAPT: signature (brief: `longitudinalBridgeForce(...)`).
        return pme::string::longitudinalBridgeForce<double>(
            lon_, xi_, std::span<const double>(seg_.q(), n_),
            std::span<const double>(seg_.q() + n_, n_));
    }
    /// Transverse energy (modes, psi and V_KC, SAV energy of A3) in J. SI-0015: transverse only.
    [[nodiscard]] double transverseEnergy() const { return pme::nl::savEnergy<double>(seg_); }
    /// Longitudinal energy E_L = sum_m rhoA H_m^L in J.
    [[nodiscard]] double longitudinalEnergy() const {
        double e = 0.0;
        for (std::size_t m = 0; m < xi_.size(); ++m) {
            const double u = uxi_[m];
            const double v = u / dt_;
            e += rhoA_ * (0.5 * v * v + 0.5 * lon_.osc[m].omegaHat2 * xi_[m] * (xi_[m] - u));
        }
        return e;
    }

private:
    double dt_;
    double length_;
    double ea_;
    double tension_;
    double rhoA_;
    bool tm_;
    std::size_t n_;
    double gamma_{0.0};
    bool ok_{true};
    int kept_{0};
    pme::nl::SavSegment<double> seg_;
    pme::nl::TensionModulationPotential<double> pot_;  // ADAPT: type and default constructibility
    pme::string::LongitudinalModes lon_;               // ADAPT: type name
    std::vector<double> xi_, uxi_;
};

}  // namespace pme::oracle::geo
