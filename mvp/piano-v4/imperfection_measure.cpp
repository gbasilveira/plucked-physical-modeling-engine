// SPDX-License-Identifier: MIT
// 0.5.5: A/B demo and decay measurements of the imperfections.
//   piano-imperfection-measure demo out.wav        C4 and C5 mf held 4 s, perfect (0) then imperfect (1), then a chord both ways
//   piano-imperfection-measure decay out.json      two-stage decay of the bridge-force envelope of C4/C5 at 0 and 1
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
static void word(std::ostream& f, std::uint32_t x, int bytes) { for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((x >> (8 * i)) & 255)); }
// Plays `keys` at velocity v for `seconds` (no pedal), returns the stereo mix.
static std::vector<float> play(double amount, const std::vector<int>& keys, double v, double seconds, std::vector<float>* right = nullptr) {
    Piano p; p.setSharedBoard(true);
    if (!p.prepare(fs)) throw std::runtime_error("prepare");
    p.setQuality(Piano::Quality::Full);
    p.setImperfection(amount);
    const int frames = static_cast<int>(seconds * fs);
    std::vector<float> l(frames), r(frames);
    for (int i = 0; i < frames; ++i) {
        if (i == 0) for (int k : keys) p.noteOn(k, v);
        p.sample(l[i], r[i]);
    }
    if (right) *right = r;
    return l;
}
int main(int argc, char** argv) {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        if (argc != 3) throw std::runtime_error("usage: piano-imperfection-measure demo|decay out");
        const std::string mode = argv[1];
        if (mode == "demo") {
            std::vector<float> L, R;
            const auto add = [&](double amount, const std::vector<int>& keys, double secs, double gap) {
                std::vector<float> r; const auto l = play(amount, keys, 0.62, secs, &r);   // mf
                L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
                L.insert(L.end(), static_cast<std::size_t>(gap * fs), 0.f); R.insert(R.end(), static_cast<std::size_t>(gap * fs), 0.f);
            };
            for (int k : {60, 72}) { add(0, {k}, 4, 0.8); add(1, {k}, 4, 1.6); }
            add(0, {48, 55, 60, 64, 67, 72}, 4, 0.8); add(1, {48, 55, 60, 64, 67, 72}, 4, 0.5);
            double peak = 0; for (std::size_t i = 0; i < L.size(); ++i) peak = std::max({peak, double(std::abs(L[i])), double(std::abs(R[i]))});
            if (!(peak > 1e-4) || peak > 1) throw std::runtime_error("bad peak");
            std::ofstream out(argv[2], std::ios::binary);
            out.write("RIFF", 4); word(out, 36 + L.size() * 4, 4); out.write("WAVEfmt ", 8);
            word(out, 16, 4); word(out, 1, 2); word(out, 2, 2); word(out, fs, 4); word(out, fs * 4, 4); word(out, 4, 2); word(out, 16, 2);
            out.write("data", 4); word(out, L.size() * 4, 4);
            for (std::size_t i = 0; i < L.size(); ++i) for (float x : {L[i], R[i]}) word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x * 32767))), 2);
            std::cout << "wrote " << argv[2] << " " << L.size() / double(fs) << " s, peak " << peak << "\n";
        } else if (mode == "decay") {
            std::ofstream js(argv[2]);
            js << "{\n";
            bool first = true;
            for (int key : {60, 72}) for (double amount : {0.0, 1.0}) {
                const double secs = 14.0;
                const auto x = play(amount, {key}, 0.62, secs);
                // 50 ms RMS envelope in dB re its value at 0.1-0.15 s.
                const int hop = fs / 20;
                std::vector<double> env;
                for (std::size_t i = 0; i + hop <= x.size(); i += hop) { double e = 0; for (int k = 0; k < hop; ++k) e += double(x[i + k]) * x[i + k]; env.push_back(10 * std::log10(e / hop + 1e-30)); }
                const double ref = env[2];
                // Two-stage fit: slope (dB/s) of a straight line over 0.25-1.5 s (prompt) and 3-9 s (aftersound).
                const auto slope = [&](double t0, double t1) {
                    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
                    for (int i = static_cast<int>(t0 * 20); i < static_cast<int>(t1 * 20); ++i) { const double t = i / 20.0; sx += t; sy += env[i]; sxx += t * t; sxy += t * env[i]; ++n; }
                    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
                };
                // Roughness: rms deviation (dB) of the envelope from a straight line over 1-9 s, a measure of beating.
                const double s1 = slope(0.25, 1.5), s2 = slope(3, 9);
                double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
                for (int i = 20; i < 180; ++i) { const double t = i / 20.0; sx += t; sy += env[i]; sxx += t * t; sxy += t * env[i]; ++n; }
                const double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n;
                double dev = 0; for (int i = 20; i < 180; ++i) { const double r = env[i] - (a + b * i / 20.0); dev += r * r; }
                js << (first ? "" : ",\n") << "  \"key" << key << "_amount" << amount << "\": {\"prompt_db_per_s\": " << s1 << ", \"after_db_per_s\": " << s2
                   << ", \"level_at_1s_db\": " << env[20] - ref << ", \"level_at_5s_db\": " << env[100] - ref << ", \"level_at_9s_db\": " << env[180] - ref
                   << ", \"envelope_roughness_db_rms_1_9s\": " << std::sqrt(dev / n) << "}";
                first = false;
                std::cout << "key " << key << " amount " << amount << ": prompt " << s1 << " dB/s, aftersound " << s2 << " dB/s, level 1 s/5 s/9 s " << env[20] - ref << "/" << env[100] - ref << "/" << env[180] - ref << " dB, roughness " << std::sqrt(dev / n) << " dB\n";
            }
            js << "\n}\n";
        } else throw std::runtime_error("mode");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
