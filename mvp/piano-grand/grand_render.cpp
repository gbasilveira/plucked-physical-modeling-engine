// SPDX-License-Identifier: MIT
// Piano 0.7a offline render path. Runs the unchanged 0.6 Full strings and hammer (Piano class, quality Full) coupled to the
// FEM grand board of mvp/piano-grand (installed through the BoardOverride hook: loaded poles and participations), and writes
// the per-sample bridge force records g_k(t) of the 88 string points (the same quantity the 0.6 radiation stage consumes).
// The radiation to the microphones is the convolution of g_k with the port-space impulse responses of assemble_ir.py,
// done by render_grand.py. The 0.6 radiation of the old board is zeroed under the override and its output is not used.
//
// 0.7b: --dump-strings FILE writes the compiled string data of every key (count, speaking length, outer diameter, mode count, omega) for
// string_radiation.py; --strrad TABLE writes out_prefix.s.f32 (frames x nmics float32): the pressure at the microphones radiated directly by
// the vibrating strings, from the modal states of the struck strings and the complex gain table of string_radiation.py (G per unit modal
// velocity at omega_n: p = Re[G (u/dt + j omega (q - u/2))], the analytic signal of a decaying partial, both polarisations).
//
// Diagnostics (0.7b): --split also writes out_prefix.gu.f32 (the bridge force records of the unstruck voices only; the struck part is g - gu) and, with
// --strrad, out_prefix.sl.f32 (direct radiation of the unstruck strings, vertical modes below 2 kHz, the light model's states); --solo KEY writes
// out_prefix.solo.f32 (frames x (nmics + 1) float32): direct radiation of that key's strings (struck or unstruck) and a pickup, the strings' summed
// velocity 20 mm from the bridge (m/s, vertical + horizontal when struck).
// usage: piano-grand-render events board_table.bin out_prefix [--full] [--seconds-tail S] [--strrad strrad.bin]
//   board_table.bin: 512 omega, 512 sigma, 88*512 phi (little-endian doubles)
//   out_prefix.g.f32: frames x 88 float32 (force records); out_prefix.info.txt
#include "piano.hpp"
#include "midi.hpp"
#include "board_override.hpp"
#include "preset.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("usage: piano-grand-render events board_table.bin out_prefix [--full] [--tail seconds]");
        bool full = false; double tail = 4.0; std::string dumpPath, dumpTerms, strradPath; bool split = false; int solo = -1;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--full") full = true; else if (a == "--tail" && i + 1 < argc) tail = std::stod(argv[++i]);
            else if (a == "--split") split = true; else if (a == "--solo" && i + 1 < argc) solo = std::stoi(argv[++i]);
            else if (a == "--dump-strings" && i + 1 < argc) dumpPath = argv[++i]; else if (a == "--dump-terms" && i + 1 < argc) dumpTerms = argv[++i]; else if (a == "--strrad" && i + 1 < argc) strradPath = argv[++i];
            else throw std::runtime_error("unknown option " + a);
        }
        constexpr int fs = 48000;
        struct Event { int frame; std::array<std::uint8_t, 3> data; };
        std::vector<Event> events; {
            std::ifstream in(argv[1]); if (!in) throw std::runtime_error("cannot read events");
            int frame, status, a, b, previous = -1;
            while (in >> frame) {
                if (!(in >> status >> a >> b)) throw std::runtime_error("truncated event");
                if (frame < previous || frame < 0 || a < 0 || a > 127 || b < 0 || b > 127 || (status != 128 && status != 144 && !(status == 176 && (a == 64 || a == 67))) || (status != 176 && (a < 21 || a > 108)))
                    throw std::runtime_error("invalid event schedule");
                previous = frame; events.push_back({frame, {std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)}});
            }
            if (events.empty()) throw std::runtime_error("empty schedule");
        }
        piano_mvp_v4::BoardOverride ov;
        {
            std::ifstream in(argv[2], std::ios::binary); if (!in) throw std::runtime_error("cannot read board table");
            ov.omega.resize(512); ov.sigma.resize(512); ov.phi.resize(88 * 512);
            in.read(reinterpret_cast<char*>(ov.omega.data()), 512 * 8); in.read(reinterpret_cast<char*>(ov.sigma.data()), 512 * 8);
            in.read(reinterpret_cast<char*>(ov.phi.data()), 88 * 512 * 8);
            if (!in) throw std::runtime_error("short board table");
        }
        piano_mvp_v4::setBoardOverride(&ov);
        // identical configuration to piano-midi-render "current" (the 0.6 Full default)
        piano_mvp_v4::Piano piano;
        piano.setResidualBridgeLoss(true);
        piano.setSoftFelt(true); piano.setBridgeMassLoading(true); piano.setSaturatingStringLoss(true); piano.setUnaCorda(true);
        piano.setUndampedTreble(true); piano.setSharedBoard(true); piano.setGrandBridgeGeometry(true); piano.setTensionModulation(true);
        piano.setLeakyDampers(true); piano.setHeldNoteCount(true); piano.setImperfection(1.0);
        piano.setVoiceLimit(piano_mvp_v4::Piano::maxPolyphony);
        piano.setRadiation(piano_mvp_v4::Piano::Radiation::Modal);
        if (!piano.setWorkerThreads(0)) throw std::runtime_error("threads");
        if (!piano.prepare(fs)) throw std::runtime_error("prepare failed");
        if (!dumpTerms.empty()) {   // 0.7c: bridge-force coefficients a_n of every string (count, then per string nm and a[nm] float32)
            std::ofstream d(dumpTerms, std::ios::binary);
            for (int key = 21; key <= 108; ++key) {
                const std::int32_t cnt = piano.keyStringCount(key); d.write(reinterpret_cast<const char*>(&cnt), 4);
                for (int j = 0; j < cnt; ++j) {
                    const auto& t = piano.compiledTerms(key, j); const std::int32_t nm = static_cast<std::int32_t>(t.a.size());
                    d.write(reinterpret_cast<const char*>(&nm), 4); d.write(reinterpret_cast<const char*>(t.a.data()), 4 * nm);
                }
            }
            return 0;
        }
        if (!dumpPath.empty()) {
            std::ofstream d(dumpPath, std::ios::binary);
            for (int key = 21; key <= 108; ++key) {
                const auto preset = piano_mvp_v2::parametersForKey(key);
                const std::int32_t cnt = piano.keyStringCount(key);
                d.write(reinterpret_cast<const char*>(&cnt), 4);
                for (int j = 0; j < cnt; ++j) {
                    const auto& m = piano.compiledString(key, j);
                    const double hdr[4] = {m.L, preset.string.dOuter, m.rhoA, m.tension};
                    const std::int32_t nm = static_cast<std::int32_t>(m.omega.size());
                    d.write(reinterpret_cast<const char*>(hdr), 32); d.write(reinterpret_cast<const char*>(&nm), 4);
                    d.write(reinterpret_cast<const char*>(m.omega.data()), 8 * nm);
                }
            }
            return 0;
        }
        // direct string radiation table: per key [string][polarisation][mode][mic][re, im] float32
        struct KeyTab { std::int32_t count{}; std::vector<std::int32_t> nm; std::vector<float> g; std::vector<std::size_t> off; };
        std::vector<KeyTab> tabs; int nmic = 0;
        if (!strradPath.empty()) {
            std::ifstream in(strradPath, std::ios::binary); if (!in) throw std::runtime_error("cannot read strrad table");
            char magic[4]; std::int32_t nmi; in.read(magic, 4); in.read(reinterpret_cast<char*>(&nmi), 4); nmic = nmi;
            if (std::string(magic, 4) != "SRAD") throw std::runtime_error("bad strrad magic");
            tabs.resize(88);
            for (int k = 0; k < 88; ++k) {
                auto& t = tabs[k]; in.read(reinterpret_cast<char*>(&t.count), 4); t.nm.resize(t.count); t.off.resize(t.count);
                std::size_t total = 0;
                for (int j = 0; j < t.count; ++j) { in.read(reinterpret_cast<char*>(&t.nm[j]), 4); t.off[j] = total; total += std::size_t(2) * t.nm[j] * nmic * 2; }
                t.g.resize(total); in.read(reinterpret_cast<char*>(t.g.data()), total * 4);
                if (!in) throw std::runtime_error("short strrad table");
            }
        }
        std::ofstream gu, sl, sosf; if (split) gu.open(std::string(argv[3]) + ".gu.f32", std::ios::binary);
        if (nmic && split) sl.open(std::string(argv[3]) + ".sl.f32", std::ios::binary);
        if (solo >= 21) sosf.open(std::string(argv[3]) + ".solo.f32", std::ios::binary);
        std::vector<float> recu(88), soloFrame(8, 0.f); std::vector<double> lacc(8, 0.0), soacc(8, 0.0);
        std::ofstream sout; std::vector<double> sacc(std::max(nmic, 1), 0.0); std::vector<float> sframe(std::max(nmic, 1), 0.f);
        if (nmic) sout.open(std::string(argv[3]) + ".s.f32", std::ios::binary);
        const double invDt = fs; std::vector<double> compiledLen(88, 1.0);
        for (int k = 0; k < 88; ++k) compiledLen[k] = piano.compiledString(k + 21, 0).L;
        const int end = events.back().frame + 1;
        const int finish = full ? end + int(tail * fs) : events.back().frame + int(tail * fs);
        std::ofstream out(std::string(argv[3]) + ".g.f32", std::ios::binary); if (!out) throw std::runtime_error("cannot write");
        pme::simd::ScopedFlushDenormals denormals;
        std::size_t cursor = 0; std::vector<float> rec(88); double peakG = 0;
        float l, r;
        for (int i = 0; i < finish; ++i) {
            if (i < end) while (cursor < events.size() && events[cursor].frame == i) { piano_mvp_v4::dispatchMidi(piano, events[cursor].data.data(), 3); ++cursor; }
            if (i == end) { piano.sustain(false); piano.allNotesOff(); }
            piano.sample(l, r);
            std::fill(rec.begin(), rec.end(), 0.f);
            for (int k = 0; k < piano.radiationPortCount(); ++k) {
                const double g = piano.radiationDrive(k); rec[piano.radiationPort(k)] = float(g); peakG = std::max(peakG, std::fabs(g));
            }
            out.write(reinterpret_cast<const char*>(rec.data()), 88 * 4);
            if (split) {
                std::fill(recu.begin(), recu.end(), 0.f);
                for (int k = 0; k < piano.radiationPortCount(); ++k) { const int p = piano.radiationPort(k); if (!piano.voiceStruck(p + 21)) recu[p] = rec[p]; }
                gu.write(reinterpret_cast<const char*>(recu.data()), 88 * 4);
            }
            if (nmic) {
                std::fill(lacc.begin(), lacc.end(), 0.0); std::fill(soacc.begin(), soacc.end(), 0.0);
                if (split || solo >= 21)
                    piano.visitLightStrings([&](int k, int j, std::size_t nAct, const double* om, const double* q, const double* u) {
                        const auto& t = tabs[k]; if (j >= t.count) return;
                        const std::size_t n = std::min<std::size_t>(nAct, t.nm[j]); const float* gV = t.g.data() + t.off[j];
                        const double lp = (solo == k + 21) ? 1.0 : 0.0;
                        for (std::size_t i = 0; i < n; ++i) {
                            const double vV = u[i] * invDt, dV = om[i] * (q[i] - 0.5 * u[i]); const float* a = gV + i * nmic * 2;
                            for (int m = 0; m < nmic; ++m) { const double c = a[2 * m] * vV - a[2 * m + 1] * dV; lacc[m] += c; if (lp) soacc[m] += c; }
                            if (lp) soacc[nmic] += vV * std::sqrt(2.0 / compiledLen[k]) * std::sin((i + 1) * 3.14159265358979323846 * (compiledLen[k] - 0.02) / compiledLen[k]);
                        }
                    });
                if (split) { for (int m = 0; m < nmic; ++m) sframe[m] = float(lacc[m]); sl.write(reinterpret_cast<const char*>(sframe.data()), nmic * 4); }
                std::fill(sacc.begin(), sacc.end(), 0.0);
                piano.visitStrings([&](int k, int j, std::size_t nAct, const double* om, const double* qV, const double* uV, const double* qH, const double* uH) {
                    const auto& t = tabs[k]; if (j >= t.count) return;
                    const std::size_t n = std::min<std::size_t>(nAct, t.nm[j]);
                    const float* gV = t.g.data() + t.off[j]; const float* gH = gV + std::size_t(t.nm[j]) * nmic * 2;
                    for (std::size_t i = 0; i < n; ++i) {
                        const double vV = uV[i] * invDt, dV = om[i] * (qV[i] - 0.5 * uV[i]);
                        const double vH = uH[i] * invDt, dH = om[i] * (qH[i] - 0.5 * uH[i]);
                        const float* a = gV + i * nmic * 2; const float* b = gH + i * nmic * 2;
                        for (int m = 0; m < nmic; ++m) { const double c = a[2 * m] * vV - a[2 * m + 1] * dV + b[2 * m] * vH - b[2 * m + 1] * dH; sacc[m] += c; if (k + 21 == solo) soacc[m] += c; }
                        if (k + 21 == solo) soacc[nmic] += (vV + vH) * std::sqrt(2.0 / compiledLen[k]) * std::sin((i + 1) * 3.14159265358979323846 * (compiledLen[k] - 0.02) / compiledLen[k]);
                    }
                });
                for (int m = 0; m < nmic; ++m) sframe[m] = float(sacc[m]);
                sout.write(reinterpret_cast<const char*>(sframe.data()), nmic * 4);
                if (solo >= 21) { for (int m = 0; m <= nmic; ++m) soloFrame[m] = float(soacc[m]); sosf.write(reinterpret_cast<const char*>(soloFrame.data()), (nmic + 1) * 4); }
            }
        }
        std::ofstream(std::string(argv[3]) + ".info.txt") << "frames=" << finish << "\nfs=" << fs << "\npeak_g=" << peakG << "\nfaults=" << piano.numericalFaults() << "\n";
        if (piano.numericalFaults()) throw std::runtime_error("numerical faults");
        std::cout << finish / double(fs) << " s, " << finish << " frames, peak bridge drive " << peakG << " N, faults " << piano.numericalFaults() << "\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
