// SPDX-License-Identifier: MIT
// D1-R2: the translation table (CLAP and MIDI input to pme::Event).
#include <doctest/doctest.h>

#include "pme/plugin/event_translator.hpp"

using pme::plugin::EventTranslator;

namespace {
template <typename T>
const T& as(const pme::Event& e) {
    return std::get<T>(e.payload);
}
}  // namespace

TEST_CASE("D1-R2 CLAP note events translate with their offsets") {
    EventTranslator t;
    t.begin(512);
    t.noteOn(10, 60, 0.5);
    t.noteOff(20, 60, 0.25);
    t.noteChoke(30, 61);
    const auto ev = t.events();
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].offset == 10);
    CHECK(as<pme::NoteOn>(ev[0]).key == 60);
    CHECK(as<pme::NoteOn>(ev[0]).velocity == 0.5F);
    CHECK(ev[1].offset == 20);
    CHECK(as<pme::NoteOff>(ev[1]).releaseVelocity == 0.25F);
    CHECK(as<pme::NoteOff>(ev[2]).key == 61);
    CHECK(as<pme::NoteOff>(ev[2]).releaseVelocity == 0.0F);
}

TEST_CASE("D1-R2 invalid keys are ignored and zero velocity becomes the smallest velocity") {
    EventTranslator t;
    t.begin(64);
    t.noteOn(0, -1, 1.0);
    t.noteOn(0, 128, 1.0);
    t.noteOff(0, -1, 0.0);
    t.noteOn(1, 0, 0.0);
    t.noteOn(2, 127, 7.0);
    const auto ev = t.events();
    REQUIRE(ev.size() == 2);
    CHECK(as<pme::NoteOn>(ev[0]).velocity == doctest::Approx(1.0F / 127.0F));
    CHECK(as<pme::NoteOn>(ev[1]).velocity == 1.0F);
}

TEST_CASE("D1-R2 MIDI notes: velocity 0 note on is a note off") {
    EventTranslator t;
    t.begin(64);
    t.midi(1, 0x90, 64, 127);
    t.midi(2, 0x91, 64, 0);
    t.midi(3, 0x80, 64, 64);
    const auto ev = t.events();
    REQUIRE(ev.size() == 3);
    CHECK(as<pme::NoteOn>(ev[0]).velocity == 1.0F);
    CHECK(as<pme::NoteOff>(ev[1]).releaseVelocity == 0.0F);
    CHECK(as<pme::NoteOff>(ev[2]).releaseVelocity == doctest::Approx(64.0F / 127.0F));
}

TEST_CASE("D1-R2 MIDI CC64 is continuous so half-pedal works") {
    EventTranslator t;
    t.begin(64);
    t.midi(0, 0xB0, 64, 0);
    t.midi(1, 0xB0, 64, 64);
    t.midi(2, 0xB3, 64, 127);
    const auto ev = t.events();
    REQUIRE(ev.size() == 3);
    CHECK(as<pme::Pedal>(ev[0]).which == pme::PedalKind::Sustain);
    CHECK(as<pme::Pedal>(ev[0]).value == 0.0F);
    CHECK(as<pme::Pedal>(ev[1]).value == doctest::Approx(64.0F / 127.0F));
    CHECK(as<pme::Pedal>(ev[2]).value == 1.0F);
}

TEST_CASE("D1-R2 MIDI CC66 is a switch and CC67 is continuous") {
    EventTranslator t;
    t.begin(64);
    t.midi(0, 0xB0, 66, 63);
    t.midi(1, 0xB0, 66, 64);
    t.midi(2, 0xB0, 67, 127);
    t.midi(3, 0xB0, 67, 32);
    const auto ev = t.events();
    REQUIRE(ev.size() == 4);
    CHECK(as<pme::Pedal>(ev[0]).which == pme::PedalKind::Sostenuto);
    CHECK(as<pme::Pedal>(ev[0]).value == 0.0F);
    CHECK(as<pme::Pedal>(ev[1]).value == 1.0F);
    CHECK(as<pme::Pedal>(ev[2]).which == pme::PedalKind::Soft);
    CHECK(as<pme::Pedal>(ev[2]).value == 1.0F);
    CHECK(as<pme::Pedal>(ev[3]).value == doctest::Approx(32.0F / 127.0F));
}

TEST_CASE("D1-R2 MIDI polyphonic aftertouch is key depth and other MIDI is ignored") {
    EventTranslator t;
    t.begin(64);
    t.midi(0, 0xA0, 60, 127);
    t.midi(1, 0xB0, 1, 100);  // mod wheel
    t.midi(2, 0xE0, 0, 64);   // pitch bend
    t.midi(3, 0xC0, 5, 0);    // program change
    const auto ev = t.events();
    REQUIRE(ev.size() == 1);
    CHECK(as<pme::KeyDepth>(ev[0]).key == 60);
    CHECK(as<pme::KeyDepth>(ev[0]).depth == 1.0F);
}

TEST_CASE("D1-R2 offsets are clamped into the block and sorted stably") {
    EventTranslator t;
    t.begin(8);
    t.param(7, 2001, 1.0);
    t.param(100, 2001, 2.0);  // clamped to 7
    t.param(3, 2001, 3.0);
    t.param(3, 2001, 4.0);
    t.sort();
    const auto ev = t.events();
    REQUIRE(ev.size() == 4);
    CHECK(ev[0].offset == 3);
    CHECK(as<pme::Param>(ev[0]).value == 3.0);
    CHECK(as<pme::Param>(ev[1]).value == 4.0);  // stable
    CHECK(ev[2].offset == 7);
    CHECK(as<pme::Param>(ev[2]).value == 1.0);
    CHECK(ev[3].offset == 7);
}

TEST_CASE("D1-R2 a full buffer drops events and counts them") {
    EventTranslator t;
    t.begin(8);
    for (std::size_t i = 0; i < EventTranslator::kCapacity + 5; ++i) {
        t.param(0, 2001, 0.0);
    }
    CHECK(t.events().size() == EventTranslator::kCapacity);
    CHECK(t.dropped() == 5);
}
