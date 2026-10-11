// SPDX-License-Identifier: MIT
#pragma once

// Test support for the CLAP adapter: a fake host, input event lists and in-memory streams. The
// event list and the process call never allocate once built, so they can run under the RT trap.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include <clap/clap.h>

#include "clap_shell.hpp"
#include "fake_engine.hpp"

namespace pme::clap::test {

/// A fake CLAP host. Optionally offers `clap.thread-pool`; the mock pool runs the tasks through
/// the plug-in's own `clap_plugin_thread_pool.exec`, in reverse order.
struct TestHost {
    TestHost() {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "test host";
        host.vendor = "pme";
        host.url = "";
        host.version = "1";
        host.get_extension = getExtension;
        host.request_restart = [](const clap_host_t*) {};
        host.request_process = [](const clap_host_t*) {};
        host.request_callback = [](const clap_host_t* h) { self(h).callbacks += 1; };
        pool.request_exec = requestExec;
    }
    static TestHost& self(const clap_host_t* h) { return *static_cast<TestHost*>(h->host_data); }

    static const void* getExtension(const clap_host_t* h, const char* id) {
        TestHost& t = self(h);
        if (t.offerPool && std::strcmp(id, CLAP_EXT_THREAD_POOL) == 0) {
            return &t.pool;
        }
        return nullptr;
    }
    static bool requestExec(const clap_host_t* h, uint32_t count) {
        TestHost& t = self(h);
        ++t.execRequests;
        if (t.plugin == nullptr) {
            return false;
        }
        const auto* ext = static_cast<const clap_plugin_thread_pool_t*>(
            t.plugin->get_extension(t.plugin, CLAP_EXT_THREAD_POOL));
        for (uint32_t i = count; i > 0; --i) {
            ext->exec(t.plugin, i - 1);
        }
        return true;
    }

    clap_host_t host{};
    clap_host_thread_pool_t pool{};
    const clap_plugin_t* plugin{nullptr};
    bool offerPool{false};
    int callbacks{0};
    int execRequests{0};
};

/// One plug-in instance around a FakeEngine (or the given adapter).
struct Instance {
    explicit Instance(bool withPool = false, pme::Status prepare = pme::Status::Ok) {
        host.offerPool = withPool;
        auto fake = std::make_unique<pme::plugin::test::FakeEngine>(prepare);
        engine = fake.get();
        plugin = createPlugin(&host.host, std::move(fake));
        host.plugin = plugin;
        initialised = plugin != nullptr && plugin->init(plugin);
    }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance() {
        if (plugin != nullptr) {
            plugin->destroy(plugin);
        }
    }

    template <typename Ext>
    const Ext* ext(const char* id) const {
        return static_cast<const Ext*>(plugin->get_extension(plugin, id));
    }

    TestHost host;
    const clap_plugin_t* plugin{nullptr};
    pme::plugin::test::FakeEngine* engine{nullptr};
    bool initialised{false};
};

/// Input events held in a vector; `get` never allocates.
class InEvents {
public:
    using Stored = std::variant<clap_event_note_t, clap_event_midi_t, clap_event_param_value_t>;

    InEvents() {
        list_.ctx = this;
        list_.size = [](const clap_input_events_t* l) {
            return static_cast<uint32_t>(self(l).events_.size());
        };
        list_.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
            return std::visit([](const auto& e) { return &e.header; }, self(l).events_[i]);
        };
        events_.reserve(64);
    }
    InEvents(const InEvents&) = delete;
    InEvents& operator=(const InEvents&) = delete;

