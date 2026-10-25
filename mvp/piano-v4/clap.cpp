// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cstring>
#include <strings.h>

namespace {
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                   CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION, "org.pme.experimental.piano-mvp-v4", "Piano MVP 4 - Real time", "PME",
    "", "", "", "0.5.4.1", "Experimental coupled-unison piano: one shared soundboard, sympathetic strings, leaky dampers, string tension modulation, una corda (CC67), undamped treble; quality Live / Balanced / Full (Full is selected automatically while the host renders offline)", features};
using piano_mvp_v4::Piano;
enum : clap_id { kParamQuality = 0, kParamThreads = 1, kParamAdaptive = 2 };
constexpr int kMaxThreads = 3;
struct Instance {
    clap_plugin_t plugin{};
    Piano piano;
    bool active{}, processing{};
    std::atomic<int> quality{0};                 // 0 Live, 1 Balanced, 2 Full (the parameter)
    std::atomic<int> threads{1};                 // helper threads taking part, 0..kMaxThreads
    std::atomic<int> adaptive{1};                // 0.5.4.1 adaptive load shedding (Live/Balanced), default on
    std::atomic<int> offline{0};                 // clap.render: offline rendering forces Full
    int appliedQuality{-1}, appliedThreads{-1}, appliedAdaptive{-1};  // audio thread
    // Audio thread: make the engine match the parameters (cheap; no allocation).
    void applySettings() noexcept {
        const int q = offline.load(std::memory_order_relaxed) ? 2 : quality.load(std::memory_order_relaxed);
        if (q != appliedQuality) { piano.setQuality(static_cast<Piano::Quality>(q)); appliedQuality = q; }
        const int t = threads.load(std::memory_order_relaxed);
        if (t != appliedThreads) { piano.setActiveWorkers(t); appliedThreads = t; }
        const int a = (adaptive.load(std::memory_order_relaxed) != 0 && q != 2) ? 1 : 0;
        if (a != appliedAdaptive) { piano.setAdaptive(a != 0); appliedAdaptive = a; }
    }
};
Instance& self(const clap_plugin_t* p) { return *static_cast<Instance*>(p->plugin_data); }
clap_process_status CLAP_ABI process(const clap_plugin_t* plugin, const clap_process_t* block) {
    pme::simd::ScopedFlushDenormals denormals;
    auto& s = self(plugin);
    if (!s.processing || !block || block->audio_outputs_count != 1 || !block->audio_outputs)
        return CLAP_PROCESS_ERROR;
    auto& out = block->audio_outputs[0];
    if (out.channel_count != 2 || (!out.data32 && !out.data64)) return CLAP_PROCESS_ERROR;
    out.constant_mask = 0;
    const auto* events = block->in_events;
    const auto count = events ? events->size(events) : 0;
    std::uint32_t next = 0;
    s.applySettings();
    // The model is rendered in chunks of at most Piano::maxBlockFrames frames; the radiation of each
    // chunk finishes on the helper thread while the strings of the chunk are stepped.
    for (std::uint32_t base = 0; base < block->frames_count; base += Piano::maxBlockFrames) {
        const std::uint32_t n = std::min<std::uint32_t>(Piano::maxBlockFrames, block->frames_count - base);
        s.piano.blockBegin(n);
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t frame = base + k;
            while (next < count) {
                const auto* e = events->get(events, next);
                if (!e) { ++next; continue; }
                if (e->time > frame) break;
                if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_MIDI &&
                    e->size >= sizeof(clap_event_midi_t)) {
                    const auto* m = reinterpret_cast<const clap_event_midi_t*>(e);
                    if (m->port_index == 0) piano_mvp_v4::dispatchMidi(s.piano,m->data,3);
                } else if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE &&
                           e->size >= sizeof(clap_event_param_value_t)) {
                    const auto* p = reinterpret_cast<const clap_event_param_value_t*>(e);
                    if (p->param_id == kParamQuality) s.quality.store(std::clamp(static_cast<int>(p->value + 0.5), 0, 2));
                    if (p->param_id == kParamThreads) s.threads.store(std::clamp(static_cast<int>(p->value + 0.5), 0, kMaxThreads));
                    if (p->param_id == kParamAdaptive) s.adaptive.store(std::clamp(static_cast<int>(p->value + 0.5), 0, 1));
                    s.applySettings();
                }
                ++next;
            }
            s.piano.blockStep();
        }
        if (out.data32) s.piano.blockEnd(out.data32[0] + base, out.data32[1] + base);
        else s.piano.blockEnd(out.data64[0] + base, out.data64[1] + base);
    }
    return CLAP_PROCESS_CONTINUE;
}
const clap_plugin_audio_ports_t audioPorts = {
    [](const clap_plugin_t*, bool input) -> std::uint32_t { return input ? 0 : 1; },
    [](const clap_plugin_t*, std::uint32_t index, bool input, clap_audio_port_info_t* info) {
        if (input || index || !info) return false;
        *info = {}; info->id = 0; std::strcpy(info->name, "Piano stereo");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->channel_count = 2; info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID; return true;
    }};
