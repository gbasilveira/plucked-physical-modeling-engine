// SPDX-License-Identifier: MIT
// 0.6 step 2 tools.
//   piano-baked-fit synth FIT OUT.wav [SECONDS]       fitted string -> baked radiation -> float WAV (mono notes)
//   piano-baked-fit ab OUT.wav FIT36 FIT60 FIT84 [FIT_DIR_unused]   A/B file: Full then fitted for C2, C4, C6 (mf) and the chord
//   piano-baked-fit cost KEY MIDI_VEL                  cost per voice: Live and Full Piano against the fitted string
#include "common.hpp"
#include "fitted_string.hpp"
#include "grand_board.hpp"
#include <map>
using pb::Piano;
using namespace piano_mvp_v4;

struct Chain {   // fitted strings -> bridge-mass low pass -> baked radiation (as Piano does for a struck port)
    GrandBoard board;
    std::vector<piano_baked::FittedString> strings;
    explicit Chain(const std::vector<std::string>& fits) {
        std::vector<double> phi; buildParticipation(true, phi);
        board.configure(pb::kFs, phi, 128, false, true); board.setBaked(true);
        strings.resize(fits.size());
        for (std::size_t i = 0; i < fits.size(); ++i) if (!strings[i].load(fits[i])) throw std::runtime_error("cannot load " + fits[i]);
    }
    void step(float& l, float& r) {
        std::array<std::uint8_t, 88> port{}, light{}; std::array<double, 88> g{};
        int n = 0;
        for (auto& s : strings) { const int p = s.key() - 21; port[n] = std::uint8_t(p); g[n++] = board.lowpassStep(p, s.next()); }
        board.accumulateDrives(port.data(), g.data(), light.data(), n);
        board.radiationModes(0, kBoardModeCount);
        double a, b; board.radiationFinish(a, b);
        l = float(0.05 * a); r = float(0.05 * b);
    }
};
static pb::Stereo fitted(const std::vector<std::string>& fits, int frames) {
    pme::simd::ScopedFlushDenormals ftz;
    Chain c(fits); pb::Stereo o; o.l.assign(frames, 0.f); o.r.assign(frames, 0.f);
    for (int i = 0; i < frames; ++i) c.step(o.l[i], o.r[i]);
    return o;
}
static pb::Stereo fullNotes(const std::vector<int>& keys, int vel, int frames) {
    Piano p; pb::configure(p, Piano::Quality::Full, Piano::Radiation::Modal, 1);
    std::vector<pb::Ev> ev;
    for (int k : keys) ev.push_back({10, {144, std::uint8_t(k), std::uint8_t(vel)}});
    return pb::render(p, ev, frames);
}
static pb::Stereo fullNotesIsolated(const std::vector<int>& keys, int vel, int frames) {   // no sympathetic / leaky strings
    Piano p; p.setSympatheticStrings(false); p.setLeakyDampers(false);
    pb::configure(p, Piano::Quality::Full, Piano::Radiation::Modal, 1);
    std::vector<pb::Ev> ev;
    for (int k : keys) ev.push_back({10, {144, std::uint8_t(k), std::uint8_t(vel)}});
    return pb::render(p, ev, frames);
}
static void fadeOut(pb::Stereo& s, int n) { const int N = int(s.l.size()); for (int i = 0; i < n; ++i) { const float w = 0.5f * (1 + std::cos(float(M_PI) * (i + 0.5f) / n)); s.l[N - n + i] *= w; s.r[N - n + i] *= w; } }
static void append(pb::Stereo& dst, const pb::Stereo& src) { dst.l.insert(dst.l.end(), src.l.begin(), src.l.end()); dst.r.insert(dst.r.end(), src.r.begin(), src.r.end()); }
static void gap(pb::Stereo& dst, double seconds) { dst.l.insert(dst.l.end(), std::size_t(seconds * pb::kFs), 0.f); dst.r.insert(dst.r.end(), std::size_t(seconds * pb::kFs), 0.f); }

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "synth" && argc >= 4) {
            const int frames = int((argc > 4 ? std::stod(argv[4]) : 8.0) * pb::kFs);
            pb::writeWav(argv[3], fitted({argv[2]}, frames), false);
        } else if (cmd == "force" && argc >= 5) {   // FORCE.f64 KEY OUT.wav: the captured Full force through the baked radiation (pipeline floor)
            std::ifstream f(argv[2], std::ios::binary); std::vector<double> F; double x;
            while (f.read(reinterpret_cast<char*>(&x), 8)) F.push_back(x);
            const int key = std::stoi(argv[3]);
            std::vector<double> phi; buildParticipation(true, phi);
            GrandBoard b; b.configure(pb::kFs, phi, 128, false, true); b.setBaked(true);
            pme::simd::ScopedFlushDenormals ftz;
            pb::Stereo o; o.l.assign(F.size(), 0.f); o.r = o.l;
            const std::uint8_t port = std::uint8_t(key - 21), light = 0;
            for (std::size_t i = 0; i < F.size(); ++i) {
                const double g = b.lowpassStep(port, F[i]);
                b.accumulateDrives(&port, &g, &light, 1); b.radiationModes(0, kBoardModeCount);
                double l, r; b.radiationFinish(l, r); o.l[i] = float(0.05 * l); o.r[i] = float(0.05 * r);
            }
            pb::writeWav(argv[4], o, false);
        } else if (cmd == "ab" && argc >= 6) {
            const int frames = 5 * pb::kFs; const int vel = 76;
            const std::vector<std::string> fits{argv[3], argv[4], argv[5]};
            pb::Stereo out;
            const int keys[3] = {36, 60, 84};
            for (int i = 0; i < 3; ++i) {
                auto a = fullNotes({keys[i]}, vel, frames); fadeOut(a, 7200);
                auto b = fitted({fits[i]}, frames); fadeOut(b, 7200);
                append(out, a); gap(out, 0.6); append(out, b); gap(out, 1.4);
            }
            auto a = fullNotes({36, 60, 84}, vel, frames); fadeOut(a, 7200);
            auto b = fitted(fits, frames); fadeOut(b, 7200);
            append(out, a); gap(out, 0.6); append(out, b); gap(out, 0.5);
            pb::writeWav(argv[2], out, true);
            double pk = 0; for (float x : out.l) pk = std::max(pk, double(std::abs(x)));
            std::printf("A/B %.1f s, peak %.3f\n", out.l.size() / double(pb::kFs), pk);
        } else if (cmd == "ab3" && argc >= 6) {   // OUT FIT36 FIT60 FIT84: Full, Full without sympathetic/leaky strings, fitted
            const int frames = 5 * pb::kFs; const int vel = 76;
            const std::vector<std::string> fits{argv[3], argv[4], argv[5]};
            pb::Stereo out;
            const int keys[3] = {36, 60, 84};
            for (int i = 0; i < 3; ++i) {
                auto a = fullNotes({keys[i]}, vel, frames); fadeOut(a, 7200);
                auto c = fullNotesIsolated({keys[i]}, vel, frames); fadeOut(c, 7200);
                auto b = fitted({fits[i]}, frames); fadeOut(b, 7200);
                append(out, a); gap(out, 0.6); append(out, c); gap(out, 0.6); append(out, b); gap(out, 1.4);
            }
            auto a = fullNotes({36, 60, 84}, vel, frames); fadeOut(a, 7200);
            auto c = fullNotesIsolated({36, 60, 84}, vel, frames); fadeOut(c, 7200);
            auto b = fitted(fits, frames); fadeOut(b, 7200);
            append(out, a); gap(out, 0.6); append(out, c); gap(out, 0.6); append(out, b); gap(out, 0.5);
            pb::writeWav(argv[2], out, true);
            std::printf("A/B/C %.1f s\n", out.l.size() / double(pb::kFs));
        } else if (cmd == "cost" && argc >= 4) {
            const int key = std::stoi(argv[2]), vel = std::stoi(argv[3]);
            const double ghz = 3.6;
            pme::simd::ScopedFlushDenormals ftz;
            // Piano whole pipeline: ns per sample between 1 s and 3 s, block interface, no helper thread. `bg` notes sound too.
            const auto pianoCost = [&](Piano::Quality q, bool withKey, const std::vector<int>& bg) {
                double best = 1e30;
                for (int rep = 0; rep < 3; ++rep) {
                    Piano p; pb::configure(p, q, Piano::Radiation::Auto, 0);
                    p.warmUp();
                    std::vector<pb::Ev> ev; for (int k : bg) ev.push_back({10, {144, std::uint8_t(k), 76}});
                    if (withKey) ev.push_back({10, {144, std::uint8_t(key), std::uint8_t(vel)}});
                    std::vector<float> l(128), r(128); std::size_t cur = 0; double t = 0;
                    for (int i0 = 0; i0 < 3 * pb::kFs; i0 += 128) {
                        const double t0 = pb::now();
                        p.blockBegin(128);
                        for (int i = i0; i < i0 + 128; ++i) { while (cur < ev.size() && ev[cur].frame <= i) { piano_mvp_v4::dispatchMidi(p, ev[cur].d.data(), 3); ++cur; } p.blockStep(); }
                        p.blockEnd(l.data(), r.data());
                        if (i0 >= pb::kFs) t += pb::now() - t0;
                    }
                    best = std::min(best, t / (2.0 * pb::kFs) * 1e9);
                }
                return best;
            };
            const std::vector<int> bg{45, 52, 67, 74};
            const double liveOne = pianoCost(Piano::Quality::Live, true, {}), liveBg = pianoCost(Piano::Quality::Live, false, bg), liveBgKey = pianoCost(Piano::Quality::Live, true, bg);
            const double fullOne = pianoCost(Piano::Quality::Full, true, {}), fullBg = pianoCost(Piano::Quality::Full, false, bg), fullBgKey = pianoCost(Piano::Quality::Full, true, bg);
            std::printf("Live: one note %.0f ns/sample (%.0f cycles at %.1f GHz); marginal voice (5 notes - 4 notes) %.0f ns (%.0f cycles)\n", liveOne, liveOne * ghz, ghz, liveBgKey - liveBg, (liveBgKey - liveBg) * ghz);
            std::printf("Full: one note %.0f ns/sample (%.0f cycles); marginal voice %.0f ns (%.0f cycles)\n", fullOne, fullOne * ghz, fullBgKey - fullBg, (fullBgKey - fullBg) * ghz);
            std::printf("(Live cost includes the board coupling, sympathetic/leaky strings the note wakes and the baked radiation; marginal = with and without the key among four background notes)\n");
        } else if (cmd == "fitcost" && argc >= 3) {
            // fitted pipeline: voice only (rotors + low pass + drive), then everything (+ radiation modes and sum)
            pme::simd::ScopedFlushDenormals ftz;
            const int frames = 2 * pb::kFs;
            std::vector<std::string> fits(argv + 2, argv + argc);
            double bestVoice = 1e30, bestAll = 1e30, bestRotors = 1e30;
            for (int rep = 0; rep < 5; ++rep) {
                Chain c(fits);
                for (int i = 0; i < pb::kFs; ++i) { float a, b; c.step(a, b); }
                double t = pb::now(); float sink = 0;
                for (int i = 0; i < frames; ++i) { float a, b; c.step(a, b); sink += a; }
                bestAll = std::min(bestAll, (pb::now() - t) / frames * 1e9);
                Chain d(fits); for (int i = 0; i < pb::kFs; ++i) { for (auto& s : d.strings) sink += s.next(); }
                t = pb::now();
                for (int i = 0; i < frames; ++i) for (auto& s : d.strings) sink += s.next();
                bestRotors = std::min(bestRotors, (pb::now() - t) / frames * 1e9);
                if (sink == 12345.f) std::printf(" ");
            }
            std::size_t comps = 0; for (auto& s : Chain(fits).strings) comps += s.components();
            std::printf("fitted %zu string(s), %zu rotors: rotor bank %.1f ns/sample (%.0f cycles at 3.6 GHz); whole pipeline with baked radiation %.0f ns/sample (%.0f cycles)\n", fits.size(), comps, bestRotors, bestRotors * 3.6, bestAll, bestAll * 3.6);
            (void)bestVoice;
        } else { std::fprintf(stderr, "usage: see file header\n"); return 2; }
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