    void note(uint32_t time, uint16_t type, int16_t key, double velocity) {
        clap_event_note_t e{};
        header(e.header, time, type, sizeof(e));
        e.note_id = -1;
        e.port_index = 0;
        e.channel = 0;
        e.key = key;
        e.velocity = velocity;
        events_.emplace_back(e);
    }
    void midi(uint32_t time, uint8_t status, uint8_t d1, uint8_t d2) {
        clap_event_midi_t e{};
        header(e.header, time, CLAP_EVENT_MIDI, sizeof(e));
        e.port_index = 0;
        e.data[0] = status;
        e.data[1] = d1;
        e.data[2] = d2;
        events_.emplace_back(e);
    }
    void param(uint32_t time, clap_id id, double value, uint32_t space = CLAP_CORE_EVENT_SPACE_ID) {
        clap_event_param_value_t e{};
        header(e.header, time, CLAP_EVENT_PARAM_VALUE, sizeof(e));
        e.header.space_id = static_cast<uint16_t>(space);
        e.param_id = id;
        e.cookie = nullptr;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = value;
        events_.emplace_back(e);
    }
    void clear() { events_.clear(); }
    const clap_input_events_t* list() const { return &list_; }

private:
    static const InEvents& self(const clap_input_events_t* l) {
        return *static_cast<const InEvents*>(l->ctx);
    }
    static void header(clap_event_header_t& h, uint32_t time, uint16_t type, std::size_t size) {
        h.size = static_cast<uint32_t>(size);
        h.time = time;
        h.space_id = CLAP_CORE_EVENT_SPACE_ID;
        h.type = type;
        h.flags = 0;
    }
    clap_input_events_t list_{};
    std::vector<Stored> events_;
};

/// Stereo (or n-channel) output buffer for `process`.
struct OutBuffers {
    explicit OutBuffers(uint32_t frames, uint32_t channels = 2)
        : storage(static_cast<std::size_t>(frames) * channels, 1.0F) {
        for (uint32_t c = 0; c < channels; ++c) {
            pointers.push_back(storage.data() + static_cast<std::size_t>(c) * frames);
        }
        buffer.data32 = pointers.data();
        buffer.data64 = nullptr;
        buffer.channel_count = channels;
        buffer.latency = 0;
        buffer.constant_mask = 0;
    }
    std::vector<float> storage;
    std::vector<float*> pointers;
    clap_audio_buffer_t buffer{};
};

/// Run one `process` call.
inline clap_process_status process(const clap_plugin_t* p, uint32_t frames, OutBuffers& out,
                                   const InEvents& in) {
    clap_process_t proc{};
    proc.steady_time = -1;
    proc.frames_count = frames;
    proc.transport = nullptr;
    proc.audio_inputs = nullptr;
    proc.audio_inputs_count = 0;
    proc.audio_outputs = &out.buffer;
    proc.audio_outputs_count = 1;
    proc.in_events = in.list();
    proc.out_events = nullptr;
    return p->process(p, &proc);
}

/// In-memory output stream; `chunk` limits the bytes accepted per call (partial writes).
struct MemoryOut {
    explicit MemoryOut(std::size_t chunkSize = 0) : chunk(chunkSize) {
        stream.ctx = this;
        stream.write = [](const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t {
            auto& self = *static_cast<MemoryOut*>(s->ctx);
            std::size_t n = static_cast<std::size_t>(size);
            if (self.chunk > 0) {
                n = std::min(n, self.chunk);
            }
            const auto* bytes = static_cast<const char*>(buffer);
            self.data.append(bytes, n);
            return static_cast<int64_t>(n);
        };
    }
    MemoryOut(const MemoryOut&) = delete;
    MemoryOut& operator=(const MemoryOut&) = delete;
    std::string data;
    std::size_t chunk;
    clap_ostream_t stream{};
};

/// In-memory input stream; reads at most `chunk` bytes per call (a small prime, like the validator).
struct MemoryIn {
    explicit MemoryIn(std::string bytes, std::size_t chunkSize = 7)
        : data(std::move(bytes)), chunk(chunkSize) {
        stream.ctx = this;
        stream.read = [](const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t {
            auto& self = *static_cast<MemoryIn*>(s->ctx);
            const std::size_t n = std::min({static_cast<std::size_t>(size), self.chunk,
                                            self.data.size() - self.position});
            std::memcpy(buffer, self.data.data() + self.position, n);
            self.position += n;
            return static_cast<int64_t>(n);
        };
    }
    MemoryIn(const MemoryIn&) = delete;
    MemoryIn& operator=(const MemoryIn&) = delete;
    std::string data;
    std::size_t chunk;
    std::size_t position{0};
    clap_istream_t stream{};
};

}  // namespace pme::clap::test