const clap_plugin_note_ports_t notePorts = {
    [](const clap_plugin_t*, bool input) -> std::uint32_t { return input ? 1 : 0; },
    [](const clap_plugin_t*, std::uint32_t index, bool input, clap_note_port_info_t* info) {
        if (!input || index || !info) return false;
        *info = {}; info->id = 0; std::strcpy(info->name, "Piano MIDI (omni)");
        info->supported_dialects = info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI; return true;
    }};
const char* const qualityNames[] = {"Live", "Balanced", "Full"};
const clap_plugin_params_t params = {
    [](const clap_plugin_t*) -> std::uint32_t { return 3; },
    [](const clap_plugin_t*, std::uint32_t i, clap_param_info_t* info) {
        if (i > 2 || !info) return false;
        *info = {};
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
        if (i == 0) {
            info->id = kParamQuality; std::strcpy(info->name, "Quality"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = 2; info->default_value = 0;
        } else if (i == 2) {
            info->id = kParamAdaptive; std::strcpy(info->name, "Adaptive load shedding"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = 1; info->default_value = 1;
        } else {
            info->id = kParamThreads; std::strcpy(info->name, "Worker threads"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = kMaxThreads; info->default_value = 1;
        }
        return true;
    },
    [](const clap_plugin_t* p, clap_id id, double* v) {
        if (!v) return false;
        if (id == kParamQuality) { *v = self(p).quality.load(); return true; }
        if (id == kParamThreads) { *v = self(p).threads.load(); return true; }
        if (id == kParamAdaptive) { *v = self(p).adaptive.load(); return true; }
        return false;
    },
    [](const clap_plugin_t*, clap_id id, double v, char* out, std::uint32_t cap) {
        if (!out || !cap) return false;
        if (id == kParamQuality) { std::snprintf(out, cap, "%s", qualityNames[std::clamp(static_cast<int>(v + 0.5), 0, 2)]); return true; }
        if (id == kParamThreads) { std::snprintf(out, cap, "%d", static_cast<int>(v + 0.5)); return true; }
        if (id == kParamAdaptive) { std::snprintf(out, cap, "%s", v > 0.5 ? "On" : "Off"); return true; }
        return false;
    },
    [](const clap_plugin_t*, clap_id id, const char* text, double* v) {
        if (!text || !v) return false;
        if (id == kParamQuality) {
            for (int q = 0; q < 3; ++q) if (!strcasecmp(text, qualityNames[q])) { *v = q; return true; }
            return false;
        }
        if (id == kParamThreads) { *v = std::clamp(std::atoi(text), 0, kMaxThreads); return true; }
        if (id == kParamAdaptive) { *v = (!strcasecmp(text, "off") || !std::strcmp(text, "0")) ? 0 : 1; return true; }
        return false;
    },
    // Not processing: parameter changes arrive here.
    [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
        if (!in) return;
        for (std::uint32_t i = 0; i < in->size(in); ++i) {
            const auto* e = in->get(in, i);
            if (!e || e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE ||
                e->size < sizeof(clap_event_param_value_t)) continue;
            const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(e);
            if (pv->param_id == kParamQuality) self(p).quality.store(std::clamp(static_cast<int>(pv->value + 0.5), 0, 2));
            if (pv->param_id == kParamThreads) self(p).threads.store(std::clamp(static_cast<int>(pv->value + 0.5), 0, kMaxThreads));
            if (pv->param_id == kParamAdaptive) self(p).adaptive.store(std::clamp(static_cast<int>(pv->value + 0.5), 0, 1));
        }
    }};
// clap.render: while the host renders offline the plug-in uses the full model (Quality is ignored).
const clap_plugin_render_t renderExt = {
    [](const clap_plugin_t*) { return false; },
    [](const clap_plugin_t* p, clap_plugin_render_mode mode) {
        self(p).offline.store(mode == CLAP_RENDER_OFFLINE ? 1 : 0);
        return true;
    }};
// State: magic, quality, threads (three bytes after the magic).
constexpr char kStateMagic[4] = {'P', 'M', 'S', '4'};
const clap_plugin_state_t stateExt = {
    [](const clap_plugin_t* p, const clap_ostream_t* stream) {
        unsigned char buf[7] = {kStateMagic[0], kStateMagic[1], kStateMagic[2], kStateMagic[3],
                                static_cast<unsigned char>(self(p).quality.load()),
                                static_cast<unsigned char>(self(p).threads.load()),
                                static_cast<unsigned char>(self(p).adaptive.load() ? 0 : 1)};   // byte 6: adaptive off
        return stream && stream->write(stream, buf, 7) == 7;
    },
    [](const clap_plugin_t* p, const clap_istream_t* stream) {
        unsigned char buf[7] = {};
        if (!stream || stream->read(stream, buf, 7) != 7 || std::memcmp(buf, kStateMagic, 4)) return false;
        self(p).quality.store(std::clamp<int>(buf[4], 0, 2)); self(p).threads.store(std::clamp<int>(buf[5], 0, kMaxThreads)); self(p).adaptive.store(buf[6] ? 0 : 1);
        return true;
    }};
const void* CLAP_ABI extension(const clap_plugin_t*, const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &renderExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    return nullptr;
}
const clap_plugin_t* CLAP_ABI create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!host || !clap_version_is_compatible(host->clap_version) || !id || std::strcmp(id, descriptor.id))
        return nullptr;
    try {
        auto* s = new Instance;
        s->plugin = {
            &descriptor, s,
            [](const clap_plugin_t*) { return true; },
            [](const clap_plugin_t* p) { delete &self(p); },
            [](const clap_plugin_t* p, double fs, std::uint32_t min, std::uint32_t max) {
                if (!min || max < min) return false;
                // Threads are created here (never on the audio thread) and joined in deactivate().
                try {
                    auto& s = self(p);
                    s.piano.setWorkerThreads(std::min(Piano::defaultWorkerThreads(), kMaxThreads));
                    s.appliedQuality = s.appliedThreads = s.appliedAdaptive = -1;
                    if (!s.piano.prepare(fs)) return s.active = false;
                    s.piano.setQuality(static_cast<Piano::Quality>(s.offline.load() ? 2 : s.quality.load()));
                    s.applySettings();
                    s.piano.warmUp();
                    s.appliedQuality = s.appliedThreads = s.appliedAdaptive = -1; s.applySettings();
                    return s.active = true;
                } catch (...) { return self(p).active = false; }
            },
            [](const clap_plugin_t* p) { self(p).active = self(p).processing = false; self(p).piano.stopThreads(); self(p).piano.reset(); },
            [](const clap_plugin_t* p) { return self(p).processing = self(p).active; },
            [](const clap_plugin_t* p) { self(p).processing = false; },
            [](const clap_plugin_t* p) { self(p).piano.reset(); },
            process, extension, [](const clap_plugin_t*) {}};
        return &s->plugin;
    } catch (...) { return nullptr; }
}
const clap_plugin_factory_t factory = {
    [](const clap_plugin_factory_t*) -> std::uint32_t { return 1; },
    [](const clap_plugin_factory_t*, std::uint32_t i) -> const clap_plugin_descriptor_t* {
        return i == 0 ? &descriptor : nullptr;
    }, create};
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* {
        return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
    }};
