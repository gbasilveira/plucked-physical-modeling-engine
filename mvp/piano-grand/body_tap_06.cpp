// SPDX-License-Identifier: MIT
// Piano 0.7b diagnostic: the 0.6 board's response to a bridge force tap, with no strings. The 0.6 radiation path of Piano::stringsStep/sampleShared on its own:
// bridge-mass low pass of the port force, modal radiation drive, 512-mode bank, output x 0.05.
// usage: piano-body-tap-06 key amplitude_N frames out.f32   (a 1 ms raised-cosine force pulse at the key's bridge point; stereo float32 out)
#include "grand_board.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: piano-body-tap-06 key amplitude frames out.f32\n"); return 1; }
    using namespace piano_mvp_v4;
    const int key = std::atoi(argv[1]); const double amp = std::atof(argv[2]); const int frames = std::atoi(argv[3]);
    const double fs = 48000.0; const std::size_t port = key - 21;
    std::vector<double> phi; buildParticipation(true, phi);
    GrandBoard b;
    if (!b.configure(fs, std::move(phi), 128, false, true)) { std::fprintf(stderr, "configure failed\n"); return 1; }
    std::vector<float> out(std::size_t(frames) * 2);
    const int pulse = int(0.001 * fs);
    for (int n = 0; n < frames; ++n) {
        const double F = n < pulse ? amp * 0.5 * (1 - std::cos(2 * std::numbers::pi * (n + 0.5) / pulse)) : 0.0;
        const double g = b.lowpassStep(port, F);
        b.accumulateDrive(port, g, 0, kBoardModeCount);
        b.radiationModes(0, kBoardModeCount);
        double L, R; b.radiationFinish(L, R);
        out[2 * n] = float(0.05 * L); out[2 * n + 1] = float(0.05 * R);
    }
    std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), out.size() * 4);
    return 0;
}
