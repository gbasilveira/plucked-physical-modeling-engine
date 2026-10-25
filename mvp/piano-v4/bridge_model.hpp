// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include "pme/bridge/model.hpp"
#include "pme/string.hpp"
namespace piano_mvp_v4 {
inline constexpr std::size_t kLocalBridgeModes = 128;
// First 128 board modes from MVP 3, observed at the note's weighted bridge position.
// This explicit modal reduction controls CPU; the radiation path still has 512 modes.
// One local mechanical model per key; inter-key mechanical coupling is deferred.
pme::bridge::ParallelModel localBridgeModel(double pan, bool lossless = false);
// Real mechanical admittance of the omitted board modes (m / (N s)).
double residualBridgeConductance(double omega, double pan) noexcept;
// Weak-coupling diagonal approximation to energy lost into omitted board modes.
// Non-audio thread. Leaves the explicit 128-mode coupling in place.
pme::Status applyResidualBridgeLoss(pme::StringModes& modes, double pan);
}
