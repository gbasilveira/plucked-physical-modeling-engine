// SPDX-License-Identifier: MIT
// 0.5.2 tests of TensionStruckNote (tension_note.*): 0.5.2-T1 potential equals the core
// TensionModulationPotential, 0.5.2-T2 lossless energy, 0.5.2-T3 lossy passivity, 0.5.2-T4 tension
// off reproduces StruckNote bit-for-bit, 0.5.2-T5 tension raises the pitch of a loud string.
#include "preset.hpp"
#include "tension_note.hpp"
#include "pme/nonlinear/tension.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

using namespace piano_mvp_v4;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr double fs = 48000;

struct Setup {
    pme::StringModes modes;
    pme::exciters::StruckNoteDesc desc;
    double ea{};
};
static Setup make(int key, bool lossless, double muHC, bool relax) {
    Setup s;
    const auto preset = piano_mvp_v2::parametersForKey(key);
    if (lossless) {
        auto p = preset.string;
        require(pme::compileString(p, fs, 12000.0, {}, s.modes) == pme::Status::Ok, "compile failed");
    } else require(piano_mvp_v2::compilePresetString(preset, fs, s.modes) == pme::Status::Ok, "compile failed");
    s.desc.strings.push_back(&s.modes);
    s.desc.hammer = preset.hammer;
    s.desc.hammer.muHC = muHC;
    s.desc.sav.driftRelaxation = relax;
    s.ea = preset.string.ECore * std::numbers::pi * preset.string.dCore * preset.string.dCore / 4.0;
    return s;
}

// 0.5.2-T1: the vertical-only potential is the core potential with zero horizontal modes.
static void potentialCheck() {
    for (int key : {24, 45, 60, 84}) {
        auto s = make(key, true, 0, true);
        const std::size_t n = s.modes.k.size();
        std::vector<double> k2(n);
        for (std::size_t i = 0; i < n; ++i) k2[i] = s.modes.k[i] * s.modes.k[i];
        const double c = (s.ea - s.modes.tension) / (2 * s.modes.L);
        pme::nl::TensionModulationPotential<double> core;
        require(core.configure(k2, c) == pme::Status::Ok, "core potential");
        VerticalTensionPotential mine;
        mine.configure({{0, k2, c}});
        std::mt19937 rng(key);
        std::uniform_real_distribution<double> d(-1e-4, 1e-4);
        std::vector<double> zm(n + 1), zc(2 * n + 1, 0.0);
        for (std::size_t i = 0; i < n; ++i) zm[i] = zc[i] = d(rng) / (i + 1);
        std::vector<double> gm(n + 1), gc(2 * n + 1);
        mine.gradient(zm, gm); core.gradient(zc, gc);
        double err = std::abs(mine.value(zm) / core.value(zc) - 1);
        for (std::size_t i = 0; i < n; ++i) err = std::max(err, std::abs(gm[i] - gc[i]) / (std::abs(gc[i]) + 1e-30));
        require(err < 1e-13 && gm[n] == 0, "vertical tension potential differs from the core potential");
        std::cout << "0.5.2-T1 key " << key << ": value/gradient vs core TensionModulationPotential, max rel diff " << err
                  << ", EA " << s.ea << " N, T " << s.modes.tension << " N, c " << c << " N/m\n";
    }
}

