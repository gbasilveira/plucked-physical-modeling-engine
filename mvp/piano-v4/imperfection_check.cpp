// SPDX-License-Identifier: MIT
// 0.5.5 tests of the imperfections: 0.5.5-T1 plan bounds, 0.5.5-T2 passive contact with retuned strings
// and uneven hammer, 0.5.5-T3 determinism (threads, block vs sample), 0.5.5-T4 amount 0 is 0.5.4
// (also after a detour through other amounts), 0.5.5-T5 amounts take effect at the next note-on only,
// 0.5.5-T6 imperfection changes the sound.
#include "piano.hpp"
#include "preset.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace piano_mvp_v4;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr double fs = 48000;

static void planChecks() {
    int withFalse = 0, distinct = 0, multi = 0;
    double minSpread = 1e9, maxSpread = 0, maxMean = 0, maxStrength = 0, minAmp = 1e9, maxLag = 0, minFalseHz = 1e9, maxFalseHz = 0;
    std::vector<double> spreads;
    for (int key = 21; key <= 108; ++key) {
        const int count = Piano::stringsForKey(key);
        const auto p = Piano::makeImperfectionPlan(key, count, 60);
        const auto q = Piano::makeImperfectionPlan(key, count, 60);
        require(std::memcmp(&p, &q, sizeof p) == 0, "plan is not deterministic");
        if (count < 2) {
            require(p.falseCount == 0 && p.cents[0] == 0 && p.strengthDev[0] == 0 && p.lagSeconds[0] == 0, "single-string keys must be unchanged");
            continue;
        }
        ++multi;
        double lo = 1e9, hi = -1e9, mean = 0, sLo = 1e9, sHi = -1e9, tLo = 1e9, tHi = -1e9, sm = 0;
        for (int j = 0; j < count; ++j) {
            lo = std::min(lo, p.cents[j]); hi = std::max(hi, p.cents[j]); mean += p.cents[j] / count;
            sLo = std::min(sLo, p.strengthDev[j]); sHi = std::max(sHi, p.strengthDev[j]); sm += p.strengthDev[j] / count;
            tLo = std::min(tLo, p.lagSeconds[j]); tHi = std::max(tHi, p.lagSeconds[j]);
        }
        minSpread = std::min(minSpread, hi - lo); maxSpread = std::max(maxSpread, hi - lo); spreads.push_back(hi - lo);
        maxMean = std::max(maxMean, std::abs(mean));
        require(std::abs(sm) < 1e-12, "strength deviations must have zero mean");
        const double amp = std::max(sHi, -sLo);
        minAmp = std::min(minAmp, amp); maxStrength = std::max(maxStrength, amp);
        require(tLo == 0.0 && tHi >= 30e-6 - 1e-12 && tHi <= 90e-6 + 1e-12, "contact lag outside 0 / 30..90 us");
        maxLag = std::max(maxLag, tHi);
        require(p.falseCount >= 0 && p.falseCount <= 4, "false partial count");
        if (p.falseCount) {
            ++withFalse;
            require(p.falseCount >= 2, "a false-string key splits 2..4 partials");
            for (int k = 0; k < p.falseCount; ++k) {
                const double a = std::abs(p.falseHz[k]);
                minFalseHz = std::min(minFalseHz, a); maxFalseHz = std::max(maxFalseHz, a);
                require(a >= 0.3 - 1e-12 && a <= 2.0 + 1e-12, "false-string shift outside 0.3..2 Hz");
                require(p.falseMode[k] >= 1 && p.falseMode[k] <= 13 && p.falseString[k] >= 0 && p.falseString[k] < count, "false-string index");
                for (int l = 0; l < k; ++l) require(p.falseMode[l] != p.falseMode[k], "duplicate false partial");
            }
        }
    }
    std::sort(spreads.begin(), spreads.end());
    for (std::size_t i = 1; i < spreads.size(); ++i) distinct += spreads[i] - spreads[i - 1] > 1e-9;
    require(minSpread >= 0.3 - 1e-9 && maxSpread <= 2.0 + 1e-9, "unison spread outside 0.3..2 cents");
    require(maxMean < 1e-12, "per-key mean unison offset must be zero");
    require(minAmp >= 0.05 - 1e-12 && maxStrength <= 0.15 + 1e-12, "contact deviation outside 5..15 %");
    require(distinct > multi / 2, "unison offsets must differ between keys");
    require(withFalse >= multi / 8 && withFalse <= multi / 2, "false strings must be a minority of keys");
    std::cout << "0.5.5-T1 " << multi << " multi-string keys: unison spread " << minSpread << ".." << maxSpread << " cents (max |mean| " << maxMean
              << "), contact deviation amplitude " << minAmp << ".." << maxStrength << ", lag up to " << maxLag * 1e6 << " us, "
              << withFalse << " keys with false partials, shifts " << minFalseHz << ".." << maxFalseHz << " Hz\n";
}

