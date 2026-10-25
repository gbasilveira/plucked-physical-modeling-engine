// SPDX-License-Identifier: MIT
// Offline rendering of a validated, sample-timed fixture. No MIDI parser in audio code.
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void word(std::ostream& f, std::uint32_t x, int bytes) {
    for (int i=0;i<bytes;++i) f.put(static_cast<char>((x>>(8*i))&255));
}
struct Event { int frame; std::array<std::uint8_t,3> data; };
int main(int argc, char** argv) {
    try {
        // --full (any position after the output): render the whole schedule from frame 0 to the
        // last event plus a 4 s release tail. Default behaviour (40-64 s excerpt) is unchanged.
        bool full=false;
        for (int i=1;i<argc;) {
            if (std::string(argv[i])=="--full") { full=true; for (int j=i;j<argc-1;++j) argv[j]=argv[j+1]; --argc; }
            else ++i;
        }
        if (argc != 3 && !(argc==4 && (std::string(argv[3])=="--legacy" || std::string(argv[3])=="--previous" || std::string(argv[3])=="--v051" || std::string(argv[3])=="--v050" || std::string(argv[3])=="--shared-pan" || std::string(argv[3])=="--v049" || std::string(argv[3])=="--v048" || std::string(argv[3])=="--v046")))
            throw std::runtime_error("usage: piano-midi-render validated.events output.wav [--legacy|--v046|--v048|--v049|--v050|--v051|--previous(0.5.2, no leaky dampers)|--shared-pan(0.5.1 coupling, 0.5.0 bridge positions)]");
        constexpr int fs=48000;
        int start=40*fs, end=64*fs, finish=66*fs;
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot read events");
        std::vector<Event> events;
        int frame, status, a, b, previous=-1;
        while (input >> frame) {
            if (!(input >> status >> a >> b)) throw std::runtime_error("truncated event");
            if (frame<previous || frame<0 || a<0 || a>127 || b<0 || b>127 ||
                (status!=128 && status!=144 && !(status==176 && (a==64 || a==67))) ||
                (status!=176 && (a<21 || a>108)))
                throw std::runtime_error("invalid event schedule");
            previous=frame;
            events.push_back({frame,{static_cast<std::uint8_t>(status),
                                    static_cast<std::uint8_t>(a),static_cast<std::uint8_t>(b)}});
        }
        if (!input.eof() || events.empty()) throw std::runtime_error("malformed/empty schedule");
        if (full) { start=0; end=events.back().frame+1; finish=end+4*fs; }
        piano_mvp_v4::Piano piano;
        piano.setResidualBridgeLoss(argc!=4 || std::string(argv[3])!="--legacy");
        const std::string mode = argc==4 ? argv[3] : "";
        // current: 0.5.3 default, --previous (0.5.2: no leaky dampers), --v051 (no tension either) or
        // --shared-pan; v050: --v050 (0.5.0, private bridges).
        const bool current = mode.empty() || mode=="--previous" || mode=="--v051" || mode=="--shared-pan", v050 = mode=="--v050";
        piano.setSoftFelt(current || v050 || mode=="--v049" || mode=="--v048");
        // 0.4.9 physics is kept by --v049 and later; 0.5.0 adds polyphony, una corda, undamped treble.
        piano.setBridgeMassLoading(current || v050 || mode=="--v049");
        piano.setSaturatingStringLoss(current || v050 || mode=="--v049");
        piano.setUnaCorda(current || v050);
        piano.setUndampedTreble(current || v050);
        // 0.5.1 shared board and key-dependent bridge points (each off in --previous).
        piano.setSharedBoard(current);
        piano.setGrandBridgeGeometry(mode.empty() || mode=="--previous" || mode=="--v051");
        // 0.5.2 tension modulation of the struck strings (off in every earlier version).
        piano.setTensionModulation(mode.empty() || mode=="--previous");
        // 0.5.3 leaky dampers (off in every earlier version).
        piano.setLeakyDampers(mode.empty());
        // Offline: no voice stealing by default; earlier versions keep their 12-voice limit.
        piano.setVoiceLimit(current || v050 ? piano_mvp_v4::Piano::maxPolyphony : piano_mvp_v4::Piano::maxVoices);
        if (!piano.prepare(fs)) throw std::runtime_error("prepare failed");
        std::ofstream out(argv[2],std::ios::binary);
        if (!out) throw std::runtime_error("cannot write WAV");
        out.write("RIFF",4);word(out,36+(finish-start)*4,4);out.write("WAVEfmt ",8);
        word(out,16,4);word(out,1,2);word(out,2,2);word(out,fs,4);
        word(out,fs*4,4);word(out,4,2);word(out,16,2);
        out.write("data",4);word(out,(finish-start)*4,4);
        pme::simd::ScopedFlushDenormals denormals;
        std::size_t cursor=0;
        double peak=0, energy=0;
        std::uint64_t stealsAtStart=0;
        const auto begin=std::chrono::steady_clock::now();
        for (int i=0;i<finish;++i) {
            if (i==start) stealsAtStart=piano.voiceSteals();
            if (i<end) {
                while (cursor<events.size() && events[cursor].frame==i) {
                    piano_mvp_v4::dispatchMidi(piano,events[cursor].data.data(),3);++cursor;
                }
            }
            if (i==end) {
                piano.sustain(false);
                for (int k=21;k<=108;++k) piano.noteOff(k);
            }
            float l,r;piano.sample(l,r);
            if (i<start) continue;
            // Only the end boundary fades, after 1.95 seconds of natural release.
            const double fade=std::min(1.0,(finish-1-i)/(0.05*fs));
            for (double x : {l*fade,r*fade}) {
                if (!std::isfinite(x) || std::abs(x)>1)
                    throw std::runtime_error("invalid/clipping audio");
                peak=std::max(peak,std::abs(x));energy+=x*x;
                word(out,static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(x*32767))),2);
            }
        }
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        if (!out || piano.numericalFaults() || peak<1e-5) throw std::runtime_error("failed/silent render");
        std::cout << (finish-start)/double(fs) << " s stereo 48 kHz" << (full ? "; complete source + 4 s release" : "; source 40–64 s + 2 s release") << "; peak " << peak
                  << "; RMS " << std::sqrt(energy/(2*(finish-start)))
                  << "; numerical faults " << piano.numericalFaults()
                  << (full ? "; voice steals " : "; excerpt voice steals ") << piano.voiceSteals()-stealsAtStart
                  << "; render time " << elapsed << " s; realtime factor " << (finish-start)/double(fs)/elapsed << "\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
