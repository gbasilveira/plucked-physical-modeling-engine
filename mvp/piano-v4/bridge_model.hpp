// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include "pme/bridge/model.hpp"
namespace piano_mvp_v4 {
inline constexpr std::size_t kLocalBridgeModes = 128;
// First 128 board modes from MVP 3, observed at the note's weighted bridge position.
// This explicit modal reduction controls CPU; the radiation path still has 512 modes.
// One local mechanical model per key; inter-key mechanical coupling is deferred.
pme::bridge::ParallelModel localBridgeModel(double pan, bool lossless = false);
}
