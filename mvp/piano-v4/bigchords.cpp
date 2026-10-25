// SPDX-License-Identifier: MIT
// 0.5.5: the "big chords" pattern of piano-rt-stress (scenario bigchords): a new pedalled 10-note chord every 1.5 s,
// each note held 5 s. Usage:
//   piano-bigchords render out.wav [voicesX]   Live Voices 16, Live Voices X (default 32), then Full, 14 s each
//   piano-bigchords count                      steal counts of Live at Voices 16/32/48/64 and Full (offline, no timing)
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using piano_mvp_v4::Piano;
constexpr int fs = 48000;
struct Ev { int frame; bool on; int key, vel; };
static std::vector<Ev> pattern(int chords) {
    static const int chord[] = {28, 35, 40, 47, 52, 59, 64, 69, 76, 83};
    std::vector<Ev> e;
    for (int r = 0; r < chords; ++r) for (int i = 0; i < 10; ++i) {
        const int k = chord[i] + ((r * 5 + i) % 4) * 2 - (i > 6 ? 2 : 0);
        const int on = 1 + r * (fs * 3 / 2) + i * 60;
        e.push_back({on, true, k, 96 + (i * 7 + r * 3) % 24});
        e.push_back({on + fs * 5, false, k, 0});
    }
    std::stable_sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.frame < b.frame; });
    return e;
}
// quality: 0 Live, 2 Full; voices: 0 Auto.
static std::vector<float> run(int quality, int voices, double seconds, std::uint64_t& steals, std::vector<float>* right) {
    Piano p; if (!p.prepare(fs)) throw std::runtime_error("prepare");
    p.setQuality(quality == 2 ? Piano::Quality::Full : Piano::Quality::Live);
    p.setVoices(voices); p.setImperfection(1.0);
    const int frames = static_cast<int>(seconds * fs);
    std::vector<float> l(frames), r(frames);
    const auto ev = pattern(10);
    std::size_t c = 0;
    p.sustain(true);
    for (int i = 0; i < frames; ++i) {
        while (c < ev.size() && ev[c].frame <= i) { if (ev[c].on) p.noteOn(ev[c].key, ev[c].vel / 127.0); else p.noteOff(ev[c].key); ++c; }
        p.sample(l[i], r[i]);
    }
    steals = p.voiceSteals();
    if (p.numericalFaults()) throw std::runtime_error("faults");
    if (right) *right = r;
    return l;
}
static void word(std::ostream& f, std::uint32_t x, int bytes) { for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((x >> (8 * i)) & 255)); }
int main(int argc, char** argv) {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        if (argc < 2) throw std::runtime_error("usage: piano-bigchords render out.wav [voices] | count");
        const std::string mode = argv[1];
        if (mode == "count") {
            for (int v : {16, 32, 48, 64}) { std::uint64_t s; run(0, v, 14, s, nullptr); std::cout << "Live voices " << v << ": steals " << s << "\n"; }
            std::uint64_t s; run(2, 0, 14, s, nullptr); std::cout << "Full: steals " << s << "\n";
        } else if (mode == "render" && argc >= 3) {
            const int x = argc > 3 ? std::stoi(argv[3]) : 32;
            std::vector<float> L, R;
            double peak = 0;
            for (auto [q, v] : {std::pair{0, 16}, std::pair{0, x}, std::pair{2, 0}}) {
                std::uint64_t s; std::vector<float> r;
                const auto l = run(q, v, 14, s, &r);
                L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
                L.insert(L.end(), 2 * fs, 0.f); R.insert(R.end(), 2 * fs, 0.f);
                std::cout << (q == 2 ? "Full" : "Live voices " + std::to_string(v)) << ": steals " << s << "\n";
            }
            for (std::size_t i = 0; i < L.size(); ++i) peak = std::max({peak, double(std::abs(L[i])), double(std::abs(R[i]))});
            if (!(peak > 1e-4) || peak > 1) throw std::runtime_error("bad peak");
            std::ofstream out(argv[2], std::ios::binary);
            out.write("RIFF", 4); word(out, 36 + L.size() * 4, 4); out.write("WAVEfmt ", 8);
            word(out, 16, 4); word(out, 1, 2); word(out, 2, 2); word(out, fs, 4); word(out, fs * 4, 4); word(out, 4, 2); word(out, 16, 2);
            out.write("data", 4); word(out, L.size() * 4, 4);
            for (std::size_t i = 0; i < L.size(); ++i) for (float x2 : {L[i], R[i]}) word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x2 * 32767))), 2);
            std::cout << "wrote " << argv[2] << " " << L.size() / double(fs) << " s, peak " << peak << "\n";
        } else throw std::runtime_error("mode");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