// 0.5.5-T2: a retuned, unevenly struck 3-string note (key 60) keeps the energy balance of 0.5.2-T2/T3.
static void passivity() {
    for (bool lossless : {true, false}) for (double amount : {1.0, 2.0}) {
        const int key = 60;
        const auto preset = piano_mvp_v2::parametersForKey(key);
        std::array<pme::StringModes, 3> modes;
        pme::exciters::StruckNoteDesc d;
        const auto plan = Piano::makeImperfectionPlan(key, 3, 60);
        for (int j = 0; j < 3; ++j) {
            auto string = preset;
            string.string.f1Target *= std::exp2((j - 1.0) * 0.8 / 1200.0);
            if (lossless) require(pme::compileString(string.string, fs, 12000.0, {}, modes[j]) == pme::Status::Ok, "compile");
            else require(piano_mvp_v2::compilePresetString(string, fs, modes[j]) == pme::Status::Ok, "compile");
            d.strings.push_back(&modes[j]);
        }
        d.hammer = preset.hammer; d.hammer.K /= 3; d.hammer.muHC = lossless ? 0.0 : 0.5; d.sav.driftRelaxation = !lossless;
        const double ea = preset.string.ECore * std::numbers::pi * preset.string.dCore * preset.string.dCore / 4.0;
        const std::array<double, 3> eas{ea, ea, ea};
        TensionStruckNote note;
        require(note.configure(d, fs, nullptr, eas) == pme::Status::Ok, "configure");
        if (lossless) note.setTensionFloor(0);
        std::vector<double> ratio(modes[0].omega.size());
        std::array<double, 3> strength{}, lag{};
        for (int j = 0; j < 3; ++j) {
            std::fill(ratio.begin(), ratio.end(), std::exp2(amount * (plan.cents[j] - plan.baseCents[j]) / 1200.0));
            note.retuneString(j, modes[j], ratio);
            strength[j] = 1.0 + amount * plan.strengthDev[j]; lag[j] = amount * plan.lagSeconds[j];
        }
        note.setContactImperfection(strength, lag);
        require(note.launch(0.9, 0) == pme::Status::Ok, "launch");
        auto total = [&] { return note.energy() + note.releasedEnergy() + note.resetEnergyRemoved(); };
        note.step();
        const double e0 = total();
        double worst = 0, rise = 0;
        for (int i = 0; i < 96000; ++i) {
            note.step();
            const double r = total() / e0 - 1;
            require(std::isfinite(r), "non-finite energy");
            worst = std::max(worst, std::abs(r)); rise = std::max(rise, r);
        }
        if (lossless) require(worst < 1e-9, "imperfect lossless note does not conserve energy");
        else require(rise < 1e-9 && total() / e0 < 1, "imperfect lossy note gains energy");
        std::cout << "0.5.5-T2 " << (lossless ? "lossless" : "lossy") << " amount " << amount << ": " << (lossless ? "max |E/E0-1| " : "max rise ")
                  << (lossless ? worst : rise) << ", final E/E0 " << total() / e0 << "\n";
    }
}

