// SPDX-License-Identifier: MIT
// Piano 0.7c tool: the impulse responses of the grand radiation bank. For each requested port the bank is reset, driven with one unit
// force sample and run for N frames; the output (0.05 x pressure, the plug-in scale) of the left and right channel of the chosen lid and
// microphone preset is written as float32, frames interleaved L R, port after port. The reference is the 0.7b E[mic, port, lag] times
// the level factor (gain 0.00529): compare_bank.py does that.
//   piano-grand-bank TABLE OUT lid mic frames port [port ...]      (lid 0..2, mic 0..2, ports 0..87)
#include "grand_live.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    if (argc < 7) { std::fprintf(stderr, "usage: piano-grand-bank TABLE OUT lid mic frames port [port ...]\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    GrandTable table;
    if (!table.parse(blob.data(), blob.size())) { std::fprintf(stderr, "bad table\n"); return 1; }
    GrandRadiation bank;
    if (!bank.configure(&table, 48000.0)) return 1;
    bank.setDirectEnabled(false);
    const int lid = std::atoi(argv[3]), mic = std::atoi(argv[4]), frames = std::atoi(argv[5]);
    bank.request(lid, mic);
    std::ofstream out(argv[2], std::ios::binary);
    std::vector<float> buf(static_cast<std::size_t>(frames) * 2);
    for (int a = 6; a < argc; ++a) {
        const std::uint8_t port = static_cast<std::uint8_t>(std::atoi(argv[a])), light = 0;
        bank.reset();
        // the request above started a crossfade from the default set; run it out silently first
        for (int i = 0; i < 4000; ++i) { bank.step(); double l, r; bank.finish(l, r, nullptr); }
        bank.reset();
        for (int n = 0; n < frames; ++n) {
            if (n == 0) { const float g = 1.f; bank.accumulate(&port, &g, &light, 1); }
            bank.step();
            double l, r; bank.finish(l, r, nullptr);
            buf[2 * n] = static_cast<float>(0.05 * l); buf[2 * n + 1] = static_cast<float>(0.05 * r);
        }
        out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size() * 4));
    }
    return 0;
}
