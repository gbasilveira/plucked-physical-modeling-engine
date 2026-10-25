// SPDX-License-Identifier: MIT
// 0.5.4.1 deadline bench at the Piano level: plays an event file (or a key list) through the block interface
// at a given block size and sample rate, times every block (begin..end) against its deadline and reports the
// distribution, the output peak and the number of samples above 1.0.
//   piano-block-bench --events FILE [--from S] [--to S] | --keys 36,43,48 [--pedal] [--seconds S]
//       [--block N] [--fs HZ] [--quality live|balanced|full] [--threads N] [--velocity V] [--adaptive]
//       | --chords BASEKEY [--gap S] [--pedal] [--velocity V]
//       [--o-NAME VALUE ...]   (preset overrides: voices sym leaky tension trunc floor sleepforce sleeps)
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    std::string eventsFile, quality = "live", keyList; int chordBase = 0; double chordGap = 0.15;
    double from = 0, to = -1, fs = 48000, velocity = 0.8, seconds = 4;
    int block = 64, threads = 1; bool pedal = false, adaptive = false;
    std::vector<std::pair<std::string, double>> overrides;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--events") eventsFile = next(); else if (a == "--from") from = std::stod(next()); else if (a == "--to") to = std::stod(next());
        else if (a == "--chords") chordBase = std::stoi(next()); else if (a == "--gap") chordGap = std::stod(next());
        else if (a == "--keys") keyList = next(); else if (a == "--pedal") pedal = true; else if (a == "--seconds") seconds = std::stod(next());
        else if (a == "--block") block = std::stoi(next()); else if (a == "--fs") fs = std::stod(next());
        else if (a == "--quality") quality = next(); else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--velocity") velocity = std::stod(next()); else if (a == "--adaptive") adaptive = true;
        else if (a.rfind("--o-", 0) == 0) overrides.push_back({a.substr(4), std::stod(next())});
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    const auto q = quality == "full" ? Piano::Quality::Full : quality == "balanced" ? Piano::Quality::Balanced : Piano::Quality::Live;
    Piano piano;
    if (!piano.setWorkerThreads(threads) || !piano.prepare(fs)) return 1;
    piano.setQuality(q);
    piano.setActiveWorkers(threads);
    if (adaptive) piano.setAdaptive(true);
    if (!overrides.empty()) {
        auto o = Piano::preset(q);
        for (auto& [k, v] : overrides) {
            if (k == "voices") o.voices = int(v); else if (k == "sym") o.sympathetic = int(v); else if (k == "leaky") o.leaky = int(v);
            else if (k == "tension") o.tensionMaxKey = int(v); else if (k == "trunc") o.truncation = v; else if (k == "floor") o.tensionFloor = v;
            else if (k == "sleepforce") o.sleepForce = v; else if (k == "sleeps") o.sleepSeconds = v; else return 2;
        }
        piano.applyPreset(q, o);
    }
    piano.warmUp();
    pme::simd::ScopedFlushDenormals denormals;
    struct Event { int frame; std::array<std::uint8_t, 3> d; };
    std::vector<Event> events;
    int f0 = 0, f1 = int(seconds * fs);
    if (!eventsFile.empty()) {
        std::ifstream in(eventsFile);
        int frame, status, a, b;
        while (in >> frame >> status >> a >> b) events.push_back({frame, {std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)}});
        f0 = int(from * fs); f1 = int((to > 0 ? to : from + seconds) * fs);
    } else if (chordBase) {   // rolling 4-note chords from `chordBase`, every `gap` seconds (+ pedal when asked)
        if (pedal) events.push_back({0, {176, 64, 127}});
        for (int r = 0; r * chordGap < seconds; ++r)
            for (int k : {0, 7, 12, 16}) {
                const int key = chordBase + k + (r % 4) * 2;
                events.push_back({int(r * chordGap * fs) + 1 + k * 3, {144, std::uint8_t(key), std::uint8_t(velocity * 127)}});
                events.push_back({int((r + 1) * chordGap * fs * 0.9), {128, std::uint8_t(key), 0}});
            }
        std::stable_sort(events.begin(), events.end(), [](const Event& x, const Event& y) { return x.frame < y.frame; });
    } else {
        if (pedal) events.push_back({0, {176, 64, 127}});
        for (char* s = keyList.data(), *t; (t = std::strtok(s, ",")); s = nullptr)
            events.push_back({1, {144, std::uint8_t(std::atoi(t)), std::uint8_t(velocity * 127)}});
    }
    std::vector<float> l(block), r(block);
    std::vector<double> ratio;
    std::size_t cursor = 0; double peak = 0; std::uint64_t over = 0;
    // Start at the beginning of the file so that the pedal and held keys are right; time only [f0, f1).
    for (int i0 = 0; i0 < f1; i0 += block) {
        const int n = std::min(block, f1 - i0);
        const auto tb = std::chrono::steady_clock::now();
        piano.blockBegin(n);
        for (int i = i0; i < i0 + n; ++i) {
            while (cursor < events.size() && events[cursor].frame <= i) { dispatchMidi(piano, events[cursor].d.data(), 3); ++cursor; }
            piano.blockStep();
        }
        piano.blockEnd(l.data(), r.data());
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count();
        if (i0 >= f0) {
            ratio.push_back(dt * fs / n);
            for (int k = 0; k < n; ++k) { const double a = std::max(std::fabs(l[k]), std::fabs(r[k])); peak = std::max(peak, a); over += a > 1.0; }
        }
    }
    std::vector<double> u = ratio; std::sort(u.begin(), u.end());
    double mean = 0; for (double x : ratio) mean += x; mean /= double(ratio.size());
    std::size_t miss = 0; for (double x : ratio) miss += x > 1.0;
    const auto pct = [&](double p) { return u[std::min(u.size() - 1, std::size_t(double(u.size()) * p))]; };
    std::printf("%s block %d fs %.0f threads %d%s: mean %.2f p99 %.2f p99.9 %.2f max %.2f misses %zu/%zu peak %.3f over1 %llu stalls %llu shed %d\n",
                quality.c_str(), block, fs, threads, adaptive ? " adaptive" : "", mean, pct(0.99), pct(0.999), u.back(), miss, ratio.size(), peak,
                (unsigned long long)over, (unsigned long long)piano.radiationStalls(), piano.shedLevel());
}
