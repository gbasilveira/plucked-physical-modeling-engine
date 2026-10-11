// SPDX-License-Identifier: MIT
#pragma once

// Test support for the LV2 adapter: a URID map, a recording engine that notes the calling thread,
// a recording work:schedule, hand-built atom sequences and a rig that wires one Plugin to buffers.
// `process` and `schedule_work` allocate nothing, so `run` works under the RT trap.

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lv2_shell.hpp"
#include "pme/plugin/engine_adapter.hpp"
#include "turtle.hpp"

namespace pme::lv2::test {

// ---- urid:map -------------------------------------------------------------------------------

class UridMap {
public:
    UridMap() {
        map_.handle = this;
        map_.map = [](LV2_URID_Map_Handle h, const char* uri) {
            return static_cast<UridMap*>(h)->intern(uri);
        };
    }
    LV2_URID_Map* map() { return &map_; }
    LV2_URID intern(const char* uri) {
        for (std::size_t i = 0; i < uris_.size(); ++i) {
            if (uris_[i] == uri) {
                return static_cast<LV2_URID>(i + 1);
            }
        }
        uris_.emplace_back(uri);
        return static_cast<LV2_URID>(uris_.size());
    }

private:
    std::vector<std::string> uris_;
    LV2_URID_Map map_{};
};

// ---- recording engine -----------------------------------------------------------------------

/// Records the events of every `process` call and the thread of every call. Not `final`-bound to
/// the D1 fake because the D3 tests need the calling thread.
struct RecEngine final : plugin::EngineAdapter {
    explicit RecEngine(Status prepareResult = Status::Ok) : prepareStatus(prepareResult) {
        events.reserve(4096);
        blocks.reserve(256);
    }
    Status prepare(const EngineConfig& config) override {
        lastConfig = config;
        ++prepareCalls;
        ++mainThreadCalls;
        return prepareStatus;
    }
    void setInstrument(std::shared_ptr<const CompiledInstrument> /*i*/) noexcept override {
        ++mainThreadCalls;
    }
    void collectRetired() noexcept override {
        ++collectCalls;
        ++mainThreadCalls;
        collectThread = std::this_thread::get_id();
    }
    void process(const EventList& list, AudioOut& out, int frames) noexcept override {
        processThread = std::this_thread::get_id();
        const std::size_t first = events.size();
        for (const Event& e : list) {
            if (events.size() < events.capacity()) {
                events.push_back(e);
            }
        }
        if (blocks.size() < blocks.capacity()) {
            blocks.push_back({first, events.size() - first, frames});
        }
        for (float* ch : out.channels) {
            std::memset(ch, 0, sizeof(float) * static_cast<std::size_t>(frames));
        }
    }
    void reset() noexcept override {}
    bool popDiagnostic(Diagnostic& /*d*/) noexcept override { return false; }

    struct Block {
        std::size_t first;
        std::size_t count;
        int frames;
    };
    /// Forget everything recorded so far (events, blocks); counters stay.
    void clearRecording() {
        events.clear();
        blocks.clear();
    }

    Status prepareStatus;
    EngineConfig lastConfig{};
    std::atomic<int> prepareCalls{0};
    std::atomic<int> collectCalls{0};
    std::atomic<int> mainThreadCalls{0};  ///< prepare, setInstrument, collectRetired
    std::thread::id processThread;
    std::thread::id collectThread;
    std::vector<Event> events;
    std::vector<Block> blocks;
};

// ---- work:schedule ---------------------------------------------------------------------------

struct Scheduler {
    Scheduler() {
        feature.handle = this;
        feature.schedule_work = [](LV2_Worker_Schedule_Handle h, std::uint32_t size,
                                   const void* data) {
            auto* self = static_cast<Scheduler*>(h);
            if (self->count >= self->jobs.size() || size != sizeof(Job)) {
                return LV2_WORKER_ERR_NO_SPACE;
            }
            std::memcpy(&self->jobs[self->count++], data, sizeof(Job));
            return self->result;
        };
    }
    LV2_Worker_Schedule feature{};
    std::array<Job, 64> jobs{};
    std::size_t count{0};
    LV2_Worker_Status result{LV2_WORKER_SUCCESS};

    [[nodiscard]] std::size_t countOf(JobKind kind) const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < count; ++i) {
            n += jobs[i].kind == static_cast<std::uint32_t>(kind) ? 1U : 0U;
        }
        return n;
    }
};

// ---- atom sequences -------------------------------------------------------------------------

