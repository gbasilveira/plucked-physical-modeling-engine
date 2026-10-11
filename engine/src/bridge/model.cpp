// SPDX-License-Identifier: MIT
// Parallel-axis bridge model factories (A4-R2). See docs/dev/bridge.md.
#include "pme/bridge/model.hpp"

#include <cmath>
#include <utility>

namespace pme::bridge {

namespace {

bool finiteNonNegative(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

}  // namespace

Status conductanceModel(double conductance, ParallelModel& out) {
    if (!finiteNonNegative(conductance)) {
        return Status::InvalidArgument;
    }
    ParallelModel m;
    m.kind = ParallelModel::Kind::Conductance;
    m.conductance = conductance;
    out = std::move(m);
    return Status::Ok;
}

Status massSpringDamperModel(double mass, double stiffness, double damping, ParallelModel& out) {
    if (!(std::isfinite(mass) && mass > 0.0) || !finiteNonNegative(stiffness) ||
        !finiteNonNegative(damping)) {
        return Status::InvalidArgument;
    }
    ParallelModel m;
    m.kind = ParallelModel::Kind::MassSpringDamper;
    BridgeMode mode;
    mode.omega = std::sqrt(stiffness / mass);  // SPEC-BODY Local bridge model: Omega = sqrt(k/m)
    mode.sigma = damping / (2.0 * mass);       // sigma = r / (2 m)
    mode.phi = 1.0 / std::sqrt(mass);          // Phi = 1 / sqrt(m): unit modal mass
    if (!std::isfinite(mode.omega) || !std::isfinite(mode.sigma) || !std::isfinite(mode.phi)) {
        return Status::InvalidArgument;
    }
    m.modes.push_back(mode);
    out = std::move(m);
    return Status::Ok;
}

Status modalSetModel(const LocalBridge& local, ParallelModel& out) {
    const std::size_t n = local.omega.size();
    if (local.sigma.size() != n || local.phi.size() != n ||
        !finiteNonNegative(local.conductanceHf)) {
        return Status::InvalidArgument;
    }
    ParallelModel m;
    m.kind = ParallelModel::Kind::ModalSet;
    m.conductance = local.conductanceHf;
    m.modes.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!finiteNonNegative(local.omega[i]) || !finiteNonNegative(local.sigma[i]) ||
            !std::isfinite(local.phi[i])) {
            return Status::InvalidArgument;
        }
        m.modes.push_back(BridgeMode{local.omega[i], local.sigma[i], local.phi[i]});
    }
    out = std::move(m);
    return Status::Ok;
}

Status compileBridgeMode(double omega, double sigma, double fs, osc::OscCoeffsF64& out) noexcept {
    if (omega != 0.0) {
        return osc::compileOscillatorF64(omega, sigma, fs, out);
    }
    if (!(std::isfinite(fs) && fs > 0.0) || !finiteNonNegative(sigma)) {
        return Status::InvalidArgument;
    }
    const double dt = 1.0 / fs;
    osc::OscCoeffsF64 c;
    c.sigmaHatDt = std::tanh(sigma * dt);  // SPEC-OSC: sigmaHat dt = tanh(sigma dt)
    c.omegaHat2Dt2 = 0.0;                  // free mass: no restoring force
    c.beta = (1.0 - c.sigmaHatDt) / (1.0 + c.sigmaHatDt);
    c.kappa = 0.0;
    c.gamma = dt * dt / (1.0 + c.sigmaHatDt);
    c.omegaHat2 = 0.0;
    c.sigmaHat = c.sigmaHatDt / dt;
    out = c;
    return Status::Ok;
}

}  // namespace pme::bridge
