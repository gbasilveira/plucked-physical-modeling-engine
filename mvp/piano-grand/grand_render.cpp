// SPDX-License-Identifier: MIT
// Piano 0.7a offline render path. Runs the unchanged 0.6 Full strings and hammer (Piano class, quality Full) coupled to the
// FEM grand board of mvp/piano-grand (installed through the BoardOverride hook: loaded poles and participations), and writes
// the per-sample bridge force records g_k(t) of the 88 string points (the same quantity the 0.6 radiation stage consumes).
// The radiation to the microphones is the convolution of g_k with the port-space impulse responses of assemble_ir.py,
// done by render_grand.py. The 0.6 radiation of the old board is zeroed under the override and its output is not used.
//
// usage: piano-grand-render events board_table.bin out_prefix [--full] [--seconds-tail S]
//   board_table.bin: 512 omega, 512 sigma, 88*512 phi (little-endian doubles)
//   out_prefix.g.f32: frames x 88 float32 (force records); out_prefix.info.txt
#include "piano.hpp"
#include "midi.hpp"
#include "board_override.hpp"
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
        bool full = false; double tail = 4.0;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--full") full = true; else if (a == "--tail" && i + 1 < argc) tail = std::stod(argv[++i]); else throw std::runtime_error("unknown option " + a);
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
        }
        std::ofstream(std::string(argv[3]) + ".info.txt") << "frames=" << finish << "\nfs=" << fs << "\npeak_g=" << peakG << "\nfaults=" << piano.numericalFaults() << "\n";
        if (piano.numericalFaults()) throw std::runtime_error("numerical faults");
        std::cout << finish / double(fs) << " s, " << finish << " frames, peak bridge drive " << peakG << " N, faults " << piano.numericalFaults() << "\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
