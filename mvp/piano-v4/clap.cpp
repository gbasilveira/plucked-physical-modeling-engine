// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cmath>
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
    CLAP_VERSION, "org.pme.experimental.piano-mvp-v4", "Piano MVP 4 - Baked radiation", "PME",
    "", "", "", "0.6.0", "Experimental coupled-unison piano with a baked (reduced, float-drive) soundboard radiation stage for Live and Balanced (Radiation: Auto / Modal / Baked), per-key unison mistuning, uneven hammer contact and false-string partials (Imperfection amount), a Voices setting and: one shared soundboard, sympathetic strings, leaky dampers, string tension modulation, una corda (CC67), undamped treble; quality Live / Balanced / Full (Full is selected automatically while the host renders offline)", features};
using piano_mvp_v4::Piano;
enum : clap_id { kParamQuality = 0, kParamThreads = 1, kParamAdaptive = 2, kParamImperfection = 3, kParamVoices = 4, kParamRadiation = 5 };
constexpr int kVoiceChoices[] = {0, 16, 32, 48, 64};   // Voices parameter: index -> struck-voice limit (0 = Auto)
constexpr int kMaxThreads = 3;
struct Instance {
    clap_plugin_t plugin{};
    Piano piano;
    bool active{}, processing{};
    std::atomic<int> quality{0};                 // 0 Live, 1 Balanced, 2 Full (the parameter)
    std::atomic<int> threads{1};                 // helper threads taking part, 0..kMaxThreads
    std::atomic<int> adaptive{1};                // 0.5.4.1 adaptive load shedding (Live/Balanced), default on
    std::atomic<double> imperfection{Piano::defaultImperfection};   // 0..2 (the parameter)
    std::atomic<int> voices{0};                  // index into kVoiceChoices (the parameter)
    std::atomic<int> radiation{0};               // 0 Auto, 1 Modal, 2 Baked (the parameter, 0.6)
    std::atomic<int> offline{0};                 // clap.render: offline rendering forces Full
    int appliedQuality{-1}, appliedThreads{-1}, appliedAdaptive{-1};  // audio thread
    // Audio thread: make the engine match the parameters (cheap; no allocation).
    void applySettings() noexcept {
        const int q = offline.load(std::memory_order_relaxed) ? 2 : quality.load(std::memory_order_relaxed);
        if (q != appliedQuality) { piano.setQuality(static_cast<Piano::Quality>(q)); appliedQuality = q; }
        piano.setVoices(kVoiceChoices[std::clamp(voices.load(std::memory_order_relaxed), 0, 4)]);
        piano.setImperfection(imperfection.load(std::memory_order_relaxed));   // taken by each key at its next note-on
        piano.setRadiation(static_cast<Piano::Radiation>(std::clamp(radiation.load(std::memory_order_relaxed), 0, 2)));   // applied at the next block
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
                    if (p->param_id == kParamVoices) s.voices.store(std::clamp(static_cast<int>(p->value + 0.5), 0, 4));
                    if (p->param_id == kParamRadiation) s.radiation.store(std::clamp(static_cast<int>(p->value + 0.5), 0, 2));
                    if (p->param_id == kParamImperfection) s.imperfection.store(std::clamp(p->value, 0.0, Piano::maxImperfection));
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
const char* const radiationNames[] = {"Auto", "Modal", "Baked"};
const clap_plugin_params_t params = {
    [](const clap_plugin_t*) -> std::uint32_t { return 6; },
    [](const clap_plugin_t*, std::uint32_t i, clap_param_info_t* info) {
        if (i > 5 || !info) return false;
        *info = {};
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
        if (i == 0) {
            info->id = kParamQuality; std::strcpy(info->name, "Quality"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = 2; info->default_value = 0;
        } else if (i == 5) {
            info->id = kParamRadiation; std::strcpy(info->name, "Radiation"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = 2; info->default_value = 0;
        } else if (i == 4) {
            info->id = kParamVoices; std::strcpy(info->name, "Voices"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = 4; info->default_value = 0;
        } else if (i == 3) {
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;   // continuous
            info->id = kParamImperfection; std::strcpy(info->name, "Imperfection"); std::strcpy(info->module, "Engine");
            info->min_value = 0; info->max_value = Piano::maxImperfection; info->default_value = Piano::defaultImperfection;
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
        if (id == kParamImperfection) { *v = self(p).imperfection.load(); return true; }
        if (id == kParamVoices) { *v = self(p).voices.load(); return true; }
        if (id == kParamRadiation) { *v = self(p).radiation.load(); return true; }
        return false;
    },
    [](const clap_plugin_t*, clap_id id, double v, char* out, std::uint32_t cap) {
        if (!out || !cap) return false;
        if (id == kParamQuality) { std::snprintf(out, cap, "%s", qualityNames[std::clamp(static_cast<int>(v + 0.5), 0, 2)]); return true; }
        if (id == kParamThreads) { std::snprintf(out, cap, "%d", static_cast<int>(v + 0.5)); return true; }
        if (id == kParamAdaptive) { std::snprintf(out, cap, "%s", v > 0.5 ? "On" : "Off"); return true; }
        if (id == kParamImperfection) { std::snprintf(out, cap, "%.2f", v); return true; }
        if (id == kParamRadiation) { std::snprintf(out, cap, "%s", radiationNames[std::clamp(static_cast<int>(v + 0.5), 0, 2)]); return true; }
        if (id == kParamVoices) { const int i = std::clamp(static_cast<int>(v + 0.5), 0, 4); if (i == 0) std::snprintf(out, cap, "Auto"); else std::snprintf(out, cap, "%d", kVoiceChoices[i]); return true; }
        return false;
    },
    [](const clap_plugin_t*, clap_id id, const char* text, double* v) {
        if (!text || !v) return false;
        if (id == kParamQuality) {
            for (int q = 0; q < 3; ++q) if (!strcasecmp(text, qualityNames[q])) { *v = q; return true; }
            return false;
        }
        if (id == kParamRadiation) {
            for (int q = 0; q < 3; ++q) if (!strcasecmp(text, radiationNames[q])) { *v = q; return true; }
            return false;
        }
        if (id == kParamThreads) { *v = std::clamp(std::atoi(text), 0, kMaxThreads); return true; }
        if (id == kParamAdaptive) { *v = (!strcasecmp(text, "off") || !std::strcmp(text, "0")) ? 0 : 1; return true; }
        if (id == kParamImperfection) { *v = std::clamp(std::atof(text), 0.0, Piano::maxImperfection); return true; }
        if (id == kParamVoices) {
            if (!strcasecmp(text, "auto")) { *v = 0; return true; }
            for (int i = 1; i < 5; ++i) if (std::atoi(text) == kVoiceChoices[i]) { *v = i; return true; }
            return false;
        }
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
            if (pv->param_id == kParamVoices) self(p).voices.store(std::clamp(static_cast<int>(pv->value + 0.5), 0, 4));
            if (pv->param_id == kParamRadiation) self(p).radiation.store(std::clamp(static_cast<int>(pv->value + 0.5), 0, 2));
            if (pv->param_id == kParamImperfection) self(p).imperfection.store(std::clamp(pv->value, 0.0, Piano::maxImperfection));
        }
    }};
// clap.render: while the host renders offline the plug-in uses the full model (Quality is ignored).
const clap_plugin_render_t renderExt = {
    [](const clap_plugin_t*) { return false; },
    [](const clap_plugin_t* p, clap_plugin_render_mode mode) {
        self(p).offline.store(mode == CLAP_RENDER_OFFLINE ? 1 : 0);
        return true;
    }};
// State: magic, quality, threads, adaptive-off (byte 6, 0.5.4.1), imperfection (byte 7: 0 = not stored, default; else round(100 x amount) + 1),
// voices (byte 8: 0 = not stored, Auto; else index + 1), radiation (byte 9, 0.6: 0 = not stored, Auto; else value + 1).
// States of 0.5.4 / 0.5.4.1 have 7 bytes, those of 0.5.5 have 9; missing bytes load as the defaults.
constexpr char kStateMagic[4] = {'P', 'M', 'S', '4'};
const clap_plugin_state_t stateExt = {
    [](const clap_plugin_t* p, const clap_ostream_t* stream) {
        unsigned char buf[10] = {kStateMagic[0], kStateMagic[1], kStateMagic[2], kStateMagic[3],
                                static_cast<unsigned char>(self(p).quality.load()),
                                static_cast<unsigned char>(self(p).threads.load()),
                                static_cast<unsigned char>(self(p).adaptive.load() ? 0 : 1),   // byte 6: adaptive off
                                static_cast<unsigned char>(std::lround(100.0 * self(p).imperfection.load()) + 1),
                                static_cast<unsigned char>(self(p).voices.load() + 1),
                                static_cast<unsigned char>(self(p).radiation.load() + 1)};
        return stream && stream->write(stream, buf, 10) == 10;
    },
    [](const clap_plugin_t* p, const clap_istream_t* stream) {
        unsigned char buf[10] = {};
        if (!stream || stream->read(stream, buf, 7) != 7 || std::memcmp(buf, kStateMagic, 4)) return false;
        self(p).quality.store(std::clamp<int>(buf[4], 0, 2)); self(p).threads.store(std::clamp<int>(buf[5], 0, kMaxThreads)); self(p).adaptive.store(buf[6] ? 0 : 1);
        if (stream->read(stream, buf + 7, 1) != 1) buf[7] = 0;   // older state: no imperfection / voices bytes
        else if (stream->read(stream, buf + 8, 1) != 1) buf[8] = 0;
        else if (stream->read(stream, buf + 9, 1) != 1) buf[9] = 0;
        self(p).imperfection.store(buf[7] == 0 ? Piano::defaultImperfection : std::clamp((buf[7] - 1) / 100.0, 0.0, Piano::maxImperfection));
        self(p).voices.store(buf[8] == 0 ? 0 : std::clamp<int>(buf[8] - 1, 0, 4));
        self(p).radiation.store(buf[9] == 0 ? 0 : std::clamp<int>(buf[9] - 1, 0, 2));
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
