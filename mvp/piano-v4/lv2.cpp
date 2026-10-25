// SPDX-License-Identifier: MIT
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include "midi.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cstring>
#include <memory>

namespace {
constexpr const char* kUri = "https://gbasilveira.github.io/plucked-physical-modeling-engine/mvp/piano-v4";
struct Instance {
    piano_mvp_v4::Piano piano;
    const LV2_Atom_Sequence* midi{};
    float* left{};
    float* right{};
    LV2_URID midiType{}, sequenceType{}, frameTime{}, beatTime{};
    bool active{};
};
LV2_Handle instantiate(const LV2_Descriptor*,double fs,const char*,const LV2_Feature* const* features) {
    const LV2_URID_Map* map = nullptr;
    if (features) for (const auto* const* feature = features; *feature; ++feature)
        if ((*feature)->URI && std::strcmp((*feature)->URI,LV2_URID__map) == 0)
            map = static_cast<const LV2_URID_Map*>((*feature)->data);
    if (!map || !map->map) return nullptr;
    try {
        auto s = std::make_unique<Instance>();
        s->midiType = map->map(map->handle,LV2_MIDI__MidiEvent);
        s->sequenceType = map->map(map->handle,LV2_ATOM__Sequence);
        s->frameTime = map->map(map->handle,LV2_ATOM__frameTime);
        s->beatTime = map->map(map->handle,LV2_ATOM__beatTime);
        if (!s->midiType || !s->sequenceType || !s->frameTime || !s->beatTime || !s->piano.setVoiceLimit(piano_mvp_v4::Piano::maxVoices) || !s->piano.setSympatheticLimit(piano_mvp_v4::Piano::pluginSympatheticLimit) || !s->piano.prepare(fs)) return nullptr;
        return s.release();
    } catch (...) { return nullptr; }
}
void connect(LV2_Handle handle,std::uint32_t port,void* data) {
    auto& s = *static_cast<Instance*>(handle);
    if (port == 0) s.midi = static_cast<const LV2_Atom_Sequence*>(data);
    if (port == 1) s.left = static_cast<float*>(data);
    if (port == 2) s.right = static_cast<float*>(data);
}
void activate(LV2_Handle handle) {
    auto& s = *static_cast<Instance*>(handle);
    s.piano.reset(); s.active = true;
}
void run(LV2_Handle handle,std::uint32_t frames) {
    pme::simd::ScopedFlushDenormals denormals;
    auto& s = *static_cast<Instance*>(handle);
    if (!s.active) {
        if (s.left) std::fill_n(s.left,frames,0.0f);
        if (s.right) std::fill_n(s.right,frames,0.0f);
        return;
    }
    std::uint32_t cursor = 0;
    const auto renderUntil = [&](std::uint32_t end) {
        while (cursor < end) {
            float l,r; s.piano.sample(l,r);
            if (s.left) s.left[cursor] = l;
            if (s.right) s.right[cursor] = r;
            ++cursor;
        }
    };
    const auto* seq = s.midi;
    // In run(), integer audio-frame timestamps are the default. Carla 2.5.10's
    // atom buffer helper does not initialise Sequence_Body::unit. Do not mute a
    // valid frame-timed sequence because that unused header field contains junk.
    // Explicit beat timestamps remain unsupported; all event bounds are checked.
    if (seq && seq->atom.type == s.sequenceType && seq->atom.size >= sizeof(seq->body) &&
        seq->body.unit != s.beatTime) {
        const auto* body = reinterpret_cast<const std::uint8_t*>(&seq->body);
        std::size_t offset = sizeof(seq->body);
        while (offset <= seq->atom.size && seq->atom.size-offset >= sizeof(LV2_Atom_Event)) {
            LV2_Atom_Event event;
            std::memcpy(&event,body+offset,sizeof(event));
            const std::size_t remaining = seq->atom.size-offset-sizeof(event);
            if (event.body.size > remaining) break;
            const auto padded = (static_cast<std::uint64_t>(event.body.size)+7U)&~std::uint64_t{7U};
            if (padded > remaining) break;
            if (event.time.frames >= 0 && static_cast<std::uint64_t>(event.time.frames) < frames &&
                event.body.type == s.midiType) {
                renderUntil(std::max(cursor,static_cast<std::uint32_t>(event.time.frames)));
                piano_mvp_v4::dispatchMidi(s.piano,body+offset+sizeof(event),event.body.size);
            }
            offset += sizeof(event)+static_cast<std::size_t>(padded);
        }
    }
    renderUntil(frames);
}
void deactivate(LV2_Handle handle) { static_cast<Instance*>(handle)->active = false; }
void cleanup(LV2_Handle handle) { delete static_cast<Instance*>(handle); }
const void* extension(const char*) { return nullptr; }
const LV2_Descriptor descriptor{kUri,instantiate,connect,activate,run,deactivate,cleanup,extension};
}
extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(std::uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
