// SPDX-License-Identifier: MIT
// P0.4-T2 and P0.4-R3: the stub Engine processes silence without allocating.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "pme/pme.hpp"

#ifdef PME_TEST_HAVE_RTCHECK
#include "pme/rtcheck/rtcheck.hpp"
#endif
#ifdef PME_TEST_COUNT_ALLOCATIONS
#include "alloc_counter.hpp"
#endif

namespace {

constexpr int kMaxBlock = 256;
constexpr int kChannels = 2;

pme::EngineConfig config() {
    pme::EngineConfig c;
    c.fs = 48000.0;
    c.maxBlock = kMaxBlock;
    c.subBlock = 32;
    c.channels = kChannels;
    c.threads = 1;
    c.fMaxStrings = 16000.0;
    return c;
}

// One event of each of the seven types, sorted by offset.
std::array<pme::Event, 7> mixedEvents() {
    return {{
        {0, pme::NoteOn{60, 0.8f}},
        {1, pme::NoteOff{60, 0.3f}},
        {2, pme::KeyDepth{61, 0.5f}},
        {3, pme::Pedal{pme::PedalKind::Sustain, 1.0f}},
        {4, pme::Pluck{0, 0.1, 0.0, 1.0f}},
        {5, pme::Bend{0, 100.0}},
        {6, pme::Param{1001, 0.5}},
    }};
}

// Events are valid only if their offsets lie inside the block: offer them when the block holds all.
pme::EventList eventsFor(const std::array<pme::Event, 7>& events, int frames) {
    return frames > 6 ? pme::EventList(events.data(), events.size()) : pme::EventList{};
}

struct Buffers {
    std::vector<float> storage;
    std::array<float*, kChannels> pointers{};
    explicit Buffers(int frames) : storage(static_cast<std::size_t>(frames) * kChannels, 0.25f) {
        for (int c = 0; c < kChannels; ++c) {
            pointers[static_cast<std::size_t>(c)] =
                storage.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(frames);
        }
    }
    void fillNonZero() { std::fill(storage.begin(), storage.end(), 0.25f); }
    [[nodiscard]] bool firstFramesAreZero(int frames) const {
        const int total = static_cast<int>(storage.size() / kChannels);
        for (int c = 0; c < kChannels; ++c) {
            for (int i = 0; i < total; ++i) {
                const float v = pointers[static_cast<std::size_t>(c)][i];
                if (i < frames ? v != 0.0f : v != 0.25f) {
                    return false;
                }
            }
        }
        return true;
    }
};

// Runs the audio-thread calls of the stub; returns true if every output sample was exactly zero.
bool runBlocks() {
    pme::Engine engine;
    const pme::Status status = engine.prepare(config());
    CHECK(status == pme::Status::NotImplemented);

    const auto events = mixedEvents();
    const std::array<int, 6> sizes{1, 16, 32, 64, 128, kMaxBlock};
    Buffers buffers(kMaxBlock);
    bool allZero = true;
    for (int round = 0; round < 20; ++round) {
        for (const int frames : sizes) {
            buffers.fillNonZero();
            pme::AudioOut out{std::span<float* const>(buffers.pointers)};
            engine.process(eventsFor(events, frames), out, frames);
            allZero = allZero && buffers.firstFramesAreZero(frames);
        }
    }
    engine.reset();
    pme::Diagnostic d;
    CHECK_FALSE(engine.popDiagnostic(d));
    return allZero;
}

}  // namespace

#ifdef PME_TEST_HAVE_RTCHECK
TEST_CASE("P0.4-T2 stub Engine processes silence in the rt-safety build without allocating") {
    pme::Engine engine;
    CHECK(engine.prepare(config()) == pme::Status::NotImplemented);  // non-audio thread
    const auto events = mixedEvents();
    const std::array<int, 6> sizes{1, 16, 32, 64, 128, kMaxBlock};
    Buffers buffers(kMaxBlock);
    bool allZero = true;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;  // any allocation or blocking call aborts
        for (int round = 0; round < 20; ++round) {    // 120 blocks of varied sizes
            for (const int frames : sizes) {
                buffers.fillNonZero();
                pme::AudioOut out{std::span<float* const>(buffers.pointers)};
                engine.process(eventsFor(events, frames), out, frames);
                allZero = allZero && buffers.firstFramesAreZero(frames);
            }
        }
        engine.reset();
        pme::Diagnostic d;
        CHECK_FALSE(engine.popDiagnostic(d));
    }
    // ICR-0001: setInstrument and collectRetired are non-audio calls; they stay outside the
    // real-time scope.
    engine.setInstrument(nullptr);
    engine.collectRetired();
    CHECK(allZero);
}
#endif

#ifdef PME_TEST_COUNT_ALLOCATIONS
TEST_CASE("P0.4-R3 stub Engine process makes no allocations") {
    pme::Engine engine;
    CHECK(engine.prepare(config()) == pme::Status::NotImplemented);
    const auto events = mixedEvents();
    const std::array<int, 6> sizes{1, 16, 32, 64, 128, kMaxBlock};
    Buffers buffers(kMaxBlock);
    pme::AudioOut out{std::span<float* const>(buffers.pointers)};

    pme_test::startCountingAllocations();
    for (int round = 0; round < 20; ++round) {
        for (const int frames : sizes) {
            engine.process(eventsFor(events, frames), out, frames);
        }
    }
    engine.reset();
    pme::Diagnostic d;
    const bool popped = engine.popDiagnostic(d);
    engine.setInstrument(nullptr);
    engine.collectRetired();
    const long allocations = pme_test::stopCountingAllocations();
    CHECK_FALSE(popped);
    CHECK(allocations == 0);
}
#endif

TEST_CASE("P0.4-R3 stub Engine process writes exact zeros") { CHECK(runBlocks()); }

TEST_CASE("P0.4-R3 stub Engine process zeroes the output even without a successful prepare") {
    pme::Engine unprepared;
    Buffers buffers(64);
    pme::AudioOut out{std::span<float* const>(buffers.pointers)};
    unprepared.process(pme::EventList{}, out, 64);
    CHECK(buffers.firstFramesAreZero(64));

    // A failed prepare, a null channel pointer and a non-positive frame count do not misbehave.
    CHECK(unprepared.prepare(pme::EngineConfig{}) == pme::Status::InvalidArgument);
    buffers.fillNonZero();
    std::array<float*, 2> withNull{buffers.pointers[0], nullptr};
    pme::AudioOut partial{std::span<float* const>(withNull)};
    unprepared.process(pme::EventList{}, partial, 64);
    CHECK(buffers.pointers[0][63] == 0.0f);
    CHECK(buffers.pointers[1][63] == 0.25f);  // untouched
    unprepared.process(pme::EventList{}, partial, 0);
    unprepared.process(pme::EventList{}, partial, -5);
}
