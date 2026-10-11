// SPDX-License-Identifier: MIT
// Diagonal damper (A6-R1). See pme/dampers/diagonal.hpp.
#include "pme/dampers/diagonal.hpp"

#include <cmath>
#include <numbers>

#include "pme/dampers/lift.hpp"
#include "pme/string/string_model.hpp"

namespace pme::dmp {

namespace {

// sin(pi r) with the argument reduced exactly modulo 2 first, so that integer r gives exactly 0.
double sinPi(double r) noexcept {
    const double red = std::remainder(r, 2.0);
    return std::sin(std::numbers::pi * red);
}

OscCoeffs roundCoefficients(const osc::OscCoeffsF64& c) noexcept {
    OscCoeffs out;
    out.beta = static_cast<float>(c.beta);
    out.kappa = static_cast<float>(c.kappa);
    out.gamma = static_cast<float>(c.gamma);
    out.omegaHat2 = c.omegaHat2;
    out.sigmaHat = c.sigmaHat;
    return out;
}

bool tablesConsistent(const StringModes& m) noexcept {
    return !m.omega.empty() && m.omega.size() == m.sigma.size() && m.omega.size() == m.k.size() &&
           m.rhoA > 0.0 && m.L > 0.0 && m.fs > 0.0;
}

}  // namespace

std::vector<double> padShape(const StringModes& modes, const DamperParams& params) {
    std::vector<double> phi(modes.k.size(), 0.0);
    const double amp = std::sqrt(2.0 / modes.L);
    for (std::size_t i = 0; i < phi.size(); ++i) {
        // SPEC-STR forces and observations: phi-bar_n = phi_n(x_d) W(k_n), x_d = xRatio L
        const double shape = amp * sinPi(static_cast<double>(i + 1) * params.xRatio);
        phi[i] = shape * string::windowFactor(string::WindowKind::RaisedCosine, modes.k[i],
                                              params.width);
    }
    return phi;
}

OverdampingReport DiagonalDamper::analyse(const StringModes& modes, const DamperParams& params,
                                          int steps) {
    OverdampingReport report;
    if (!tablesConsistent(modes) || validateParams(params) != Status::Ok || steps < 1) {
        report.status = Status::Rejected;
        report.rejectedMode = 0;
        return report;
    }
    const std::vector<double> phi = padShape(modes, params);
    for (int s = 0; s <= steps; ++s) {
        const double e = static_cast<double>(s) / static_cast<double>(steps);
        for (std::size_t n = 0; n < phi.size(); ++n) {
            const double phi2 = phi[n] * phi[n];
            const double sigma = modes.sigma[n] + e * params.Rd * phi2 / (2.0 * modes.rhoA);
            const double omega =
                std::sqrt(modes.omega[n] * modes.omega[n] + e * params.Kd * phi2 / modes.rhoA);
            const double ratio = omega > 0.0 ? sigma / omega : 0.0;
            if (ratio > report.worstRatio) {
                report.worstRatio = ratio;
                report.worstMode = n;
                report.worstE = e;
            }
            osc::OscCoeffsF64 c;
            if (report.status == Status::Ok &&
                osc::compileOscillatorF64(omega, sigma, modes.fs, c) != Status::Ok) {
                report.status = Status::Rejected;
                report.rejectedMode = n;
                report.rejectedE = e;
            }
        }
    }
    return report;
}

Status DiagonalDamper::configure(const StringModes& modes, const DamperParams& params) {
    if (!tablesConsistent(modes) || validateParams(params) != Status::Ok) {
        return Status::InvalidArgument;
    }
    if (analyse(modes, params).status != Status::Ok) {
        return Status::Rejected;
    }
    n_ = modes.omega.size();
    fs_ = modes.fs;
    rhoA_ = modes.rhoA;
    params_ = params;
    phiBar_ = padShape(modes, params);
    omega_ = modes.omega;
    sigma_ = modes.sigma;
    dSigma_.assign(n_, 0.0);
    dOmega2_.assign(n_, 0.0);
    for (std::size_t i = 0; i < n_; ++i) {
        const double phi2 = phiBar_[i] * phiBar_[i];
        dSigma_[i] = params.Rd * phi2 / (2.0 * rhoA_);  // SPEC-DMP diagonal
        dOmega2_[i] = params.Kd * phi2 / rhoA_;         // SPEC-DMP diagonal
    }
    force_.assign(n_, OscCoeffs{});
    target_.assign(n_, OscCoeffs{});
    omegaForce_ = omega_;
    sigmaForce_ = sigma_;
    omegaTarget_ = omega_;
    sigmaTarget_ = sigma_;
    for (std::size_t i = 0; i < n_; ++i) {
        osc::OscCoeffsF64 c;
        if (osc::compileOscillatorF64(omega_[i], sigma_[i], fs_, c) != Status::Ok) {
            return Status::Rejected;  // the string's own mode fails: not a damper problem
        }
        force_[i] = roundCoefficients(c);
        target_[i] = force_[i];
    }
    eForce_ = 0.0;
    eTarget_ = 0.0;
    planned_ = false;
    failures_ = 0;
    return Status::Ok;
}

double DiagonalDamper::sigmaAt(std::size_t n, double e) const noexcept {
    return sigma_[n] + e * dSigma_[n];  // SPEC-DMP diagonal
}

double DiagonalDamper::omegaAt(std::size_t n, double e) const noexcept {
    return std::sqrt(omega_[n] * omega_[n] + e * dOmega2_[n]);  // SPEC-DMP diagonal
}

Status DiagonalDamper::coefficients(std::size_t n, double e,
                                    osc::OscCoeffsF64& out) const noexcept {
    if (n >= n_) {
        return Status::InvalidArgument;
    }
    // SPEC-OSC discrete scheme at the damped omega and sigma (SPEC-DMP "Diagonal")
    return osc::compileOscillatorF64(omegaAt(n, e), sigmaAt(n, e), fs_, out);
}

Status DiagonalDamper::initialise(double e, osc::ModeBank& bank, std::size_t firstMode) noexcept {
    if (firstMode + n_ > bank.size()) {
        return Status::InvalidArgument;
    }
    for (std::size_t i = 0; i < n_; ++i) {
        osc::OscCoeffsF64 c;
        const Status st = coefficients(i, e, c);
        if (st != Status::Ok) {
            return st;
        }
        force_[i] = roundCoefficients(c);
        omegaForce_[i] = omegaAt(i, e);
        sigmaForce_[i] = sigmaAt(i, e);
        bank.setMode(firstMode + i, force_[i]);
    }
    eForce_ = e;
    planned_ = false;
    return Status::Ok;
}

bool DiagonalDamper::plan(double eEnd, int subBlock, std::span<osc::CoeffRamp> ramps) noexcept {
    planned_ = false;
    if (eEnd == eForce_ || ramps.size() < n_) {
        return false;
    }
    for (std::size_t i = 0; i < n_; ++i) {
        osc::OscCoeffsF64 c;
        if (coefficients(i, eEnd, c) == Status::Ok) {
            target_[i] = roundCoefficients(c);
            omegaTarget_[i] = omegaAt(i, eEnd);
            sigmaTarget_[i] = sigmaAt(i, eEnd);
        } else {
            ++failures_;  // keep the coefficients in force for this mode
            target_[i] = force_[i];
            omegaTarget_[i] = omegaForce_[i];
            sigmaTarget_[i] = sigmaForce_[i];
        }
        // SPEC-OSC time-varying parameters: ramp when a change exceeds 0.1% per sub-block
        ramps[i] = osc::planRamp(force_[i], omegaForce_[i], sigmaForce_[i], target_[i],
                                 omegaTarget_[i], sigmaTarget_[i], subBlock);
    }
    eTarget_ = eEnd;
    planned_ = true;
    return true;
}

void DiagonalDamper::commit(osc::ModeBank& bank, std::size_t firstMode) noexcept {
    if (!planned_) {
        return;
    }
    for (std::size_t i = 0; i < n_; ++i) {
        force_[i] = target_[i];
        omegaForce_[i] = omegaTarget_[i];
        sigmaForce_[i] = sigmaTarget_[i];
        bank.setMode(firstMode + i, force_[i]);
    }
    eForce_ = eTarget_;
    planned_ = false;
}

}  // namespace pme::dmp
