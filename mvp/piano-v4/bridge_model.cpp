// SPDX-License-Identifier: MIT
#include "bridge_model.hpp"
#include "soundboard_table.hpp"
#include "board_override.hpp"
#include <cmath>

namespace piano_mvp_v4 {
pme::bridge::ParallelModel localBridgeModel(double pan, bool lossless) {
    pme::bridge::ParallelModel model;
    model.kind = pme::bridge::ParallelModel::Kind::ModalSet;
    model.modes.reserve(kLocalBridgeModes);
    for (std::size_t i = 0; i < kLocalBridgeModes; ++i) {
        const auto& row = piano_mvp_v3::kSoundboardModes[i];
        const double phi = (1-pan)*row[2] + pan*row[3];
        model.modes.push_back({row[0],lossless ? 0.0 : row[1],phi});
    }
    return model;
}
double residualBridgeConductance(double omega, double pan) noexcept {
    if (!std::isfinite(omega) || omega <= 0 || !std::isfinite(pan) || pan < 0 || pan > 1)
        return 0;
    double conductance = 0;
    for (std::size_t i = kLocalBridgeModes; i < piano_mvp_v3::kSoundboardModes.size(); ++i) {
        const auto& row = piano_mvp_v3::kSoundboardModes[i];
        const double phi = (1-pan)*row[2] + pan*row[3];
        const double detuning = row[0]*row[0] - omega*omega;
        const double loss = 2*row[1]*omega;
        conductance += phi*phi*loss*omega/(detuning*detuning + loss*loss);
    }
    return conductance;
}
double residualBridgeConductance(double omega, const double* phiTail) noexcept {
    if (!std::isfinite(omega) || omega <= 0 || !phiTail) return 0;
    double conductance = 0;
    for (std::size_t i = kLocalBridgeModes; i < piano_mvp_v3::kSoundboardModes.size(); ++i) {
        const auto& row = piano_mvp_v3::kSoundboardModes[i];
        const double phi = phiTail[i-kLocalBridgeModes];
        const double rowOmega = g_boardOverride ? g_boardOverride->omega[i] : row[0], rowSigma = g_boardOverride ? g_boardOverride->sigma[i] : row[1];   // 0.7a hook
        const double detuning = rowOmega*rowOmega - omega*omega;
        const double loss = 2*rowSigma*omega;
        conductance += phi*phi*loss*omega/(detuning*detuning + loss*loss);
    }
    return conductance;
}
pme::Status applyResidualBridgeLoss(pme::StringModes& modes, const double* phiTail) {
    if (!phiTail || modes.omega.size()!=modes.sigma.size() ||
        modes.k.size()!=modes.omega.size() || modes.oscV.size()!=modes.omega.size() ||
        modes.oscH.size()!=modes.omega.size() || modes.omega.empty() ||
        modes.rhoA<=0 || modes.L<=0)
        return pme::Status::InvalidArgument;
    auto updated = modes;
    for (std::size_t n=0;n<modes.omega.size();++n) {
        const double omega = modes.omega[n];
        const double g2 = 2/(modes.L*modes.k[n]*modes.k[n]);
        updated.sigma[n] += 0.5*modes.rhoA*omega*omega*g2*residualBridgeConductance(omega,phiTail);
        if (pme::compileOscillator(omega,updated.sigma[n],modes.fs,updated.oscV[n])!=pme::Status::Ok ||
            pme::compileOscillator(omega,updated.sigma[n],modes.fs,updated.oscH[n])!=pme::Status::Ok)
            return pme::Status::Rejected;
    }
    modes = std::move(updated);
    return pme::Status::Ok;
}
pme::Status applyResidualBridgeLoss(pme::StringModes& modes, double pan) {
    if (!std::isfinite(pan) || pan < 0 || pan > 1 || modes.omega.size()!=modes.sigma.size() ||
        modes.k.size()!=modes.omega.size() || modes.oscV.size()!=modes.omega.size() ||
        modes.oscH.size()!=modes.omega.size() || modes.omega.empty() ||
        modes.rhoA<=0 || modes.L<=0)
        return pme::Status::InvalidArgument;
    auto updated = modes;
    for (std::size_t n=0;n<modes.omega.size();++n) {
        const double omega = modes.omega[n];
        // E=1/2 rhoA omega^2 A^2, mean P=1/2 Re(Y) a_n^2 A^2.
        // With a_n=rhoA omega^2 g_n, amplitude loss sigma=P/(2E).
        const double g2 = 2/(modes.L*modes.k[n]*modes.k[n]);
        updated.sigma[n] += 0.5*modes.rhoA*omega*omega*g2*
                            residualBridgeConductance(omega,pan);
        if (pme::compileOscillator(omega,updated.sigma[n],modes.fs,updated.oscV[n])!=pme::Status::Ok ||
            pme::compileOscillator(omega,updated.sigma[n],modes.fs,updated.oscH[n])!=pme::Status::Ok)
            return pme::Status::Rejected;
    }
    modes = std::move(updated);
    return pme::Status::Ok;
}
}