// 0.5.2-T2/T3: modified energy. Total = SAV energy + energy carried off by the released hammer +
// energy removed by psi resets (SPEC-NL Drift control 1).
static double runEnergy(int key, double velocity, bool lossless, double& finalRatio, double& peakDeltaT) {
    auto s = make(key, lossless, lossless ? 0.0 : 0.5, !lossless);
    TensionStruckNote note;
    const double eas[1] = {s.ea};
    require(note.configure(s.desc, fs, nullptr, eas) == pme::Status::Ok, "note configure");
    if (lossless) note.setTensionFloor(0);
    require(note.launch(velocity, 0) == pme::Status::Ok, "launch");
    auto total = [&] { return note.energy() + note.releasedEnergy() + note.resetEnergyRemoved(); };
    note.step();
    const double e0 = total();
    require(e0 > 0, "no energy");
    double worst = 0, rise = 0; peakDeltaT = 0;
    for (int i = 0; i < 96000; ++i) {
        note.step();
        const double r = total() / e0 - 1;
        worst = std::max(worst, std::abs(r)); rise = std::max(rise, r);
        peakDeltaT = std::max(peakDeltaT, note.deltaT(0));
        require(std::isfinite(r), "non-finite energy");
    }
    finalRatio = total() / e0;
    if (lossless) require(worst < 1e-9, "lossless energy not conserved");
    else require(rise < 1e-9 && finalRatio < 1, "lossy energy increased");
    return lossless ? worst : rise;
}
static void energyChecks() {
    for (int key : {24, 33, 45, 60}) for (double v : {0.5, 1.0}) {
        double f, dT;
        const double worst = runEnergy(key, v, true, f, dT);
        std::cout << "0.5.2-T2 lossless key " << key << " nu " << v << ": max |E/E0-1| " << worst
                  << ", peak DeltaT " << dT << " N over 2 s\n";
    }
    for (int key : {24, 33, 60, 84}) {
        double f, dT;
        const double rise = runEnergy(key, 1.0, false, f, dT);
        std::cout << "0.5.2-T3 lossy key " << key << ": max rise " << rise << ", final E/E0 " << f
                  << ", peak DeltaT " << dT << " N\n";
    }
}

// 0.5.2-T4 / T5: with tension off the arithmetic equals pme::exciters::StruckNote; with it on, a
// loud bass string rings higher (Duffing hardening) than the linear one.
static double zeroCrossFrequency(const std::vector<double>& x, double t0, double t1) {
    int first = -1, last = -1, count = 0;
    for (std::size_t i = std::size_t(t0 * fs) + 1; i < std::size_t(t1 * fs); ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double frac = -x[i - 1] / (x[i] - x[i - 1]);
            const double pos = double(i - 1) + frac;
            if (first < 0) first = int(pos * 1024);   // fixed point is enough only for ordering
            last = int(pos * 1024); ++count;
        }
    return count > 1 ? (count - 1) * fs * 1024.0 / (last - first) : 0.0;
}
static void bitCheck() {
    for (int key : {33, 60}) {
        auto s = make(key, false, 0.5, true);
        pme::exciters::StruckNote<double> ref;
        TensionStruckNote off;
        require(ref.configure(s.desc, fs, nullptr) == pme::Status::Ok, "ref configure");
        require(off.configure(s.desc, fs, nullptr) == pme::Status::Ok, "off configure");
        require(ref.launch(0.8, 0) == pme::Status::Ok && off.launch(0.8, 0) == pme::Status::Ok, "launch");
        bool same = true;
        for (int i = 0; i < 48000 && same; ++i) {
            ref.step(); off.step();
            for (std::size_t n = 0; n < ref.segment().size(); ++n)
                same = same && ref.segment().q()[n] == off.segment().q()[n];
        }
        require(same, "tension off differs from StruckNote");
        std::cout << "0.5.2-T4 key " << key << ": tension off is bit-identical to pme::exciters::StruckNote over 1 s\n";
    }
}
static void pitchCheck() {
    auto s = make(33, false, 0.5, true);
    TensionStruckNote lin, nl;
    const double eas[1] = {s.ea};
    require(lin.configure(s.desc, fs, nullptr) == pme::Status::Ok && nl.configure(s.desc, fs, nullptr, eas) == pme::Status::Ok, "configure");
    lin.launch(1.0, 0); nl.launch(1.0, 0);
    std::vector<double> a, b;
    for (int i = 0; i < int(1.5 * fs); ++i) {
        lin.step(); nl.step();
        // Mode-1 coordinate is a clean readout of the fundamental.
        a.push_back(lin.segment().q()[0]); b.push_back(nl.segment().q()[0]);
    }
    const double fl = zeroCrossFrequency(a, 0.05, 0.25), fn = zeroCrossFrequency(b, 0.05, 0.25);
    const double cents = 1200 * std::log2(fn / fl);
    std::cout << "0.5.2-T5 A1 nu 1.0: mode-1 frequency 50-250 ms linear " << fl << " Hz, with tension " << fn
              << " Hz, " << cents << " cents\n";
    require(cents > 0, "tension modulation should raise the pitch of a loud string");
}
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try { potentialCheck(); energyChecks(); bitCheck(); pitchCheck(); }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
