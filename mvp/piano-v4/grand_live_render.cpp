// SPDX-License-Identifier: MIT
// Piano 0.7c tool: renders a sample-timed event schedule (as piano-grand-render) through the real-time Piano class in Body Classic or
// Body Grand (block interface, or sample() with --sample), any quality, lid and microphone preset, and writes headerless float32
// stereo (frames interleaved L R) from frame 0 to the last event plus a tail.
//   piano-grand-live-render EVENTS OUT.f32 [--body classic|grand] [--quality live|balanced|full] [--lid 0..2] [--mic 0..2]
//        [--table FILE] [--threads N] [--block N] [--tail S] [--no-direct] [--sample] [--hash]
// --hash prints an FNV-1a hash of the output bits (determinism tests).
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace piano_mvp_v4;
    try {
        if (argc < 3) throw std::runtime_error("usage: piano-grand-live-render EVENTS OUT.f32 [options]");
        std::string body = "grand", quality = "full", tablePath; int lid = 0, mic = 0, threads = 1, block = 128; double tail = 4.0;
        bool direct = true, useSample = false, hash = false; int stride = 0;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&]() { if (i + 1 >= argc) throw std::runtime_error("missing value for " + a); return std::string(argv[++i]); };
            if (a == "--body") body = next(); else if (a == "--quality") quality = next(); else if (a == "--lid") lid = std::stoi(next());
            else if (a == "--mic") mic = std::stoi(next()); else if (a == "--table") tablePath = next(); else if (a == "--threads") threads = std::stoi(next());
            else if (a == "--block") block = std::stoi(next()); else if (a == "--tail") tail = std::stod(next());
            else if (a == "--stride") stride = std::stoi(next()); else if (a == "--no-direct") direct = false; else if (a == "--sample") useSample = true; else if (a == "--hash") hash = true;
            else throw std::runtime_error("unknown option " + a);
        }
        constexpr int fs = 48000;
        struct Event { int frame; std::array<std::uint8_t, 3> data; };
        std::vector<Event> events; {
            std::ifstream in(argv[1]); if (!in) throw std::runtime_error("cannot read events");
            int frame, status, a, b;
            while (in >> frame >> status >> a >> b) events.push_back({frame, {std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)}});
            if (events.empty()) throw std::runtime_error("empty schedule");
        }
        std::vector<std::uint8_t> blob;
        Piano piano;
        piano.setBody(body == "classic" ? Piano::Body::Classic : Piano::Body::Grand);
        if (!tablePath.empty()) {
            std::ifstream t(tablePath, std::ios::binary); blob.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
            piano.setGrandTableBlob(blob.data(), blob.size());
        }
        piano.setVoiceLimit(Piano::maxPolyphony);
        if (!piano.setWorkerThreads(threads)) throw std::runtime_error("--threads 0..7");
        if (!piano.prepare(fs)) throw std::runtime_error("prepare failed (no grand table?)");
        piano.setQuality(quality == "live" ? Piano::Quality::Live : quality == "balanced" ? Piano::Quality::Balanced : Piano::Quality::Full);
        piano.setActiveWorkers(threads);
        piano.setGrandHighStride(stride); piano.setGrandLid(lid); piano.setGrandMic(mic); piano.setGrandDirect(direct);
        const int end = events.back().frame + 1, finish = end + static_cast<int>(tail * fs);
        std::ofstream out(argv[2], std::ios::binary); if (!out) throw std::runtime_error("cannot write");
        pme::simd::ScopedFlushDenormals denormals;
        std::size_t cursor = 0; std::uint64_t h = 1469598103934665603ull; double energy = 0, peak = 0;
        std::vector<float> buf(2 * 4096);
        const auto emit = [&](const float* l, const float* r, int n) {
            for (int k = 0; k < n; ++k) {
                buf[2 * k] = l[k]; buf[2 * k + 1] = r[k]; energy += double(l[k]) * l[k] + double(r[k]) * r[k]; peak = std::max<double>(peak, std::max(std::fabs(l[k]), std::fabs(r[k])));
            }
            out.write(reinterpret_cast<const char*>(buf.data()), n * 8);
            if (hash) for (int k = 0; k < 2 * n; ++k) { std::uint32_t u; std::memcpy(&u, &buf[k], 4); h = (h ^ u) * 1099511628211ull; }
        };
        const auto begin = std::chrono::steady_clock::now();
        const auto step = [&](int i) {
            if (i < end) while (cursor < events.size() && events[cursor].frame == i) { dispatchMidi(piano, events[cursor].data.data(), 3); ++cursor; }
            if (i == end) { piano.sustain(false); piano.allNotesOff(); }
        };
        if (useSample) {
            std::vector<float> l(1), r(1);
            for (int i = 0; i < finish; ++i) { step(i); piano.sample(l[0], r[0]); emit(l.data(), r.data(), 1); }
        } else {
            block = std::clamp(block, 1, int(Piano::maxBlockFrames));
            std::vector<float> l(block), r(block);
            for (int i0 = 0; i0 < finish; i0 += block) {
                const int n = std::min(block, finish - i0);
                piano.blockBegin(n);
                for (int i = i0; i < i0 + n; ++i) { step(i); piano.blockStep(); }
                piano.blockEnd(l.data(), r.data());
                emit(l.data(), r.data(), n);
            }
        }
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::printf("%.1f s, rms %.5f peak %.4f faults %llu, %.1f s elapsed (x%.2f real time)\n", finish / double(fs), std::sqrt(energy / (2.0 * finish)), peak,
                    (unsigned long long)piano.numericalFaults(), el, finish / double(fs) / el);
        std::printf("high stride %d, %zu high modes, %zu low poles\n", piano.grandRadiation().highStride(), piano.grandRadiation().highModeCount(), piano.grandRadiation().lowPoleCount());
        if (hash) std::printf("hash %016llx\n", (unsigned long long)h);
        return piano.numericalFaults() ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
