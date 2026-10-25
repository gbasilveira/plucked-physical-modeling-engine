// SPDX-License-Identifier: MIT
#include "string_loss.hpp"
#include <cmath>
#include <numbers>
#include <utility>
namespace piano_mvp_v4 {
double saturatedStringViscosity(double eta, double T, double rhoA, double k, double omega,
                                double cornerHz) noexcept {
    const double wc = 2 * std::numbers::pi * cornerHz;
    const double c2 = T / rhoA;
    const double plain = eta * c2 * k * k;
    if (!(omega > wc)) return plain;
    // Match the report's law at the corner (k_c = omega_c/c), then keep the loss factor fixed.
    const double kc2 = wc * wc / c2;
    return eta * c2 * kc2 * (omega / wc);
}
pme::Status applySaturatingStringLoss(pme::StringModes& modes, double eta, double cornerHz) {
    if (!std::isfinite(eta) || eta < 0 || !std::isfinite(cornerHz) || cornerHz <= 0 ||
        modes.omega.empty() || modes.omega.size() != modes.sigma.size() ||
        modes.k.size() != modes.omega.size() || modes.oscV.size() != modes.omega.size() ||
        modes.oscH.size() != modes.omega.size() || modes.rhoA <= 0 || modes.tension <= 0)
        return pme::Status::InvalidArgument;
    auto updated = modes;
    for (std::size_t n = 0; n < modes.omega.size(); ++n) {
        const double plain = eta * modes.tension / modes.rhoA * modes.k[n] * modes.k[n];
        const double sat = saturatedStringViscosity(eta, modes.tension, modes.rhoA, modes.k[n],
                                                    modes.omega[n], cornerHz);
        updated.sigma[n] += sat - plain;
        if (updated.sigma[n] < 0) updated.sigma[n] = 0;
        if (pme::compileOscillator(modes.omega[n], updated.sigma[n], modes.fs, updated.oscV[n]) != pme::Status::Ok ||
            pme::compileOscillator(modes.omega[n], updated.sigma[n], modes.fs, updated.oscH[n]) != pme::Status::Ok)
            return pme::Status::Rejected;
    }
    modes = std::move(updated);
    return pme::Status::Ok;
}
}
