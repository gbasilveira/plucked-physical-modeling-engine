// SPDX-License-Identifier: MIT
// 0.5.4 identity / throughput bench at the Piano level (no plug-in): renders a scenario sample by
// sample and prints an FNV-1a hash of the float output bits plus the real-time factor.
//   piano-hash-bench SCENARIO [--events FILE] [--from S] [--to S] [--quality live|balanced|full]
//                    [--threads N] [--max-key N] [--out file.f32]
// Scenarios: cluster, cluster-nopedal, chopin (needs --events), repeat (same key re-struck while held),
// chords (fast repeated chords with pedal).
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    if (argc < 2) return 2;
    constexpr int fs = 48000;
    std::string scenario = argv[1], eventsFile, quality = "plugin-legacy", outFile;
    std::vector<std::pair<std::string, double>> overrides;   // --o-NAME VALUE: override a preset field
    int keys = 12;
    double from = -1, to = -1; int threads = 0, block = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--events") eventsFile = argv[++i];
        else if (a == "--from") from = std::stod(argv[++i]);
        else if (a == "--to") to = std::stod(argv[++i]);
        else if (a == "--quality") quality = argv[++i];
        else if (a == "--threads") threads = std::stoi(argv[++i]);
        else if (a == "--out") outFile = argv[++i];
        else if (a == "--block") block = std::stoi(argv[++i]);
        else if (a == "--keys") keys = std::stoi(argv[++i]);
        else if (a.rfind("--o-", 0) == 0) overrides.push_back({a.substr(4), std::stod(argv[++i])});
    }
    Piano piano;
#ifdef PIANO_HAS_QUALITY
    piano.setWorkerThreads(threads);
    if (quality == "live") piano.setQuality(Piano::Quality::Live);
    else if (quality == "balanced") piano.setQuality(Piano::Quality::Balanced);
    else if (quality == "full") piano.setQuality(Piano::Quality::Full);
    else if (quality == "plugin-legacy") {
        piano.setVoiceLimit(Piano::maxVoices); piano.setSympatheticLimit(Piano::pluginSympatheticLimit);
        piano.setTensionMaxKey(Piano::pluginTensionMaxKey); piano.setLeakyLimit(Piano::pluginLeakyLimit);
    }
    if (quality == "full") piano.setVoiceLimit(88);
    if (quality == "full" || quality == "live" || quality == "balanced") {}
#else
    piano.setVoiceLimit(Piano::maxVoices); piano.setSympatheticLimit(Piano::pluginSympatheticLimit);
    piano.setTensionMaxKey(Piano::pluginTensionMaxKey); piano.setLeakyLimit(Piano::pluginLeakyLimit);
#endif
    if (!piano.prepare(fs)) return 1;
#ifdef PIANO_HAS_QUALITY
    if (!overrides.empty()) {
        auto o = Piano::preset(quality == "live" ? Piano::Quality::Live : quality == "balanced" ? Piano::Quality::Balanced : Piano::Quality::Full);
        for (auto& [k, v] : overrides) {
            if (k == "voices") o.voices = int(v); else if (k == "sym") o.sympathetic = int(v); else if (k == "leaky") o.leaky = int(v);
            else if (k == "tension") o.tensionMaxKey = int(v); else if (k == "trunc") o.truncation = v; else if (k == "fast") o.fast = v != 0;
            else if (k == "sleepforce") o.sleepForce = v; else if (k == "sleeps") o.sleepSeconds = v; else if (k == "floor") o.tensionFloor = v;
            else if (k == "idle") o.idleSkip = v != 0; else return 2;
        }
        piano.applyPreset(Piano::Quality::Live, o);
    }
