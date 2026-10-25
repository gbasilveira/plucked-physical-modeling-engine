// SPDX-License-Identifier: MIT
// 0.5.3 listening demo for the "unsustained strings feel muffled" feedback. No sustain pedal anywhere.
// A short phrase (staccato notes in three registers, a legato line, a six-note chord) is rendered
// three times with identical gain, separated by 1.5 s of silence: (1) 0.5.2 (damped strings are not
// simulated), (2) 0.5.3 (leaky dampers), (3) 0.5.3 without the 0.4.9 bridge-top mass low-pass
// (diagnostic only, to attribute "muffled": defaults are unchanged). 16-bit stereo 48 kHz, fixed gain +12.04 dB.
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void word(std::ostream& f, std::uint32_t n, int bytes) {
    for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((n >> (8 * i)) & 255));
}
struct Ev { double t; int key; double vel; };   // vel 0 = note off
static std::vector<Ev> phrase() {
    std::vector<Ev> e;
    const auto note = [&](double t, int key, double len, double vel) { e.push_back({t, key, vel}); e.push_back({t + len, key, 0}); };
    note(0.0, 48, 0.12, 0.7); note(0.55, 64, 0.12, 0.7); note(1.1, 76, 0.1, 0.65); note(1.6, 36, 0.15, 0.8);   // staccato
    const int line[] = {60, 62, 64, 65, 67, 69, 67, 64};   // legato: each note held slightly past the next onset
    for (int i = 0; i < 8; ++i) note(2.4 + 0.38 * i, line[i], 0.44, 0.55 + 0.03 * i);
    for (int k : {36, 48, 55, 64, 72, 79}) note(5.8, k, 1.4, 0.7);                                          // chord
    std::sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.vel == 0 && b.vel != 0); });
    return e;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: piano-unpedalled-demo out.wav");
        constexpr int fs = 48000, seg = 9 * fs, gap = fs * 3 / 2;
        const auto events = phrase();
        std::vector<float> out;
        pme::simd::ScopedFlushDenormals denormals;
        for (int variant = 0; variant < 3; ++variant) {
            const bool leaky = variant > 0;
            piano_mvp_v4::Piano piano;
            piano.setLeakyDampers(leaky);
            piano.setBridgeMassLoading(variant != 2);
            piano.setVoiceLimit(piano_mvp_v4::Piano::maxPolyphony);
            if (!piano.prepare(fs)) throw std::runtime_error("prepare failed");
            std::size_t cursor = 0;
            for (int i = 0; i < seg; ++i) {
                while (cursor < events.size() && events[cursor].t * fs <= i) {
                    const auto& e = events[cursor++];
                    if (e.vel > 0) piano.noteOn(e.key, e.vel); else piano.noteOff(e.key);
                }
                float l, r; piano.sample(l, r); out.push_back(l); out.push_back(r);
            }
            if (piano.numericalFaults()) throw std::runtime_error("numerical fault");
            if (variant < 2) out.insert(out.end(), 2 * gap, 0.0f);
        }
        std::ofstream f(argv[1], std::ios::binary);
        if (!f) throw std::runtime_error("cannot write");
        const std::uint32_t bytes = static_cast<std::uint32_t>(out.size() * 2);
        f.write("RIFF", 4); word(f, 36 + bytes, 4); f.write("WAVEfmt ", 8);
        word(f, 16, 4); word(f, 1, 2); word(f, 2, 2); word(f, fs, 4); word(f, fs * 4, 4); word(f, 4, 2); word(f, 16, 2);
        f.write("data", 4); word(f, bytes, 4);
        double peak = 0;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double x = 4.0 * out[i] * std::min(1.0, (out.size() - 1 - i) / (0.05 * fs * 2));   // final 50 ms fade
            if (!std::isfinite(x) || std::abs(x) >= 1) throw std::runtime_error("invalid/clipping audio");
            peak = std::max(peak, std::abs(x));
            word(f, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x * 32767))), 2);
        }
        std::cout << argv[1] << ": 0.5.2, 0.5.3, 0.5.3 without bridge mass; 9 s each + 1.5 s gap, peak " << peak << "\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