struct Render { std::vector<float> l, r; };
// Notes: a pedalled chord plus a repeated key, 0.7 s, via blockStep or sample().
static Render play(double amount, int threads, bool blocks, Piano::Quality q, bool detour = false) {
    Piano p;
    require(p.setWorkerThreads(threads), "threads");
    require(p.prepare(fs), "prepare");
    p.setQuality(q);
    if (detour) { p.setImperfection(1.7); p.noteOn(60, 0.8); p.noteOn(33, 0.5); p.noteOn(100, 0.6); for (int i = 0; i < 4800; ++i) { float a, b; p.sample(a, b); } p.reset(); }
    p.setImperfection(amount);
    const int frames = static_cast<int>(0.7 * fs);
    Render out; out.l.resize(frames); out.r.resize(frames);
    p.sustain(true);
    const auto events = [&](int i) {
        if (i == 0) for (int k : {40, 52, 60, 64, 72, 90}) p.noteOn(k, 0.3 + 0.1 * (k % 5));
        if (i == 12000) { p.noteOn(60, 0.9); p.noteOn(28, 0.7); }
        if (i == 24000) p.noteOff(64);
    };
    if (blocks) {
        for (int base = 0; base < frames; base += 128) {
            const int n = std::min(128, frames - base);
            p.blockBegin(n);
            for (int i = base; i < base + n; ++i) { events(i); p.blockStep(); }
            p.blockEnd(out.l.data() + base, out.r.data() + base);
        }
    } else for (int i = 0; i < frames; ++i) { events(i); p.sample(out.l[i], out.r[i]); }
    require(p.numericalFaults() == 0, "faults");
    return out;
}
static bool same(const Render& a, const Render& b) {
    return a.l.size() == b.l.size() && !std::memcmp(a.l.data(), b.l.data(), a.l.size() * 4) && !std::memcmp(a.r.data(), b.r.data(), a.r.size() * 4);
}
static double rmsDiff(const Render& a, const Render& b) {
    double d = 0, e = 0;
    for (std::size_t i = 0; i < a.l.size(); ++i) { d += double(a.l[i] - b.l[i]) * (a.l[i] - b.l[i]); e += double(a.l[i]) * a.l[i]; }
    return std::sqrt(d / std::max(e, 1e-300));
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        planChecks();
        passivity();
        for (auto q : {Piano::Quality::Live, Piano::Quality::Balanced, Piano::Quality::Full}) {
            const char* name = q == Piano::Quality::Live ? "live" : (q == Piano::Quality::Balanced ? "balanced" : "full");
            const auto ref = play(1.0, 0, false, q);
            for (int t : {0, 1, 2, 3}) require(same(ref, play(1.0, t, true, q)), "imperfect render differs between sample() and blocks/threads");
            require(same(ref, play(1.0, 0, false, q)), "imperfect render is not repeatable");
            std::cout << "0.5.5-T3 " << name << ": amount 1 identical for sample() and block interface with 0..3 worker threads\n";
            const auto zero = play(0.0, 0, false, q), detour = play(0.0, 0, false, q, true);
            require(same(zero, detour), "amount 0 after a detour through other amounts is not the unmodified model");
            const double diff = rmsDiff(ref, zero);
            require(diff > 1e-3, "imperfection 1 should change the sound");
            std::cout << "0.5.5-T4/T6 " << name << ": amount 0 is bit-identical after a detour via 1.7; amount 1 vs 0 relative rms difference " << diff << "\n";
        }
        {
            // 0.5.5-T5: the amount is taken at the next note-on of the key.
            auto run = [&](bool change) {
                Piano p; require(p.prepare(fs), "prepare"); p.setQuality(Piano::Quality::Full);
                p.setImperfection(0.0); p.sustain(true); p.noteOn(60, 0.8);
                std::vector<float> l;
                for (int i = 0; i < 24000; ++i) { if (change && i == 6000) p.setImperfection(1.0); float a, b; p.sample(a, b); l.push_back(a); }
                p.noteOn(67, 0.6);                         // other key: key 60 keeps ringing as compiled
                for (int i = 0; i < 6000; ++i) { float a, b; p.sample(a, b); l.push_back(a); }
                return l;
            };
            const auto a = run(false), b = run(true);
            // 67 is a new key at amount 1 in b: differs after its note-on; before it, identical.
            require(!std::memcmp(a.data(), b.data(), 24000 * 4), "changing the amount altered a sounding note");
            require(std::memcmp(a.data() + 24000, b.data() + 24000, 6000 * 4) != 0, "the new key did not use the new amount");
            std::cout << "0.5.5-T5 amount change mid-note: sounding key unchanged for 0.5 s, next note-on of another key uses the new amount\n";
        }
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "PASS\n";
}
