// SPDX-License-Identifier: MIT
#include "bridge_model.hpp"
#include "soundboard_table.hpp"

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
}
