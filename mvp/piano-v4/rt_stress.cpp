// SPDX-License-Identifier: MIT
// 0.5.4 deadline harness: drives the CLAP plug-in's process() at a fixed block size and times every
// block against its deadline (block / fs). Scenarios are worst cases of the real-time plug-in.
//   piano-rt-stress PLUGIN.clap [--scenario S] [--block N] [--quality live|balanced|full] [--threads T]
//                   [--seconds D] [--events FILE] [--rt] [--json] [--expect-equal]
// Scenarios: cluster (sustain pedal, 12 keys ff), cluster-nopedal, chords (fast repeated chords,
// pedal), repeat (same chord re-struck every 60 ms), chopin (needs --events; the 40 s .. 64 s excerpt),
// idle (no notes). --expect-equal renders the scenario twice, with the given settings and with
// 0 helper threads, and requires identical output (hash). Without --rt the harness runs as a normal
// process; with --rt it requests SCHED_FIFO for the audio thread (as a real-time host would).
#include <clap/clap.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

constexpr std::uint32_t kWarmupBlocks = 16;
struct Ev { std::uint32_t frame; std::uint8_t d[3]; };
struct Result { double rtf{}, maxRatio{}, p999{}, p99{}; std::uint64_t worstBlock{}; std::uint64_t misses{}, blocks{}, hash{}; double peak{}; };
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