/// A 64-bit aligned buffer holding one atom sequence.
struct AtomBuffer {
    explicit AtomBuffer(std::size_t bytes = 16384) : words(bytes / 8, 0) {}
    std::vector<std::uint64_t> words;
    [[nodiscard]] std::uint8_t* bytes() { return reinterpret_cast<std::uint8_t*>(words.data()); }
    [[nodiscard]] std::size_t size() const { return words.size() * 8; }
    [[nodiscard]] LV2_Atom_Sequence* sequence() {
        return reinterpret_cast<LV2_Atom_Sequence*>(words.data());
    }
};

/// Builds an input sequence (frame time unit) with the forge.
class SequenceBuilder {
public:
    SequenceBuilder(AtomBuffer& buffer, UridMap& urids) {
        lv2_atom_forge_init(&forge_, urids.map());
        lv2_atom_forge_set_buffer(&forge_, buffer.bytes(), buffer.size());
        lv2_atom_forge_sequence_head(&forge_, &frame_, 0);
        midi_ = urids.intern(LV2_MIDI__MidiEvent);
        set_ = urids.intern(LV2_PATCH__Set);
        get_ = urids.intern(LV2_PATCH__Get);
        property_ = urids.intern(LV2_PATCH__property);
        value_ = urids.intern(LV2_PATCH__value);
    }
    SequenceBuilder& midi(std::int64_t frame, std::uint8_t status, std::uint8_t d1,
                          std::uint8_t d2) {
        const std::array<std::uint8_t, 3> msg{status, d1, d2};
        lv2_atom_forge_frame_time(&forge_, frame);
        lv2_atom_forge_atom(&forge_, 3, midi_);
        lv2_atom_forge_write(&forge_, msg.data(), 3);
        return *this;
    }
    SequenceBuilder& setDouble(std::int64_t frame, LV2_URID property, double value) {
        lv2_atom_forge_frame_time(&forge_, frame);
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&forge_, &obj, 0, set_);
        lv2_atom_forge_key(&forge_, property_);
        lv2_atom_forge_urid(&forge_, property);
        lv2_atom_forge_key(&forge_, value_);
        lv2_atom_forge_double(&forge_, value);
        lv2_atom_forge_pop(&forge_, &obj);
        return *this;
    }
    SequenceBuilder& setPath(std::int64_t frame, LV2_URID property, const std::string& path) {
        lv2_atom_forge_frame_time(&forge_, frame);
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&forge_, &obj, 0, set_);
        lv2_atom_forge_key(&forge_, property_);
        lv2_atom_forge_urid(&forge_, property);
        lv2_atom_forge_key(&forge_, value_);
        lv2_atom_forge_path(&forge_, path.c_str(), static_cast<std::uint32_t>(path.size()));
        lv2_atom_forge_pop(&forge_, &obj);
        return *this;
    }
    /// patch:Get; `property` 0 means "all properties".
    SequenceBuilder& get(std::int64_t frame, LV2_URID property) {
        lv2_atom_forge_frame_time(&forge_, frame);
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&forge_, &obj, 0, get_);
        if (property != 0) {
            lv2_atom_forge_key(&forge_, property_);
            lv2_atom_forge_urid(&forge_, property);
        }
        lv2_atom_forge_pop(&forge_, &obj);
        return *this;
    }
    void end() { lv2_atom_forge_pop(&forge_, &frame_); }

private:
    LV2_Atom_Forge forge_{};
    LV2_Atom_Forge_Frame frame_{};
    LV2_URID midi_{};
    LV2_URID set_{};
    LV2_URID get_{};
    LV2_URID property_{};
    LV2_URID value_{};
};

/// One patch:Set read back from the notify port.
struct Reply {
    std::int64_t frame{};
    LV2_URID property{};
    bool isPath{false};
    double number{};
    std::string path;
};

inline std::vector<Reply> readNotify(AtomBuffer& buffer, UridMap& urids) {
    std::vector<Reply> out;
    const LV2_URID object = urids.intern(LV2_ATOM__Object);
    const LV2_URID set = urids.intern(LV2_PATCH__Set);
    const LV2_URID property = urids.intern(LV2_PATCH__property);
    const LV2_URID value = urids.intern(LV2_PATCH__value);
    const LV2_URID atomDouble = urids.intern(LV2_ATOM__Double);
    const LV2_URID atomPath = urids.intern(LV2_ATOM__Path);
    LV2_ATOM_SEQUENCE_FOREACH(buffer.sequence(), ev) {
        if (ev->body.type != object) {
            continue;
        }
        const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
        if (obj->body.otype != set) {
            continue;
        }
        Reply r;
        r.frame = ev->time.frames;
        LV2_ATOM_OBJECT_FOREACH(obj, prop) {
            if (prop->key == property) {
                r.property = reinterpret_cast<const LV2_Atom_URID*>(&prop->value)->body;
            } else if (prop->key == value) {
                if (prop->value.type == atomDouble) {
                    r.number = reinterpret_cast<const LV2_Atom_Double*>(&prop->value)->body;
                } else if (prop->value.type == atomPath) {
                    r.isPath = true;
                    r.path = static_cast<const char*>(LV2_ATOM_BODY_CONST(&prop->value));
                }
            }
        }
        out.push_back(r);
    }
    return out;
}

