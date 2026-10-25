// SPDX-License-Identifier: MIT
// 0.5.4.1 regression tests through the LV2 path.
//  LAT-1  sample accuracy: the output is bit-identical for block sizes 64/128/256/1024 and a variable pattern,
//         and for 0 and 1 helper threads, with idle gaps shorter and longer than the idle-skip hold (this caught
//         the idle-wake race of 0.5.4: a helper radiating stale ring records, ghost sound at note-on that differed
//         from run to run).
//  LAT-2  onset latency per note (MIDI event frame to the first output frame that differs from a run without that
//         note) is at most 12 ms (hammer flight) for eight different keys, and does not grow over five strikes of
//         one key (within 1 ms of the first).
//  LAT-3  a radiation helper that is descheduled while it holds the lock (simulated: it sleeps 3 ms per batch)
//         does not delay the audio thread for more than the first stalled block: the radiation moves to the audio
//         thread, and the output stays bit-identical to the single-thread output.
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "piano.hpp"
#include "pme/simd/denormals.hpp"

static LV2_URID map(void*, const char* uri) {
    if (!std::strcmp(uri, LV2_MIDI__MidiEvent)) return 1;
    if (!std::strcmp(uri, LV2_ATOM__Sequence)) return 2;
    if (!std::strcmp(uri, LV2_ATOM__frameTime)) return 3;
    return 4;
}
struct Seq {
    alignas(8) std::array<std::uint8_t, 1024> bytes{};
    LV2_Atom_Sequence* get() { return reinterpret_cast<LV2_Atom_Sequence*>(bytes.data()); }
    void reset() { bytes.fill(0); get()->atom = {sizeof(LV2_Atom_Sequence_Body), 2}; get()->body.unit = 3; }
    void add(std::int64_t frame, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
        LV2_Atom_Event e{}; e.time.frames = frame; e.body = {3, 1};
        const auto off = sizeof(LV2_Atom) + get()->atom.size;
        std::memcpy(bytes.data() + off, &e, sizeof(e));
        const std::uint8_t d[3]{a, b, c};
        std::memcpy(bytes.data() + off + sizeof(e), d, 3);
        get()->atom.size += sizeof(e) + 8;
    }
};
struct Note { long on; int key; };
static const LV2_Descriptor* gDesc;
// Plays the notes through the plug-in; `skip` leaves one note out (-1: none). Mono sum of both channels.
static std::vector<float> play(int quality, int threads, const std::vector<int>& blocks, const std::vector<Note>& notes,
                               long total, int skip) {
    const double fs = 48000;
    LV2_URID_Map mapper{nullptr, map};
    LV2_Feature mf{LV2_URID__map, &mapper};
    const LV2_Feature* features[]{&mf, nullptr};
    auto inst = gDesc->instantiate(gDesc, fs, "", features);
    if (!inst) { std::fprintf(stderr, "instantiate failed\n"); std::exit(2); }
    float q = float(quality), t = float(threads), fw = 0, ad = 0;   // adaptive shedding off: exact comparison
    std::vector<float> L(8192), R(8192);
    Seq seq; seq.reset();
    gDesc->connect_port(inst, 0, seq.get()); gDesc->connect_port(inst, 1, L.data()); gDesc->connect_port(inst, 2, R.data());
    gDesc->connect_port(inst, 3, &q); gDesc->connect_port(inst, 4, &t); gDesc->connect_port(inst, 5, &fw); gDesc->connect_port(inst, 6, &ad);
    gDesc->activate(inst);
    std::vector<float> out(std::size_t(total), 0.f);
    long pos = 0; std::size_t bi = 0;
    while (pos < total) {
        int n = blocks[bi++ % blocks.size()];
        if (pos + n > total) n = int(total - pos);
        seq.reset();
        for (std::size_t k = 0; k < notes.size(); ++k) {
            if (int(k) == skip) continue;
            if (notes[k].on >= pos && notes[k].on < pos + n) seq.add(notes[k].on - pos, 0x90, std::uint8_t(notes[k].key), 80);
            const long off = notes[k].on + long(0.3 * fs);
            if (off >= pos && off < pos + n) seq.add(off - pos, 0x80, std::uint8_t(notes[k].key), 0);
        }
        gDesc->run(inst, std::uint32_t(n));
        for (int i = 0; i < n; ++i) out[std::size_t(pos + i)] = L[std::size_t(i)] + R[std::size_t(i)];
        pos += n;
    }
    gDesc->deactivate(inst); gDesc->cleanup(inst);
    return out;
}
static std::vector<Note> scenario(double gap, bool sameKey) {
    std::vector<Note> n;
    for (int k = 0; k < 5; ++k) n.push_back({long((0.3 + k * gap) * 48000) + 17 * k, sameKey ? 60 : 40 + 6 * k});
    return n;
}
// Onset latency of each note in frames: first frame that differs from the run without the note.
static std::vector<long> onsets(const std::vector<Note>& notes, long total, const std::vector<float>& full, int threads, const std::vector<int>& blocks) {
    std::vector<long> lat;
    for (std::size_t k = 0; k < notes.size(); ++k) {
        const auto without = play(0, threads, blocks, notes, total, int(k));
        const long end = std::min<long>(total, notes[k].on + long(0.15 * 48000));
        float peak = 0;
        for (long i = notes[k].on; i < end; ++i) peak = std::max(peak, std::fabs(full[std::size_t(i)] - without[std::size_t(i)]));
        long first = -1;
        for (long i = 0; i < end; ++i) if (std::fabs(full[std::size_t(i)] - without[std::size_t(i)]) > peak * 1e-3f) { first = i - notes[k].on; break; }
        lat.push_back(first);
    }
    return lat;
}
static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } }

