// SPDX-License-Identifier: MIT
// D1-T3: automation and note events arrive at the engine at their exact sample offsets, in
// non-decreasing offset order, whole and with the host block split into sub-calls. The `process`
// path runs under ScopedRealtimeThread (the trap aborts in the rt-safety build).
#include <doctest/doctest.h>

#include <vector>

#include "clap_test_support.hpp"

using namespace pme::clap;
using namespace pme::clap::test;

namespace {

constexpr uint32_t kFrames = 512;

/// One host event with an absolute offset in the 512-frame block.
struct Spec {
    enum class Kind { Param, NoteOn, NoteOff, Midi } kind;
    uint32_t offset;
    clap_id id;      // Param
    double value;    // Param value, note velocity
    int key;         // notes
    uint8_t status;  // MIDI
    uint8_t d1;
    uint8_t d2;
};

Spec param(uint32_t offset, clap_id id, double value) {
    return {Spec::Kind::Param, offset, id, value, 0, 0, 0, 0};
}
Spec noteOn(uint32_t offset, int key, double velocity) {
    return {Spec::Kind::NoteOn, offset, 0, velocity, key, 0, 0, 0};
}
Spec noteOff(uint32_t offset, int key, double velocity) {
    return {Spec::Kind::NoteOff, offset, 0, velocity, key, 0, 0, 0};
}
Spec midi(uint32_t offset, uint8_t status, uint8_t d1, uint8_t d2) {
    return {Spec::Kind::Midi, offset, 0, 0.0, 0, status, d1, d2};
}

/// The block of the test: sorted by offset as CLAP hosts deliver it.
std::vector<Spec> scenario() {
    return {
        param(0, 2001, -6.0),  // continuous parameters at 0, 1, 255 (twice), 511
        param(1, 3001, 0.5),     midi(50, 0xB0, 64, 127),   // CC64 down
        noteOn(100, 60, 0.75),   midi(150, 0xB0, 66, 100),  // CC66
        midi(160, 0xB0, 67, 64),                            // CC67
        midi(200, 0xB0, 64, 64),                            // half pedal
        param(255, 2001, -12.0), param(255, 4001, 0.01),    // the same offset as the previous event
        noteOff(300, 60, 0.5),   midi(400, 0x90, 62, 100),  // MIDI note on
        midi(450, 0x80, 62, 0),                             // MIDI note off
        param(511, 2001, 3.0),
    };
}

void feed(InEvents& in, const std::vector<Spec>& specs, uint32_t from, uint32_t count) {
    in.clear();
    for (const Spec& s : specs) {
        if (s.offset < from || s.offset >= from + count) {
            continue;
        }
        const uint32_t local = s.offset - from;
        switch (s.kind) {
            case Spec::Kind::Param:
                in.param(local, s.id, s.value);
                break;
            case Spec::Kind::NoteOn:
                in.note(local, CLAP_EVENT_NOTE_ON, static_cast<int16_t>(s.key), s.value);
                break;
            case Spec::Kind::NoteOff:
                in.note(local, CLAP_EVENT_NOTE_OFF, static_cast<int16_t>(s.key), s.value);
                break;
            case Spec::Kind::Midi:
                in.midi(local, s.status, s.d1, s.d2);
                break;
        }
    }
}

/// Compare what the fake engine recorded (absolute offsets reconstructed from the sub-call start)
/// with the specification, event by event.
void expectDelivered(const pme::plugin::test::FakeEngine& engine, const std::vector<Spec>& specs) {
    std::vector<std::pair<uint32_t, const pme::Event*>> got;
    uint32_t start = 0;
    for (const auto& call : engine.calls) {
        for (std::size_t i = 0; i < call.count; ++i) {
            const pme::Event& e = engine.events[call.first + i];
            REQUIRE(e.offset >= 0);
            REQUIRE(e.offset < call.frames);
            got.emplace_back(start + static_cast<uint32_t>(e.offset), &e);
        }
        start += static_cast<uint32_t>(call.frames);
    }
    REQUIRE(start == kFrames);
    REQUIRE(got.size() == specs.size());  // every event once
    uint32_t previous = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i];
        CHECK(got[i].first == s.offset);
        CHECK(got[i].first >= previous);  // non-decreasing within the block
        previous = got[i].first;
        const pme::Event& e = *got[i].second;
        switch (s.kind) {
            case Spec::Kind::Param: {
                const auto* p = std::get_if<pme::Param>(&e.payload);
                REQUIRE(p != nullptr);
                CHECK(p->id == s.id);
                CHECK(p->value == s.value);  // exact
                break;
            }
            case Spec::Kind::NoteOn: {
                const auto* n = std::get_if<pme::NoteOn>(&e.payload);
                REQUIRE(n != nullptr);
                CHECK(n->key == s.key);
                CHECK(n->velocity == static_cast<float>(s.value));
                break;
            }
            case Spec::Kind::NoteOff: {
                const auto* n = std::get_if<pme::NoteOff>(&e.payload);
                REQUIRE(n != nullptr);
                CHECK(n->key == s.key);
                CHECK(n->releaseVelocity == static_cast<float>(s.value));
                break;
            }
            case Spec::Kind::Midi: {
                if (s.status == 0xB0 && s.d1 == 64) {
                    const auto* n = std::get_if<pme::Pedal>(&e.payload);
                    REQUIRE(n != nullptr);
                    CHECK(n->which == pme::PedalKind::Sustain);
                    CHECK(n->value == static_cast<float>(s.d2) / 127.0F);
                } else if (s.status == 0xB0 && s.d1 == 66) {
                    const auto* n = std::get_if<pme::Pedal>(&e.payload);
                    REQUIRE(n != nullptr);
                    CHECK(n->which == pme::PedalKind::Sostenuto);
                    CHECK(n->value == (s.d2 >= 64 ? 1.0F : 0.0F));
                } else if (s.status == 0xB0 && s.d1 == 67) {
                    const auto* n = std::get_if<pme::Pedal>(&e.payload);
                    REQUIRE(n != nullptr);
                    CHECK(n->which == pme::PedalKind::Soft);
                    CHECK(n->value == static_cast<float>(s.d2) / 127.0F);
                } else if (s.status == 0x90) {
                    const auto* n = std::get_if<pme::NoteOn>(&e.payload);
                    REQUIRE(n != nullptr);
                    CHECK(n->key == s.d1);
                } else {
                    const auto* n = std::get_if<pme::NoteOff>(&e.payload);
                    REQUIRE(n != nullptr);
                    CHECK(n->key == s.d1);
                }
                break;
            }
        }
    }
}

