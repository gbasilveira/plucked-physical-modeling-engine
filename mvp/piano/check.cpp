// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static double energy(piano_mvp::Piano& p, int frames) {
    double e = 0;
    for (int i = 0; i < frames; ++i) {
        float l, r; p.sample(l, r);
        require(std::isfinite(l) && std::isfinite(r) && std::abs(l) <= 1 && std::abs(r) <= 1,
                "invalid audio");
        e += l * l + r * r;
    }
    return e;
}
int main() {
    try {
        piano_mvp::Piano p;
        require(!p.prepare(0), "invalid sample rate accepted");
        for (double fs : {44100., 48000., 96000.}) {
            require(p.prepare(fs), "prepare failed");
            require(energy(p, 256) == 0, "idle is not silent");
            for (int key = 21; key <= 108; ++key) {
                p.reset(); p.noteOn(key, 0.8);
                require(energy(p, 2048) > 1e-9, "silent key");
            }
        }
        require(p.prepare(48000), "prepare failed");
        p.noteOn(60, 0.2); const double soft = energy(p, 12000);
        p.reset(); p.noteOn(60, 0.9); const double loud = energy(p, 12000);
        require(loud > soft * 2, "velocity has insufficient effect");
        p.reset(); p.sustain(true); p.noteOn(60, 0.7); energy(p, 4800); p.noteOff(60);
        require(energy(p, 24000) > 1e-5 && p.activeVoices() == 1, "sustain did not hold note");
        p.sustain(false); energy(p, 48000);
        require(p.activeVoices() == 0, "released note did not retire");
        p.reset();
        for (int key = 21; key <= 108; ++key) p.noteOn(key, 0.8);
        require(p.activeVoices() == 16, "polyphony limit failed");
        require(energy(p, 24000) > 1e-5, "chord silent");
        p.reset(); require(energy(p, 1024) == 0, "reset left an audio tail");
        std::cout << "PASS: 88 keys at 3 sample rates, velocity, sustain, release, 16 voices, reset\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
