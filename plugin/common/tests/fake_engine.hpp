// SPDX-License-Identifier: MIT
#pragma once

// Recording fake EngineAdapter for the D1 tests. `process` allocates nothing (the buffers are
// reserved in the constructor), so it can run under ScopedRealtimeThread.

#include <cstring>
#include <vector>

#include "pme/plugin/engine_adapter.hpp"

namespace pme::plugin::test {

struct FakeEngine final : EngineAdapter {
    struct Call {
        int frames{};
        std::size_t first{};  // index into `events`
        std::size_t count{};
    };

    explicit FakeEngine(Status prepareResult = Status::Ok) : prepareStatus(prepareResult) {
        events.reserve(4096);
        calls.reserve(256);
    }

    Status prepare(const EngineConfig& config) override {
        lastConfig = config;
        ++prepareCalls;
        return prepareStatus;
    }
    void setInstrument(std::shared_ptr<const CompiledInstrument> /*instrument*/) noexcept override {
    }
    void collectRetired() noexcept override { ++collectCalls; }
    void process(const EventList& list, AudioOut& out, int frames) noexcept override {
        Call c;
        c.frames = frames;
        c.first = events.size();
        for (const Event& e : list) {
            if (events.size() < events.capacity()) {
                events.push_back(e);
                ++c.count;
            }
        }
        if (calls.size() < calls.capacity()) {
            calls.push_back(c);
        }
        for (float* ch : out.channels) {
            std::memset(ch, 0, sizeof(float) * static_cast<std::size_t>(frames));
        }
        lastChannelCount = out.channels.size();
    }
    void reset() noexcept override { ++resetCalls; }
    bool popDiagnostic(Diagnostic& /*d*/) noexcept override { return false; }

    Status prepareStatus;
    EngineConfig lastConfig{};
    int prepareCalls{0};
    int collectCalls{0};
    int resetCalls{0};
    std::size_t lastChannelCount{0};
    std::vector<Event> events;
    std::vector<Call> calls;
};

}  // namespace pme::plugin::test
