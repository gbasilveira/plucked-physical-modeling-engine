// SPDX-License-Identifier: MIT
// 0.5.2 measurement helper: strikes one key on the full Piano (shared board, unisons) and writes the
// bridge force that key's strings put on the board (N, float64, 48 kHz) for analysis in Python.
//   piano-tension-measure key velocity(0..1) seconds on|off out.f64   -> writes the force series
//   piano-tension-measure key modes                                    -> prints partial frequencies (Hz)
#include "piano.hpp"
#include "preset.hpp"
#include "pme/simd/denormals.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    pme::simd::ScopedFlushDenormals denormals;
    constexpr int fs = 48000;
    if (argc == 3 && std::string(argv[2]) == "modes") {
        pme::StringModes m;
        if (piano_mvp_v2::compilePresetString(piano_mvp_v2::parametersForKey(std::stoi(argv[1])), fs, m) != pme::Status::Ok) return 1;
        for (double w : m.omega) std::printf("%.6f\n", w / (2 * 3.14159265358979323846));
        return 0;
    }
    if (argc != 6) { std::cerr << "usage: piano-tension-measure key velocity seconds on|off out.f64\n"; return 2; }
    const int key = std::stoi(argv[1]);
    const double velocity = std::stod(argv[2]), seconds = std::stod(argv[3]);
    piano_mvp_v4::Piano piano;
    piano.setTensionModulation(std::string(argv[4]) == "on");
    piano.setSympatheticLimit(0);
    piano.setVoiceLimit(piano_mvp_v4::Piano::maxPolyphony);
    if (!piano.prepare(fs)) return 1;
    const int frames = static_cast<int>(seconds * fs);
    std::vector<double> force(frames);
    piano.noteOn(key, velocity);
    for (int i = 0; i < frames; ++i) {
        float l, r; piano.sample(l, r);
        force[i] = piano.portForce(key);
    }
    FILE* f = std::fopen(argv[5], "wb");
    if (!f) return 1;
    std::fwrite(force.data(), sizeof(double), force.size(), f);
    std::fclose(f);
    return 0;
}
