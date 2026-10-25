// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include "piano.hpp"
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <cstring>

namespace {
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                   CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION, "org.pme.experimental.piano-mvp-v4", "Piano MVP 4 — Bridge feedback", "PME",
    "", "", "", "0.4.0", "Experimental moving-bridge piano: 88 keys, 16 voices, sustain", features};
struct Instance {
    clap_plugin_t plugin{};
    piano_mvp_v4::Piano piano;
    bool active{}, processing{};
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
    for (std::uint32_t frame = 0; frame < block->frames_count; ++frame) {
        while (next < count) {
            const auto* e = events->get(events, next);
            if (!e) { ++next; continue; }
            if (e->time > frame) break;
            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_MIDI &&
                e->size >= sizeof(clap_event_midi_t)) {
                const auto* m = reinterpret_cast<const clap_event_midi_t*>(e);
                if (m->port_index == 0) piano_mvp_v4::dispatchMidi(s.piano,m->data,3);
            }
            ++next;
        }
        float l, r; s.piano.sample(l, r);
        if (out.data32) { out.data32[0][frame] = l; out.data32[1][frame] = r; }
        else { out.data64[0][frame] = l; out.data64[1][frame] = r; }
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
const void* CLAP_ABI extension(const clap_plugin_t*, const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
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
                try { return self(p).active = self(p).piano.prepare(fs); }
                catch (...) { return self(p).active = false; }
            },
            [](const clap_plugin_t* p) { self(p).active = self(p).processing = false; self(p).piano.reset(); },
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
