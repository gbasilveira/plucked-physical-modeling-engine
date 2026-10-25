// SPDX-License-Identifier: MIT
// 0.5.2 CPU benchmark with the plug-in settings (12 voices, sympathetic cap 8, single thread).
//   piano-tension-bench chopin events maxkey   Chopin fixture, timing 52-58 s of the performance
//   piano-tension-bench cluster - maxkey       12 keys 48..59 at 0.75 with the sustain pedal, 4 s
//   piano-tension-bench cluster-nopedal - maxkey   same without the pedal (0.5.3: damped strings leak)
// Optional 4th argument (0.5.3): leaky limit (0..88) or off (0.5.2 behaviour); default the plug-in's cap.
// maxkey: highest key with tension modulation (20 = tension off, 108 = all keys).
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) { std::cerr << "usage: piano-tension-bench chopin|cluster events|- maxkey\n"; return 2; }
    constexpr int fs = 48000;
    const std::string mode = argv[1];
    const int maxKey = std::stoi(argv[3]);
    piano_mvp_v4::Piano piano;
    piano.setTensionModulation(maxKey > 20);
    piano.setTensionMaxKey(maxKey > 20 ? maxKey : 108);
    piano.setVoiceLimit(piano_mvp_v4::Piano::maxVoices);
    piano.setSympatheticLimit(piano_mvp_v4::Piano::pluginSympatheticLimit);
    const std::string leaky = argc == 5 ? argv[4] : std::to_string(piano_mvp_v4::Piano::pluginLeakyLimit);
    piano.setLeakyDampers(leaky != "off");
    if (leaky != "off") piano.setLeakyLimit(std::stoi(leaky));
    if (!piano.prepare(fs)) return 1;
    pme::simd::ScopedFlushDenormals denormals;
    struct Event { int frame; std::array<std::uint8_t, 3> d; };
    std::vector<Event> events;
    int from = 0, to = 4 * fs;
    if (mode == "chopin") {
        std::ifstream in(argv[2]);
        int frame, status, a, b;
        while (in >> frame >> status >> a >> b)
            events.push_back({frame, {std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)}});
        from = 52 * fs; to = 58 * fs;
    } else {
        piano.sustain(mode == "cluster");
        for (int k = 48; k < 60; ++k) events.push_back({0, {144, std::uint8_t(k), 95}});
    }
    std::size_t cursor = 0;
    double struckSum = 0; int samples = 0;
    std::chrono::steady_clock::time_point begin;
    for (int i = 0; i < to; ++i) {
        if (i == from) begin = std::chrono::steady_clock::now();
        while (cursor < events.size() && events[cursor].frame <= i) {
            piano_mvp_v4::dispatchMidi(piano, events[cursor].d.data(), 3); ++cursor;
        }
        float l, r; piano.sample(l, r);
        if (i >= from && i % 4800 == 0) { struckSum += piano.activeVoices(); ++samples; }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double dur = double(to - from) / fs;
    std::cout << mode << " maxkey " << maxKey << " leaky " << leaky << ": " << s << " s for " << dur << " s -> " << dur / s
              << "x real time, mean struck voices " << struckSum / samples << ", faults " << piano.numericalFaults() << "\n";
}
