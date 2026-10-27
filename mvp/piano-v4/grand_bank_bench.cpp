// SPDX-License-Identifier: MIT
// Piano 0.7c tool: cost per sample of the grand radiation bank alone (embedded table), for a number of struck and unstruck ports, by part.
//   piano-grand-bank-bench [struck] [light] [seconds] [lid] [stride 1|3]
#include "grand_live.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    const int struck = argc > 1 ? std::atoi(argv[1]) : 12, lightN = argc > 2 ? std::atoi(argv[2]) : 10, secs = argc > 3 ? std::atoi(argv[3]) : 4, lid = argc > 4 ? std::atoi(argv[4]) : 0, stride = argc > 5 ? std::atoi(argv[5]) : 1;
    std::size_t size = 0; const std::uint8_t* blob = embeddedGrandTable(size); GrandTable t;
    if (!blob || !t.parse(blob, size)) { std::fprintf(stderr, "no table\n"); return 1; }
    GrandRadiation b; b.configure(&t, 48000.0); b.setHighStride(stride); b.request(lid, 0); b.setDirectEnabled(false);
    std::vector<std::uint8_t> port, light; std::vector<float> g;
    for (int i = 0; i < struck + lightN; ++i) { port.push_back(std::uint8_t(20 + 2 * i)); light.push_back(i >= struck); g.push_back(0.3f + 0.01f * i); }
    const int n = secs * 48000; double sink = 0;
    using clk = std::chrono::steady_clock; double tAcc = 0, tStep = 0, tFin = 0;
    for (int i = 0; i < n; ++i) {
        auto t0 = clk::now(); b.accumulate(port.data(), g.data(), light.data(), int(port.size()));
        auto t1 = clk::now(); b.step();
        auto t2 = clk::now(); double l, r; b.finish(l, r, nullptr); sink += l + r;
        auto t3 = clk::now();
        tAcc += std::chrono::duration<double>(t1 - t0).count(); tStep += std::chrono::duration<double>(t2 - t1).count(); tFin += std::chrono::duration<double>(t3 - t2).count();
        if (i % 4800 == 0) for (auto& x : g) x = -x;   // keep it alive
    }
    std::printf("struck %d light %d lid %d: accumulate %.2f us, step %.2f us, finish %.2f us per sample (total %.2f us; budget 20.8 us) [%g]\n", struck, lightN, lid, 1e6 * tAcc / n, 1e6 * tStep / n, 1e6 * tFin / n, 1e6 * (tAcc + tStep + tFin) / n, sink);
}
