// SPDX-License-Identifier: MIT
// 0.5.5 tests of the Voices setting and the steal policy: 0.5.5-V1 limit mapping, 0.5.5-V2 steals the quietest
// voice (not the newest, not one in hammer flight), 0.5.5-V3 the steal fade does not click, 0.5.5-V4
// determinism (sample() vs blocks, 0..3 workers), 0.5.5-V5 no steal below the limit leaves the output unchanged.
#include "piano.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace piano_mvp_v4;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr double fs = 48000;

struct Out { std::vector<float> l, r; std::uint64_t steals{}; std::vector<int> stealFrames; };
// 24 pedalled notes, one every 40 ms in a spread of keys, 3 s.
static Out play(const Piano::Preset& preset, int threads, bool blocks, int fade = -1) {
    Piano p;
    require(p.setWorkerThreads(threads) && p.prepare(fs), "prepare");
    p.applyPreset(Piano::Quality::Live, preset);
    if (fade > 0) p.setStealFadeSamples(fade);
    const int frames = static_cast<int>(3.0 * fs);
    Out o; o.l.resize(frames); o.r.resize(frames);
    p.sustain(true);
    const auto ev = [&](int i) {
        if (i % 1920 == 0 && i / 1920 < 24) {
            const int n = i / 1920;
            p.noteOn(30 + (n * 7) % 60, 0.4 + 0.02 * (n % 20));
        }
    };
    if (blocks) {
        for (int base = 0; base < frames; base += 128) {
            const int n = std::min(128, frames - base);
            p.blockBegin(n);
            for (int i = base; i < base + n; ++i) { ev(i); p.blockStep(); }
            p.blockEnd(o.l.data() + base, o.r.data() + base);
        }
    } else { std::uint64_t last = 0; for (int i = 0; i < frames; ++i) { ev(i); p.sample(o.l[i], o.r[i]); if (p.voiceSteals() != last) { last = p.voiceSteals(); o.stealFrames.push_back(i); } } }
    require(p.numericalFaults() == 0, "faults");
    o.steals = p.voiceSteals();
    return o;
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        {   // V1: mapping
            Piano p; require(p.prepare(fs), "prepare");
            struct Row { Piano::Quality q; int set, v, s, k; };
            const Row rows[] = {{Piano::Quality::Live, 0, 16, 12, 6}, {Piano::Quality::Live, 16, 16, 12, 6}, {Piano::Quality::Live, 32, 32, 17, 9},
                                {Piano::Quality::Live, 48, 48, 21, 11}, {Piano::Quality::Live, 64, 64, 24, 12}, {Piano::Quality::Balanced, 0, 24, 16, 8},
                                {Piano::Quality::Balanced, 48, 48, 23, 12}, {Piano::Quality::Full, 16, 88, 88, 88}};
            for (const auto& r : rows) {
                p.setVoices(r.set); p.setQuality(r.q);
                require(p.voiceLimit() == r.v && p.sympatheticLimit() == r.s && p.leakyLimit() == r.k, "limit mapping");
                p.setVoices(0); p.setVoices(r.set);   // order independence
                require(p.voiceLimit() == r.v && p.sympatheticLimit() == r.s && p.leakyLimit() == r.k, "limit mapping (setting after quality)");
            }
            std::cout << "0.5.5-V1 Live Auto/16/32/48/64 -> struck/sympathetic/leaky 16/12/6, 16/12/6, 32/17/9, 48/21/11, 64/24/12; Full ignores the setting\n";
        }
        auto live = Piano::preset(Piano::Quality::Live);
        auto small = live; small.voices = 16;
        auto big = live; big.voices = 88;
        const auto a = play(small, 0, false), b = play(big, 0, false);
        require(a.steals > 0 && b.steals == 0, "scenario must steal at 16 and not at 88");
        // V3: step of the difference to the run that keeps every voice, inside the steal windows, with the 8 ms fade
        // and with a 1-sample "cut"; the fade must not be worse and must be small against the signal's own steps.
        const auto stepOf = [&](const Out& x, double& sigStep) {
            double step = 0; sigStep = 0;
            for (std::size_t i = 1; i < x.l.size(); ++i) {
                bool in = false;
                for (int s : x.stealFrames) in = in || (int(i) >= s && int(i) <= s + 450);
                sigStep = std::max(sigStep, std::abs(double(b.l[i]) - b.l[i - 1]));
                if (in) step = std::max(step, std::abs((double(x.l[i]) - b.l[i]) - (double(x.l[i - 1]) - b.l[i - 1])));
            }
            return step;
        };
        double sig = 0;
        const double fadeStep = stepOf(a, sig);
        const auto cut = play(small, 0, false, 1);
        const double cutStep = stepOf(cut, sig);
        double peak = 0; for (float v : b.l) peak = std::max<double>(peak, std::abs(v));
        std::cout << "0.5.5-V2/V3 24 pedalled notes: " << a.steals << " steals at 16 voices; peak " << peak << "; max one-sample step of the difference to the 88-voice run inside the steal windows: 8 ms fade "
                  << fadeStep << ", 1-sample cut " << cutStep << " (signal's own max step " << sig << ")\n";
        require(fadeStep <= cutStep && fadeStep < 0.12 * sig, "steal fade produces a step");
        for (int t : {0, 1, 3}) {
            const auto c = play(small, t, true);
            require(c.steals == a.steals && !std::memcmp(a.l.data(), c.l.data(), a.l.size() * 4) && !std::memcmp(a.r.data(), c.r.data(), a.r.size() * 4),
                    "stealing is not deterministic across block interface / workers");
        }
        std::cout << "0.5.5-V4 steal run identical for sample() and blocks with 0, 1 and 3 workers\n";
        // V5: a limit above the demand changes nothing.
        auto mid = live; mid.voices = 40;
        const auto c = play(mid, 0, false);
        require(c.steals == 0 && !std::memcmp(b.l.data(), c.l.data(), b.l.size() * 4), "unused voice headroom altered the output");
        std::cout << "0.5.5-V5 limit 40 (no steal) is bit-identical to limit 88\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "PASS\n";
}
