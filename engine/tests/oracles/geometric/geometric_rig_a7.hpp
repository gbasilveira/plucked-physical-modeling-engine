// SPDX-License-Identifier: MIT
// A7 backend of the oracle rig: the same `GeoRig` as geometric_rig_scratch.hpp, but the geometric
// terms are computed by the code under test (phase A7). Compiled only with PME_ORACLE_GEO_A7
// (CMake PME_A7_ORACLE_BACKEND=A7, or AUTO once the A7 headers exist).
//
// Written against the public-for-tests headers of A7 only (pme/nonlinear/tension.hpp,
// pme/string/longitudinal.hpp) and the A3 SAV segment; the transverse modes are stepped by A3's
// savStep with the A7 TensionModulationPotential, the longitudinal modes by LongitudinalBank. The
// order of a sample follows SPEC-EXEC: forcing from step n, SAV step of the transverse modes, then
// the longitudinal update.
//
// Precision. The rig runs with S = double (SI-0023: ORC-11 asks for 1e-9 relative, a float32
// result carries 6e-8). The transverse state pointers are therefore double*. The arrival time of
// A7-T3 does not depend on the state type.
//
// Layout. The SAV segment stacks the vertical modes, then the horizontal modes, so
// qV() = q(), qH() = q() + N.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "geometric_oracle_common.hpp"
#include "geometric_rig.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/nonlinear/tension.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/string/longitudinal.hpp"

namespace pme::oracle::geo {

class GeoRig {
public:
    explicit GeoRig(const GeoSetup& setup)
        : n_(setup.modes.omega.size()), tm_(setup.tensionModulation), rhoA_(setup.modes.rhoA) {
        const double ea = pme::string::axialStiffness(setup.params);
        const double gamma = ea - setup.modes.tension;
        ok_ = gamma > 0.0;
        ok_ = ok_ && seg_.configure(2 * n_, 0, 0, setup.fs) == pme::Status::Ok;
        std::vector<double> kSq(n_);
        for (std::size_t i = 0; i < n_; ++i) {
            kSq[i] = setup.modes.k[i] * setup.modes.k[i];
            pme::osc::OscCoeffsF64 c;
            ok_ = ok_ &&
                  pme::osc::compileOscillatorF64(setup.modes.omega[i], setup.modes.sigma[i],
                                                 setup.fs, c) == pme::Status::Ok &&
                  seg_.setCoordinate(i, c, rhoA_) == pme::Status::Ok &&
                  seg_.setCoordinate(n_ + i, c, rhoA_) == pme::Status::Ok;
        }
        ok_ = ok_ && pot_.configure(kSq, gamma / (2.0 * setup.modes.L)) == pme::Status::Ok;
        ok_ = ok_ && pme::string::compileLongitudinal(
                         setup.modes, setup.params, setup.longitudinalModes, setup.b1L, setup.b3L,
                         setup.fs, setup.fMax, lon_) == pme::Status::Ok;
        bank_.configure(lon_);
        kept_ = lon_.kept();
        seg_.resetState();
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] int longitudinalKept() const { return kept_; }
    [[nodiscard]] std::size_t transverseModes() const { return n_; }
    [[nodiscard]] double* qV() { return seg_.q(); }
    [[nodiscard]] double* uV() { return seg_.u(); }
    [[nodiscard]] double* qH() { return seg_.q() + n_; }
    [[nodiscard]] double* uH() { return seg_.u() + n_; }

    /// Makes psi consistent with the potential for the transverse state just set, then starts the
    /// longitudinal modes at rest (Taylor start, LongitudinalBank::startFromRest, SI-0024). The
    /// tests call this once, after setting the transverse state.
    void startLongitudinalAtRest() {
        if (tm_) {
            const pme::nl::Potential<double>* p = &pot_;
            seg_.initialisePsi(std::span<const pme::nl::Potential<double>* const>(&p, 1));
        }
        std::vector<double> f(static_cast<std::size_t>(kept_));
        longitudinalForce(f);
        bank_.startFromRest(f);
    }

    /// One sample (SPEC-EXEC).
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
        bank_.step(f);
    }

    /// S and Delta T from the code under test.
    [[nodiscard]] double S() const { return pot_.sum(state()); }
    [[nodiscard]] double deltaT() const { return pot_.deltaT(state()); }
    /// f_m^L for m = 1..f.size() from the code under test, from the present state.
    void longitudinalForce(std::span<double> f) const {
        pme::string::longitudinalForce<double>(lon_, std::span<const double>(seg_.q(), n_),
                                               std::span<const double>(seg_.q() + n_, n_), f);
    }
    /// F_L (N) at x = L from the code under test.
    [[nodiscard]] double bridgeForceL() const {
        return pme::string::longitudinalBridgeForce<double>(
            lon_, std::span<const double>(bank_.q(), bank_.size()),
            std::span<const double>(seg_.q(), n_), std::span<const double>(seg_.q() + n_, n_));
    }
    /// Transverse energy: modes (rhoA H, SPEC-OSC discrete energy with q^(n-1) = q - u) plus V_KC
    /// from the code under test; the same quantity as the scratch backend (savEnergy needs a
    /// completed step, so it is not usable at t = 0). SI-0015: transverse only.
    [[nodiscard]] double transverseEnergy() const {
        double e = 0.0;
        for (std::size_t i = 0; i < 2 * n_; ++i) {
            const double q = seg_.q()[i];
            const double u = seg_.u()[i];
            const double v = u / seg_.dt();
            e += rhoA_ * (0.5 * v * v + 0.5 * seg_.omegaHat2()[i] * q * (q - u));
        }
        return e + (tm_ ? pot_.value(state()) : 0.0);
    }
    /// Longitudinal energy E_L = sum_m rhoA H_m^L.
    [[nodiscard]] double longitudinalEnergy() const { return bank_.energy(); }

private:
    [[nodiscard]] std::span<const double> state() const { return {seg_.q(), 2 * n_}; }

    std::size_t n_;
    bool tm_;
    double rhoA_;
    bool ok_{true};
    int kept_{0};
    pme::nl::SavSegment<double> seg_;
    pme::nl::TensionModulationPotential<double> pot_;
    pme::string::LongitudinalModes lon_;
    pme::string::LongitudinalBank<double> bank_;
};

}  // namespace pme::oracle::geo
