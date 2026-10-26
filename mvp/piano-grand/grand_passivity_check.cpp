// SPDX-License-Identifier: MIT
// Piano 0.7a test G-T5: passivity of the coupled FEM board in the 0.6 difference-form scheme. With the BoardOverride installed
// (the 395-mode FEM table), pulses on several bridge ports excite the 128 coupled modes; after the forcing stops the discrete
// energy of the coupled modes must stay constant for the lossless board and never increase for the damped one. The same check
// also compares the no-override board with the 0.6 table (they must differ: the override is really used).
// usage: grand-passivity-check board_table.bin
#include "grand_board.hpp"
#include "board_override.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

using namespace piano_mvp_v4;
static bool run(bool lossless, double fs, double& e0, double& drift, double& rise) {
    GrandBoard b; std::vector<double> phi; buildParticipation(true, phi);
    if (!b.configure(fs, phi, 128, lossless, false)) return false;
    const int ports[4] = {3, 25, 50, 80};
    const int force = int(0.05 * fs), total = int(0.6 * fs);
    for (int n = 0; n < force; ++n) {
        for (int k = 0; k < 4; ++k) b.addForce(ports[k], 5.0 * std::sin(2 * M_PI * (200.0 + 170.0 * k) * n / fs) * std::exp(-double(n) / (0.01 * fs)));
        b.advanceCoupled();
    }
    e0 = b.coupledEnergy(); double lo = e0, hi = e0, prev = e0; rise = 0;
    for (int n = force; n < total; ++n) {
        b.advanceCoupled(); const double e = b.coupledEnergy();
        lo = std::min(lo, e); hi = std::max(hi, e); rise = std::max(rise, (e - prev) / e0); prev = e;
    }
    drift = (hi - lo) / e0;
    return true;
}
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: grand-passivity-check board_table.bin\n"; return 2; }
    BoardOverride ov; ov.omega.resize(512); ov.sigma.resize(512); ov.phi.resize(88 * 512);
    std::ifstream in(argv[1], std::ios::binary); if (!in) { std::cerr << "cannot read table\n"; return 2; }
    in.read(reinterpret_cast<char*>(ov.omega.data()), 512 * 8); in.read(reinterpret_cast<char*>(ov.sigma.data()), 512 * 8); in.read(reinterpret_cast<char*>(ov.phi.data()), 88 * 512 * 8);
    if (!in) { std::cerr << "short table\n"; return 2; }
    double e0, drift, rise; bool ok = true;
    // 0.6 table (no override)
    run(true, 48000.0, e0, drift, rise); const double e06 = e0;
    setBoardOverride(&ov);
    if (!run(true, 48000.0, e0, drift, rise)) return 1;
    std::printf("lossless FEM board: energy after the forcing %.4e J, relative spread %.2e (limit 1e-6)\n", e0, drift);
    ok &= drift < 1e-6 && e0 > 0 && std::fabs(e0 - e06) > 1e-3 * e06;
    if (!run(false, 48000.0, e0, drift, rise)) return 1;
    std::printf("damped FEM board: energy %.4e J, largest relative energy rise per sample %.2e (limit 1e-9), total change %.2e\n", e0, rise, drift);
    ok &= rise < 1e-9 && drift > 1e-3;
    setBoardOverride(nullptr);
    std::puts(ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
}
