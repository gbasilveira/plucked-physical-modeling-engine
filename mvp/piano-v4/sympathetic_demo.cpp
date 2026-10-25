// SPDX-License-Identifier: MIT
// Diagnostic for sympathetic resonance (0.5.1). 14 s, stereo 48 kHz, 16-bit, fixed gain +12.04 dB.
//  0-6.5 s: C3+E3+G3 have their dampers lifted but are not struck; staccato C4 at 1 s, C5 at 3 s.
//  7-14 s : sustain pedal down, one loud A1 (key 33) held 0.5 s.
// Modes: (default) 0.5.3, --previous (0.5.2, no leaky dampers), --v051 (0.5.1, no tension), --shared-pan, --v050 (0.5.0: unstruck strings are never simulated).
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static void word(std::ostream& f, std::uint32_t n, int bytes) {
    for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((n >> (8 * i)) & 255));
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("usage: piano-sympathetic-demo out.wav [--shared-pan|--previous(0.5.3)|--v052|--v051|--v050]");
        const std::string mode = argc == 3 ? argv[2] : "";
        if (!mode.empty() && mode != "--shared-pan" && mode != "--previous" && mode != "--v052" && mode != "--v051" && mode != "--v050") throw std::runtime_error("unknown mode");
        constexpr int fs = 48000, frames = 14 * fs;
        piano_mvp_v4::Piano piano;
        piano.setSharedBoard(mode != "--v050");
        piano.setGrandBridgeGeometry(mode.empty() || mode == "--previous" || mode == "--v052" || mode == "--v051");
        piano.setTensionModulation(mode.empty() || mode == "--previous" || mode == "--v052");
        piano.setLeakyDampers(mode.empty() || mode == "--previous");
        piano.setVoiceLimit(piano_mvp_v4::Piano::maxPolyphony);
        if (!piano.prepare(fs)) throw std::runtime_error("prepare failed");
        std::ofstream out(argv[1], std::ios::binary);
        if (!out) throw std::runtime_error("cannot write");
        out.write("RIFF", 4); word(out, 36 + frames * 4, 4); out.write("WAVEfmt ", 8);
        word(out, 16, 4); word(out, 1, 2); word(out, 2, 2); word(out, fs, 4); word(out, fs * 4, 4);
        word(out, 4, 2); word(out, 16, 2); out.write("data", 4); word(out, frames * 4, 4);
        pme::simd::ScopedFlushDenormals denormals;
        double peak = 0;
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) {
            if (i == 0) for (int k : {48, 52, 55}) piano.liftDamper(k);
            if (i == fs) piano.noteOn(60, 0.7);
            if (i == fs + fs * 3 / 10) piano.noteOff(60);
            if (i == 3 * fs) piano.noteOn(72, 0.7);
            if (i == 3 * fs + fs * 3 / 10) piano.noteOff(72);
            if (i == 6 * fs + fs / 2) for (int k : {48, 52, 55}) piano.noteOff(k);
            if (i == 7 * fs) { piano.sustain(true); piano.noteOn(33, 0.95); }
            if (i == 7 * fs + fs / 2) piano.noteOff(33);
            float l, r; piano.sample(l, r);
            for (double x : {4.0 * l, 4.0 * r}) {
                if (!std::isfinite(x) || std::abs(x) >= 1) throw std::runtime_error("invalid/clipping audio");
                peak = std::max(peak, std::abs(x));
                word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x * 32767))), 2);
            }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (piano.numericalFaults()) throw std::runtime_error("numerical fault");
        std::cout << argv[1] << ": peak " << peak << ", " << elapsed << " s for 14 s (x" << 14.0 / elapsed << " realtime)\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