#endif
    pme::simd::ScopedFlushDenormals denormals;
    struct Event { int frame; std::array<std::uint8_t, 3> d; };
    std::vector<Event> events;
    int f0 = 0, f1 = 4 * fs;
    if (scenario == "chopin") {
        std::ifstream in(eventsFile);
        int frame, status, a, b;
        while (in >> frame >> status >> a >> b) events.push_back({frame, {std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)}});
        f0 = int((from < 0 ? 52 : from) * fs); f1 = int((to < 0 ? 58 : to) * fs);
    } else if (scenario == "cluster" || scenario == "cluster-nopedal") {
        if (scenario == "cluster") events.push_back({0, {176, 64, 127}});
        for (int k = 48; k < 48 + keys; ++k) events.push_back({1, {144, std::uint8_t(k), 95}});
        if (to > 0) f1 = int(to * fs);
    } else if (scenario == "repeat") {
        events.push_back({0, {176, 64, 127}});
        for (int r = 0; r < 24; ++r) {   // overlapping same-key notes: on, on, off, ...
            events.push_back({1 + r * 4000, {144, 60, 100}});
            events.push_back({1 + r * 4000 + 1500, {144, 60, 90}});
            events.push_back({1 + r * 4000 + 2500, {128, 60, 0}});
        }
        if (to > 0) f1 = int(to * fs);
    } else if (scenario == "chords") {
        events.push_back({0, {176, 64, 127}});
        for (int r = 0; r < 40; ++r)
            for (int k : {43, 50, 55, 59, 62, 67})
                events.push_back({1 + r * 3600 + (k % 5) * 40, {144, std::uint8_t(k + (r % 3) * 2), 105}});
        if (to > 0) f1 = int(to * fs);
    }
    std::vector<float> outBuf;
    std::size_t cursor = 0;
    std::uint64_t h = 1469598103934665603ull;
    std::chrono::steady_clock::time_point begin;
    const auto emit = [&](int i, float l, float r) {
        if (i < f0) return;
        std::uint32_t bits[2]; std::memcpy(&bits[0], &l, 4); std::memcpy(&bits[1], &r, 4);
        for (auto b : bits) for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 255; h *= 1099511628211ull; }
        if (!outFile.empty()) { outBuf.push_back(l); outBuf.push_back(r); }
    };
    if (block == 0) {
        for (int i = 0; i < f1; ++i) {
            if (i == f0) begin = std::chrono::steady_clock::now();
            while (cursor < events.size() && events[cursor].frame <= i) { dispatchMidi(piano, events[cursor].d.data(), 3); ++cursor; }
            float l, r; piano.sample(l, r);
            emit(i, l, r);
        }
    } else {
        std::vector<float> bl(block), br(block);
        for (int i0 = 0; i0 < f1; i0 += block) {
            const int n = std::min(block, f1 - i0);
            if (i0 <= f0 && f0 < i0 + block) begin = std::chrono::steady_clock::now();
            const auto tb = std::chrono::steady_clock::now();
            piano.blockBegin(n);
            for (int i = i0; i < i0 + n; ++i) {
                while (cursor < events.size() && events[cursor].frame <= i) { dispatchMidi(piano, events[cursor].d.data(), 3); ++cursor; }
                piano.blockStep();
            }
            const auto te = std::chrono::steady_clock::now();
            piano.blockEnd(bl.data(), br.data());
            const double tot = std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count() * fs / block;
            if (tot > 1.0) std::cerr << "block " << i0 / block << " ratio " << tot << " (strings part " << std::chrono::duration<double>(te - tb).count() * fs / block << ")\n";
            for (int k = 0; k < n; ++k) emit(i0 + k, bl[k], br[k]);
        }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double dur = double(f1 - f0) / fs;
    std::cout << scenario << " q=" << quality << " threads=" << threads << " hash=" << std::hex << h << std::dec
              << " time=" << s << " rtf=" << dur / s << " faults=" << piano.numericalFaults() << " stolen=" << piano.stolenTasks() << " stalls=" << piano.poolStalls() << " tasks[0..3]=" << piano.tasksRunBy(0) << "," << piano.tasksRunBy(1) << "," << piano.tasksRunBy(2) << "," << piano.tasksRunBy(3) << "\n";
    if (!outFile.empty()) { std::ofstream o(outFile, std::ios::binary); o.write(reinterpret_cast<const char*>(outBuf.data()), outBuf.size() * 4); }
}