// ---- state store ----------------------------------------------------------------------------

struct StateStore {
    struct Item {
        std::uint32_t type{};
        std::uint32_t flags{};
        std::vector<std::uint8_t> bytes;
    };
    std::map<std::uint32_t, Item> items;

    static LV2_State_Status store(LV2_State_Handle h, std::uint32_t key, const void* value,
                                  std::size_t size, std::uint32_t type, std::uint32_t flags) {
        auto* self = static_cast<StateStore*>(h);
        const auto* b = static_cast<const std::uint8_t*>(value);
        self->items[key] = Item{type, flags, std::vector<std::uint8_t>(b, b + size)};
        return LV2_STATE_SUCCESS;
    }
    static const void* retrieve(LV2_State_Handle h, std::uint32_t key, std::size_t* size,
                                std::uint32_t* type, std::uint32_t* flags) {
        auto* self = static_cast<StateStore*>(h);
        const auto it = self->items.find(key);
        if (it == self->items.end()) {
            return nullptr;
        }
        *size = it->second.bytes.size();
        *type = it->second.type;
        *flags = it->second.flags;
        return it->second.bytes.data();
    }
    /// Put the JSON text (with its NUL) under `key` as atom:String.
    void put(std::uint32_t key, std::uint32_t type, const std::string& text) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(text.c_str());
        items[key] = Item{type, 0, std::vector<std::uint8_t>(b, b + text.size() + 1)};
    }
};

// ---- the rig --------------------------------------------------------------------------------

constexpr std::uint32_t kRigBlock = 512;

struct Rig {
    explicit Rig(bool withWorker = true, Status prepare = Status::Ok) : in(), notify(8192) {
        auto engineOwned = std::make_unique<RecEngine>(prepare);
        engine = engineOwned.get();
        plugin =
            std::make_unique<Plugin>(std::move(engineOwned), 48000.0, urids.map(),
                                     withWorker ? &scheduler.feature : nullptr, nullptr, kRigBlock);
        left.assign(kRigBlock, 1.0F);
        right.assign(kRigBlock, 1.0F);
        for (std::size_t i = 0; i < kControlPortCount; ++i) {
            controls[i] = static_cast<float>(findParam(kControlPortParams[i])->defaultValue);
        }
        plugin->connectPort(kPortIn, in.sequence());
        plugin->connectPort(kPortNotify, notify.sequence());
        plugin->connectPort(kPortOutLeft, left.data());
        plugin->connectPort(kPortOutRight, right.data());
        for (std::size_t i = 0; i < kControlPortCount; ++i) {
            plugin->connectPort(kFirstControlPort + static_cast<std::uint32_t>(i), &controls[i]);
        }
        atomSequence = urids.intern(LV2_ATOM__Sequence);
        atomChunk = urids.intern(LV2_ATOM__Chunk);
    }

    /// A fresh, empty input sequence; fill it with the returned builder, then call `end()`.
    SequenceBuilder begin() {
        std::memset(in.bytes(), 0, in.size());
        return {in, urids};
    }
    /// An empty input sequence.
    void emptyInput() { begin().end(); }

    /// One `run` of `frames` frames; the notify buffer is prepared like a host does.
    void run(std::uint32_t frames = kRigBlock) {
        std::memset(notify.bytes(), 0, notify.size());
        notify.sequence()->atom.type = atomChunk;
        notify.sequence()->atom.size = static_cast<std::uint32_t>(notify.size() - sizeof(LV2_Atom));
        plugin->run(frames);
    }

    [[nodiscard]] LV2_URID structuralUrid(ParamId id) {
        return urids.intern(structuralParamUri(*findParam(id)).c_str());
    }
    [[nodiscard]] LV2_URID instrumentUrid() { return urids.intern(instrumentUri().c_str()); }

    UridMap urids;
    Scheduler scheduler;
    RecEngine* engine{nullptr};
    std::unique_ptr<Plugin> plugin;
    AtomBuffer in;
    AtomBuffer notify;
    std::vector<float> left;
    std::vector<float> right;
    std::array<float, kControlPortCount> controls{};
    LV2_URID atomSequence{};
    LV2_URID atomChunk{};
};

}  // namespace pme::lv2::test