struct Host {
    const clap_plugin_t* plugin{};
    void* lib{};
    const clap_plugin_entry_t* entry{};
};
static std::vector<Ev> scenario(const std::string& name, const std::string& eventsFile, std::uint32_t& startFrame, std::uint32_t fs) {
    std::vector<Ev> e;
    startFrame = 0;
    if (name == "cluster" || name == "cluster-nopedal") {
        if (name == "cluster") e.push_back({0, {0xb0, 64, 127}});
        for (int k = 48; k < 60; ++k) e.push_back({1, {0x90, std::uint8_t(k), 120}});
    } else if (name == "chords" || name == "repeat") {
        e.push_back({0, {0xb0, 64, 127}});
        const std::uint32_t period = name == "chords" ? 3600 : fs * 60 / 1000;
        for (std::uint32_t r = 0; r < 4000; ++r)
            for (int k : {43, 50, 55, 59, 62, 67, 71, 74, 79, 83, 86, 91}) {
                if (name == "repeat" && r % 2 && k < 60) continue;
                e.push_back({1 + r * period + (std::uint32_t(k) % 5) * 40, {0x90, std::uint8_t(k + (r % 3) * 2), 112}});
            }
    } else if (name == "chopin") {
        std::ifstream in(eventsFile);
        require(bool(in), "cannot read --events");
        std::uint32_t frame; int st, a, b;
        // The 24 s excerpt (40 s .. 64 s of the performance): notes struck earlier are not replayed; pedal
        // controllers before it are applied at frame 0.
        const std::uint32_t from = 40 * fs;
        while (in >> frame >> st >> a >> b) {
            if (frame < from && st != 176) continue;
            e.push_back({frame < from ? 0 : frame - from, {std::uint8_t(st), std::uint8_t(a), std::uint8_t(b)}});
        }
    } else require(name == "idle", "unknown scenario");
    std::stable_sort(e.begin(), e.end(), [](const Ev& x, const Ev& y) { return x.frame < y.frame; });
    return e;
}
struct Events {
    std::vector<clap_event_midi_t> midi;
    std::vector<clap_event_param_value_t> params;
    std::vector<const clap_event_header_t*> all;
};
static Result run(const char* path, const std::string& name, const std::string& eventsFile, std::uint32_t block,
                  int quality, int threads, double seconds, bool rt) {
    constexpr std::uint32_t fs = 48000;
    Host h;
    h.lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    require(h.lib, "dlopen failed");
    h.entry = static_cast<const clap_plugin_entry_t*>(dlsym(h.lib, "clap_entry"));
    require(h.entry && h.entry->init(path), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(h.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    clap_host_t host{CLAP_VERSION, nullptr, "rt-stress", "PME", "", "0.1",
        [](const clap_host_t*, const char*) -> const void* { return nullptr; },
        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
    h.plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
    require(h.plugin && h.plugin->init(h.plugin), "instance failed");
    require(h.plugin->activate(h.plugin, fs, 1, 4096) && h.plugin->start_processing(h.plugin), "activate failed");
    if (rt) {
        sched_param sp{}; sp.sched_priority = 50;
        (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);   // ignored if not permitted
    }
    std::uint32_t start = 0;
    const auto script = scenario(name, eventsFile, start, fs);
    const std::uint32_t total = static_cast<std::uint32_t>(seconds * fs);
    std::vector<float> left(block), right(block);
    float* channels[] = {left.data(), right.data()};
    clap_audio_buffer_t audio{}; audio.data32 = channels; audio.channel_count = 2;
    struct Ctx { std::vector<const clap_event_header_t*>* list; } ctx;
    std::vector<const clap_event_header_t*> list;
    ctx.list = &list;
    clap_input_events_t in{&ctx,
        [](const clap_input_events_t* l) -> std::uint32_t { return static_cast<Ctx*>(l->ctx)->list->size(); },
        [](const clap_input_events_t* l, std::uint32_t i) -> const clap_event_header_t* { return (*static_cast<Ctx*>(l->ctx)->list)[i]; }};
    clap_process_t proc{}; proc.frames_count = block; proc.audio_outputs = &audio; proc.audio_outputs_count = 1; proc.in_events = &in;
    std::vector<clap_event_midi_t> midi(4096);
    clap_event_param_value_t pq{}, pt{};
    const auto param = [](clap_event_param_value_t& p, clap_id id, double v) {
        p.header = {sizeof(p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        p.param_id = id; p.value = v; p.note_id = -1; p.port_index = -1; p.channel = -1; p.key = -1;
    };
    param(pq, 0, quality); param(pt, 1, threads);
    std::size_t cursor = 0;
    // Events before `start` are applied in the first block (pedal) at their time.
    std::vector<double> ratios;
    ratios.reserve(total / block + 1);
    std::uint64_t hash = 1469598103934665603ull, misses = 0;
    double peak = 0, worst = 0; std::uint64_t worstBlock = 0;
    const double deadline = double(block) / fs;
    const auto wall0 = std::chrono::steady_clock::now();
    double busy = 0;
    for (std::uint32_t base = 0; base < total; base += block) {
        list.clear();
        std::size_t used = 0;
        if (base == 0) { list.push_back(&pq.header); list.push_back(&pt.header); }
        const std::uint32_t abs0 = start + base;
        while (cursor < script.size() && script[cursor].frame < abs0 + block && used < midi.size()) {
            const std::uint32_t f = script[cursor].frame < abs0 ? 0 : script[cursor].frame - abs0;
            auto& m = midi[used++];
            m.header = {sizeof(m), f, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
            m.port_index = 0; std::memcpy(m.data, script[cursor].d, 3);
            list.push_back(&m.header); ++cursor;
        }
        const auto t0 = std::chrono::steady_clock::now();
        require(h.plugin->process(h.plugin, &proc) != CLAP_PROCESS_ERROR, "process failed");
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        busy += dt;
        // The first blocks after activation (cold helper thread, first-touch pages, dynamic-linker
        // resolution of the SIMD kernels) are warm-up: timed in rtf but not counted as misses.
        if (base / block >= kWarmupBlocks) { if (dt / deadline > worst) { worst = dt / deadline; worstBlock = base / block; } ratios.push_back(dt / deadline); if (dt > deadline) ++misses; }
        for (std::uint32_t i = 0; i < block; ++i) {
            std::uint32_t b[2]; std::memcpy(&b[0], &left[i], 4); std::memcpy(&b[1], &right[i], 4);
            for (auto w : b) for (int k = 0; k < 4; ++k) { hash ^= (w >> (8 * k)) & 255; hash *= 1099511628211ull; }
            peak = std::max<double>(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        }
    }
    (void)wall0;
    h.plugin->stop_processing(h.plugin);
    h.plugin->deactivate(h.plugin);   // joins the helper threads
    h.plugin->destroy(h.plugin);
    h.entry->deinit();
    dlclose(h.lib);
    Result r;
    std::sort(ratios.begin(), ratios.end());
    const auto q = [&](double p) { return ratios.empty() ? 0.0 : ratios[std::min(ratios.size() - 1, std::size_t(p * ratios.size()))]; };
    r.rtf = (double(total) / fs) / busy; r.maxRatio = ratios.empty() ? 0 : ratios.back();
    r.p99 = q(0.99); r.p999 = q(0.999); r.misses = misses; r.blocks = ratios.size(); r.hash = hash; r.peak = peak; r.worstBlock = worstBlock;
    return r;
}
int main(int argc, char** argv) {
    try {
        require(argc >= 2, "plug-in path required");
        std::string name = "cluster", events, qname = "live";
        std::uint32_t block = 128; int threads = 1; double seconds = 5; bool rt = false, json = false, equal = false; double minRtf = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--scenario") name = argv[++i];
            else if (a == "--block") block = std::stoul(argv[++i]);
            else if (a == "--quality") qname = argv[++i];
            else if (a == "--threads") threads = std::stoi(argv[++i]);
            else if (a == "--seconds") seconds = std::stod(argv[++i]);
            else if (a == "--events") events = argv[++i];
            else if (a == "--rt") rt = true;
            else if (a == "--json") json = true;
            else if (a == "--expect-equal") equal = true;
            else if (a == "--min-rtf") minRtf = std::stod(argv[++i]);
        }
        const int quality = qname == "live" ? 0 : qname == "balanced" ? 1 : 2;
        const Result r = run(argv[1], name, events, block, quality, threads, seconds, rt);
        if (json)
            std::cout << "{\"scenario\":\"" << name << "\",\"block\":" << block << ",\"quality\":\"" << qname << "\",\"threads\":" << threads
                      << ",\"realtime_factor\":" << r.rtf << ",\"misses\":" << r.misses << ",\"blocks\":" << r.blocks
                      << ",\"p99_ratio\":" << r.p99 << ",\"p999_ratio\":" << r.p999 << ",\"max_ratio\":" << r.maxRatio << ",\"worst_block\":" << r.worstBlock << ",\"peak\":" << r.peak << "}\n";
        else
            std::cout << name << " block " << block << " " << qname << " threads " << threads << ": rtf " << r.rtf << ", misses " << r.misses << "/" << r.blocks
                      << ", p99 " << r.p99 << ", p99.9 " << r.p999 << ", max " << r.maxRatio << " at block " << r.worstBlock << " (ratio of block time to deadline), peak " << r.peak << "\n";
        require(r.rtf >= minRtf, "real-time factor below --min-rtf");
        if (equal) {
            const Result s = run(argv[1], name, events, block, quality, 0, seconds, rt);
            require(s.hash == r.hash, "output with helper threads differs from single-threaded output");
            std::cout << "PASS: output identical with " << threads << " and 0 helper threads (hash " << std::hex << r.hash << ")\n";
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}
