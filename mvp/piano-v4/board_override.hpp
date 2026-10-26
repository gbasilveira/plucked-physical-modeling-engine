// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <vector>
// Piano 0.7a hook: an offline renderer (mvp/piano-grand/grand_render.cpp) can replace the 512-mode MVP 3
// soundboard table by another modal board (FEM grand board): angular frequency, amplitude decay rate sigma and
// the mass-normalised participation phi[port*512 + i] of every key's bridge point. With no override installed
// (the default, and always in the plug-ins) every code path is exactly the 0.6 code. The radiation tables of the
// old board are zeroed under an override: the renderer takes the radiation records and does its own radiation.
namespace piano_mvp_v4 {
struct BoardOverride {
    std::vector<double> omega, sigma;   // 512 entries (unused modes: omega large, phi 0)
    std::vector<double> phi;            // 88 * 512
};
inline const BoardOverride* g_boardOverride = nullptr;
inline void setBoardOverride(const BoardOverride* o) noexcept { g_boardOverride = o; }
}
