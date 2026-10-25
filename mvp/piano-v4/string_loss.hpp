// SPDX-License-Identifier: MIT
#pragma once
#include "pme/string.hpp"
namespace piano_mvp_v4 {
// Frequency above which the report's k^2 string-viscosity law is replaced by a
// constant loss factor (sigma proportional to omega). Assumption, see README 0.4.9.
inline constexpr double kStringLossCornerHz = 2500.0;
// Viscous part of the report's loss, eta*T/(rho*A)*k^2 (1/s), with saturation above the corner.
double saturatedStringViscosity(double viscosity, double tension, double rhoA,
                                double k, double omega, double cornerHz) noexcept;
// Replace the k^2 viscous term in sigma by the saturating law. Non-audio thread.
pme::Status applySaturatingStringLoss(pme::StringModes& modes, double viscosity, double cornerHz);
}