void runSplit(const std::vector<uint32_t>& pieces) {
    const std::vector<Spec> specs = scenario();
    Instance inst;
    REQUIRE(inst.initialised);
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, kFrames));
    REQUIRE(inst.plugin->start_processing(inst.plugin));
    InEvents in;
    OutBuffers out(kFrames);
    uint32_t start = 0;
    for (const uint32_t n : pieces) {
        feed(in, specs, start, n);
        REQUIRE(process(inst.plugin, n, out, in) == CLAP_PROCESS_CONTINUE);
        start += n;
    }
    REQUIRE(start == kFrames);
    inst.plugin->stop_processing(inst.plugin);
    expectDelivered(*inst.engine, specs);
    inst.plugin->deactivate(inst.plugin);
}

}  // namespace

TEST_CASE("D1-T3 automation arrives at the engine at its exact sample offsets") {
    runSplit({kFrames});
}

TEST_CASE("D1-T3 the same events arrive when the host block is split into 1 and 511 frames") {
    runSplit({1, kFrames - 1});
}

TEST_CASE("D1-T3 the same events arrive when the host block is split into 7 and 505 frames") {
    runSplit({7, kFrames - 7});
}

TEST_CASE("D1-T3 the same events arrive when the host block is split into 1 7 and 504 frames") {
    runSplit({1, 7, 504});
}

TEST_CASE("D1-T3 events reach the engine in non-decreasing offset order") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, kFrames));
    InEvents in;
    // Out of order on purpose: the shell sorts (SPEC-EXEC step 2.1 precondition).
    in.param(300, 2001, 1.0);
    in.param(10, 2001, 2.0);
    in.param(300, 4001, 0.01);
    OutBuffers out(kFrames);
    process(inst.plugin, kFrames, out, in);
    REQUIRE(inst.engine->events.size() == 3);
    CHECK(inst.engine->events[0].offset == 10);
    CHECK(inst.engine->events[1].offset == 300);
    CHECK(std::get<pme::Param>(inst.engine->events[1].payload).value == 1.0);  // stable
    CHECK(inst.engine->events[2].offset == 300);
}

TEST_CASE("D1-R2 a CLAP note choke is a note off with release velocity 0") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    InEvents in;
    in.note(4, CLAP_EVENT_NOTE_CHOKE, 72, 1.0);
    in.note(5, CLAP_EVENT_NOTE_ON, -1, 1.0);  // wildcard key: ignored
    OutBuffers out(64);
    process(inst.plugin, 64, out, in);
    REQUIRE(inst.engine->events.size() == 1);
    const auto& off = std::get<pme::NoteOff>(inst.engine->events[0].payload);
    CHECK(off.key == 72);
    CHECK(off.releaseVelocity == 0.0F);
    CHECK(inst.engine->events[0].offset == 4);
}

TEST_CASE("D1-R2 half-pedal: continuous CC64 values map to fractional pedal depth") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    InEvents in;
    for (uint8_t v = 0; v <= 100; v = static_cast<uint8_t>(v + 20)) {
        in.midi(v, 0xB0, 64, v);
    }
    OutBuffers out(128);
    process(inst.plugin, 128, out, in);
    REQUIRE(inst.engine->events.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        const auto& pedal = std::get<pme::Pedal>(inst.engine->events[i].payload);
        CHECK(pedal.value == static_cast<float>(i * 20) / 127.0F);
    }
}
