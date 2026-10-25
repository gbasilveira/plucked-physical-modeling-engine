// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void word(std::ostream& f, std::uint32_t n, int bytes) {
    for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((n >> (8 * i)) & 255));
}
int main(int argc0, char** argv0) {
    try {
        // Developer options (0.5.3), any position: --damper-rate R, --damper-exp P, --leaky-limit N,
        // --leaky-release, --no-bridge-mass. Remaining arguments are the positional ones below.
        std::vector<char*> positional{argv0[0]};
        double damperRate = piano_mvp_v4::Piano::defaultDamperRate, damperExp = 0;
        int leakyLimit = piano_mvp_v4::Piano::maxPolyphony;
        bool leakyRelease = false, noBridgeMass = false;
        for (int i = 1; i < argc0; ++i) {
            const std::string a = argv0[i];
            if (a == "--damper-rate" && i + 1 < argc0) damperRate = std::stod(argv0[++i]);
            else if (a == "--damper-exp" && i + 1 < argc0) damperExp = std::stod(argv0[++i]);
            else if (a == "--leaky-limit" && i + 1 < argc0) leakyLimit = std::stoi(argv0[++i]);
            else if (a == "--leaky-release") leakyRelease = true;
            else if (a == "--no-bridge-mass") noBridgeMass = true;   // diagnostic only
            else positional.push_back(argv0[i]);
        }
        const int argc = static_cast<int>(positional.size());
        char** argv = positional.data();
        const std::string path = argc > 1 ? argv[1] : "piano-v4-demo.wav";
        const int key = argc > 2 ? std::stoi(argv[2]) : -1;
        std::string pathName = argc > 3 ? argv[3] : "coupled";
        if (pathName == "previous") pathName = "coupled";   // 0.5.4: previous = 0.5.3 (= current model); v052 = old previous
        const double velocity = argc > 4 ? std::stod(argv[4]) : 0.75;
        const bool wide24 = argc == 6 && std::string(argv[5]) == "--24bit";
        if ((argc > 5 && !wide24) || argc > 6 || (argc > 2 && key != -1 && (key < 21 || key > 108)) ||
            (pathName != "fixed" && pathName != "coupled" && pathName != "single" && pathName != "unisons" && pathName != "v052" && pathName != "v051" && pathName != "v050" && pathName != "v049" && pathName != "sharedpan" && pathName != "v048" && pathName != "v046") ||
            !std::isfinite(velocity) || velocity <= 0 || velocity > 1)
            throw std::runtime_error("usage: piano-render [output.wav] [MIDI-key or -1 for demo] [fixed|coupled|single|unisons|previous(0.5.3)|v052|v051|v050|sharedpan|v049|v048|v046] [velocity 0..1] [--24bit]");
        constexpr int fs = 48000, seconds = 14, frames = fs * seconds;
        piano_mvp_v4::Piano piano;
        piano.setBridgeFeedback(pathName != "fixed");
        piano.setUnisonStrings(pathName != "single");
        // "coupled"/"fixed" are 0.5.3; "v052" is 0.5.2 (no leaky dampers); "previous" is 0.5.3; "v051" is 0.5.1 (also no
        // tension modulation); "v050" is 0.5.0.
        const bool now0501 = pathName == "coupled" || pathName == "fixed" || pathName == "sharedpan" || pathName == "v052" || pathName == "v051";
        piano.setTensionModulation(pathName == "coupled" || pathName == "fixed" || pathName == "v052");
        piano.setLeakyDampers(pathName == "coupled" || pathName == "fixed");
        if (!piano.setDamperLaw(damperRate, damperExp) || !piano.setLeakyLimit(leakyLimit)) throw std::runtime_error("bad damper options");
        piano.setLeakyRelease(leakyRelease);
        piano.setSoftFelt(now0501 || pathName == "v050" || pathName == "v049" || pathName == "v048");
        // 0.4.9 bridge/string physics stays on; "v050" is 0.5.0 (private bridges), "v049" is 0.4.9,
        // "v048" is 0.4.8, "v046" is 0.4.6, "sharedpan" is 0.5.1 coupling with 0.5.0 bridge positions.
        const bool current = now0501 || pathName == "v050" || pathName == "v049";
        piano.setResidualBridgeLoss(now0501 || pathName == "v050" || pathName == "v049" || pathName == "v048" || pathName == "v046");
        piano.setSharedBoard(now0501);
        piano.setGrandBridgeGeometry(pathName != "sharedpan");  // v050 has no shared board, so it is moot
        // 0.5.0: offline renders never steal voices; "v049" and older keep 12.
        const bool now = now0501 || pathName == "v050";
        piano.setVoiceLimit(now ? piano_mvp_v4::Piano::maxPolyphony : piano_mvp_v4::Piano::maxVoices);
        piano.setUnaCorda(now); piano.setUndampedTreble(now);
        piano.setBridgeMassLoading(current && !noBridgeMass);
        piano.setSaturatingStringLoss(current);
        if (!piano.prepare(fs)) throw std::runtime_error("could not prepare piano");
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("could not open output");
        out.write("RIFF", 4); word(out, 36 + frames * (wide24 ? 6 : 4), 4); out.write("WAVEfmt ", 8);
        word(out, 16, 4); word(out, 1, 2); word(out, 2, 2); word(out, fs, 4);
        word(out, fs * (wide24 ? 6 : 4), 4); word(out, wide24 ? 6 : 4, 2); word(out, wide24 ? 24 : 16, 2);
        out.write("data", 4); word(out, frames * (wide24 ? 6 : 4), 4);
        const auto start = std::chrono::steady_clock::now();
        pme::simd::ScopedFlushDenormals denormals;
        double peak = 0;
        for (int i = 0; i < frames; ++i) {
            if (key >= 0) {
                if (i == 0) piano.noteOn(key, velocity);
                if (i == fs * 4) piano.noteOff(key);
            } else {
                constexpr int melody[] = {60, 64, 67, 72, 71, 67, 64, 60};
                if (i < fs * 4 && i % (fs / 2) == 0)
                    piano.noteOn(melody[i / (fs / 2)], 0.45 + 0.06 * (i / (fs / 2)));
                if (i < fs * 4 && i % (fs / 2) == fs * 2 / 5)
                    piano.noteOff(melody[i / (fs / 2)]);
                if (i == fs * 5) { piano.sustain(true); for (int k : {48, 60, 64, 67}) piano.noteOn(k, 0.75); }
                if (i == fs * 6) for (int k : {48, 60, 64, 67}) piano.noteOff(k);
                if (i == fs * 9) piano.sustain(false);
            }
            float l, r; piano.sample(l, r);
            for (float x : {l, r}) {
                if (!std::isfinite(x) || std::abs(x) > 1)
                    throw std::runtime_error("non-finite or over-full-scale audio; refusing a clipped WAV");
                peak = std::max(peak, std::abs(static_cast<double>(x)));
                if (wide24) word(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(x * 8388607.0))) & 0xffffff, 3);
                else word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x * 32767))), 2);
            }
        }
        if (!out) throw std::runtime_error("failed writing WAV");
        if (piano.numericalFaults()) throw std::runtime_error("a voice encountered non-finite physics");
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << path << ": 14 seconds, stereo 48 kHz, peak " << peak
                  << ", rendered in " << elapsed << " s; peak bridge motion "
                  << piano.peakBridgeMotion() << " m\n";
        return peak > 1e-5 ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