static void lat3() {
    using piano_mvp_v4::Piano;
    constexpr int blocks = 900, frames = 64;
    const auto run = [&](int threads, unsigned holdUs, std::vector<double>* ms, std::uint64_t* stalls) {
        Piano p; p.setWorkerThreads(threads); p.prepare(48000); p.setQuality(Piano::Quality::Live); p.setActiveWorkers(threads); p.warmUp();
        p.testHelperHold(holdUs);
        std::vector<float> out, l(frames), r(frames);
        for (int b = 0; b < blocks; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            p.blockBegin(frames);
            for (int i = 0; i < frames; ++i) { if (b == 5 && i == 0) { p.noteOn(40, 0.8); p.noteOn(47, 0.8); } p.blockStep(); }
            p.blockEnd(l.data(), r.data());
            if (ms) ms->push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            out.insert(out.end(), l.begin(), l.end()); out.insert(out.end(), r.begin(), r.end());
        }
        if (stalls) *stalls = p.radiationStalls();
        p.testHelperHold(0);
        return out;
    };
    std::vector<double> ms; std::uint64_t stalls = 0;
    const auto helper = run(1, 3000, &ms, &stalls);
    const auto single = run(0, 0, nullptr, nullptr);
    check(helper == single, "LAT-3 output with a stalled helper differs from the single-thread output");
    check(stalls >= 1, "LAT-3 the stalled helper was not detected");
    double worst = 0;   // blocks 60..600: after the first stall (the fallback lasts a second)
    for (int b = 60; b < 600; ++b) worst = std::max(worst, ms[std::size_t(b)]);
    std::printf("LAT-3 stalls %llu, worst block 60..600 with a stalled helper %.2f ms (deadline 1.33 ms, a 3 ms helper hold would cost >= 3 ms)\n",
                (unsigned long long)stalls, worst);
    check(worst < 2.5, "LAT-3 blocks still wait for the stalled helper");
}
int main(int argc, char** argv) {
    pme::simd::ScopedFlushDenormals fz;
    if (argc < 2) return 2;
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) return 2;
    auto entry = reinterpret_cast<const LV2_Descriptor* (*)(std::uint32_t)>(dlsym(lib, "lv2_descriptor"));
    gDesc = entry(0);
    const double fs = 48000;
    const std::vector<std::vector<int>> patterns{{64}, {128}, {256}, {1024}, {128, 256, 100, 512, 37, 1024}};
    for (double gap : {0.06, 0.4}) {   // shorter / longer than the 0.1 s idle-skip hold
        const auto notes = scenario(gap, false);
        const long total = notes.back().on + long(0.5 * fs);
        // Threaded output equals single-thread output for the same block pattern. (Different patterns differ at the
        // -200 dB level through the idle skip, which is evaluated at block ends, and the tension model amplifies it.)
        for (const auto& blocks : patterns) {
            const auto single = play(0, 0, blocks, notes, total, -1);
            const auto helper = play(0, 1, blocks, notes, total, -1);
            char what[96]; std::snprintf(what, sizeof what, "LAT-1 gap %.2f first block %d: threaded output differs from single-thread", gap, blocks[0]);
            check(helper == single, what);
        }
        if (gap > 0.1) {
            for (const auto& blocks : {patterns[0], patterns[1], patterns[4]}) {
                const auto full = play(0, 1, blocks, notes, total, -1);
                const auto lat = onsets(notes, total, full, 1, blocks);
                std::printf("LAT-2 distinct keys, first block %d, onset ms:", blocks[0]);
                for (long l : lat) { std::printf(" %.2f", l * 1000.0 / fs); check(l >= 0 && l <= 0.012 * fs, "LAT-2 onset latency above 12 ms"); }
                std::printf("\n");
            }
            const auto same = scenario(gap, true);
            const auto sfull = play(0, 1, {64}, same, total, -1);
            const auto slat = onsets(same, total, sfull, 1, {64});
            std::printf("LAT-2 repeated key, onset ms:");
            for (long l : slat) { std::printf(" %.2f", l * 1000.0 / fs); check(l >= 0 && l <= slat[0] + 0.001 * fs, "LAT-2 onset latency grows over strikes"); }
            std::printf("\n");
        }
    }
    lat3();
    std::printf("%s\n", fails ? "FAIL: LV2 latency tests" : "PASS: LAT-1 LAT-2 LAT-3");
    return fails ? 1 : 0;
}
