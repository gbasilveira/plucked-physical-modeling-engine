// SPDX-License-Identifier: MIT
// D3-R1: the adapter's `run` over the controller layer, driven without lilv by hand-built atom
// sequences and a recording engine. `run` marks itself ScopedRealtimeThread, so the rt-safety
// build aborts on any allocation, lock or sleep inside it.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

#include "lv2_test_support.hpp"

using namespace pme::lv2;
using namespace pme::lv2::test;

namespace {

/// The events of the recording engine's `index`-th block.
std::vector<pme::Event> blockEvents(const RecEngine& e, std::size_t index) {
    const RecEngine::Block& b = e.blocks.at(index);
    return {e.events.begin() + static_cast<std::ptrdiff_t>(b.first),
            e.events.begin() + static_cast<std::ptrdiff_t>(b.first + b.count)};
}

std::size_t countParams(const std::vector<pme::Event>& events) {
    return static_cast<std::size_t>(std::ranges::count_if(
        events, [](const pme::Event& e) { return std::holds_alternative<pme::Param>(e.payload); }));
}

}  // namespace

TEST_CASE("D3-R1 MIDI and patch:Set events arrive once with exact offsets, values and types") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    // Let the first-run resend of the control ports pass, then look at one clean block.
    rig.emptyInput();
    rig.run();
    rig.engine->clearRecording();

    const pme::ParamInfo& s0 = *pme::findParam(5001);  // structural
    rig.begin()
        .midi(100, 0x90, 60, 100)  // note on, key 60, velocity 100
        .midi(200, 0xB0, 64, 127)  // CC64: sustain
        .setDouble(300, rig.structuralUrid(5001), 0.75)
        .midi(400, 0x80, 60, 64)   // note off
        .midi(450, 0x90, 62, 0)    // note on with velocity 0 = note off
        .midi(500, 0xB0, 66, 100)  // CC66 sostenuto
        .end();
    rig.run();

    REQUIRE(rig.engine->blocks.size() == 1);
    const std::vector<pme::Event> ev = blockEvents(*rig.engine, 0);
    REQUIRE(ev.size() == 6);
    CHECK(std::ranges::is_sorted(ev, {}, [](const pme::Event& e) { return e.offset; }));

    CHECK(ev[0].offset == 100);
    const auto* on = std::get_if<pme::NoteOn>(&ev[0].payload);
    REQUIRE(on != nullptr);
    CHECK(on->key == 60);
    CHECK(on->velocity == doctest::Approx(100.0F / 127.0F));

    CHECK(ev[1].offset == 200);
    const auto* sustain = std::get_if<pme::Pedal>(&ev[1].payload);
    REQUIRE(sustain != nullptr);
    CHECK(sustain->which == pme::PedalKind::Sustain);
    CHECK(sustain->value == doctest::Approx(1.0F));

    CHECK(ev[2].offset == 300);
    const auto* param = std::get_if<pme::Param>(&ev[2].payload);
    REQUIRE(param != nullptr);
    CHECK(param->id == s0.id);
    CHECK(param->value == doctest::Approx(0.75));

    CHECK(ev[3].offset == 400);
    const auto* off = std::get_if<pme::NoteOff>(&ev[3].payload);
    REQUIRE(off != nullptr);
    CHECK(off->key == 60);

    CHECK(ev[4].offset == 450);
    const auto* off2 = std::get_if<pme::NoteOff>(&ev[4].payload);
    REQUIRE(off2 != nullptr);
    CHECK(off2->key == 62);
    CHECK(off2->releaseVelocity == doctest::Approx(0.0F));

    CHECK(ev[5].offset == 500);
    const auto* sostenuto = std::get_if<pme::Pedal>(&ev[5].payload);
    REQUIRE(sostenuto != nullptr);
    CHECK(sostenuto->which == pme::PedalKind::Sostenuto);
    CHECK(sostenuto->value == doctest::Approx(1.0F));
}

TEST_CASE("D3-R1 a changed control port arrives at offset 0 and an unchanged one does not") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.emptyInput();
    rig.run();
    rig.engine->clearRecording();

    rig.emptyInput();
    rig.run();  // nothing changed
    REQUIRE(rig.engine->blocks.size() == 1);
    CHECK(countParams(blockEvents(*rig.engine, 0)) == 0);

    constexpr std::size_t kGain = 3;  // output.gain, id 2001
    REQUIRE(kControlPortParams[kGain] == 2001);
    rig.controls[kGain] = -6.0F;
    rig.emptyInput();
    rig.run();
    REQUIRE(rig.engine->blocks.size() == 2);
    const std::vector<pme::Event> changed = blockEvents(*rig.engine, 1);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].offset == 0);
    const auto* p = std::get_if<pme::Param>(&changed[0].payload);
    REQUIRE(p != nullptr);
    CHECK(p->id == 2001);
    CHECK(p->value == doctest::Approx(-6.0));

    rig.emptyInput();
    rig.run();  // the same value again: no event
    CHECK(countParams(blockEvents(*rig.engine, 2)) == 0);
}

TEST_CASE("D3-R1 every port value is re-sent after activate and after restore") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.emptyInput();
    rig.run();
    {
        const std::vector<pme::Event> first = blockEvents(*rig.engine, 0);
        REQUIRE(countParams(first) == kControlPortCount);
        for (std::size_t i = 0; i < kControlPortCount; ++i) {
            const auto* p = std::get_if<pme::Param>(&first[i].payload);
            REQUIRE(p != nullptr);
            CHECK(first[i].offset == 0);
            CHECK(p->id == kControlPortParams[i]);  // port order
        }
    }

    // Second activation (deactivate, activate): the first run sends every port again.
    rig.plugin->deactivate();
    REQUIRE(rig.plugin->activate());
    rig.engine->clearRecording();
    rig.emptyInput();
    rig.run();
    CHECK(countParams(blockEvents(*rig.engine, 0)) == kControlPortCount);

    // After restore the next run re-sends every port value: the port values win over the JSON.
    StateStore store;
    store.put(
        rig.plugin->urids().stateKey, rig.plugin->urids().atomString,
        R"({"instrument":"","interfaces_version":2,"params":{"2001":-12.5},"plugin_version":"0","state_schema_version":1})");
    REQUIRE(rig.plugin->restore(StateStore::retrieve, &store) == LV2_STATE_SUCCESS);
    rig.engine->clearRecording();
    rig.emptyInput();
    rig.run();
    const std::vector<pme::Event> after = blockEvents(*rig.engine, 0);
    CHECK(countParams(after) >= kControlPortCount);
    // The gain port still holds its default, so the last gain event is the default, not -12.5.
    double lastGain = 1000.0;
    for (const pme::Event& e : after) {
        if (const auto* p = std::get_if<pme::Param>(&e.payload); p != nullptr && p->id == 2001) {
            lastGain = p->value;
        }
    }
    CHECK(lastGain == doctest::Approx(pme::findParam(2001)->defaultValue));
}

TEST_CASE("D3-R1 events beyond the block and out-of-order input are clamped and sorted") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.emptyInput();
    rig.run();
    rig.engine->clearRecording();
    rig.begin().midi(-5, 0x90, 60, 100).midi(10, 0x80, 60, 0).midi(9999, 0xB0, 64, 127).end();
    rig.run(256);
    const std::vector<pme::Event> ev = blockEvents(*rig.engine, 0);
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].offset == 0);
    CHECK(ev[1].offset == 10);
    CHECK(ev[2].offset == 255);
    CHECK(rig.engine->blocks[0].frames == 256);
}

TEST_CASE("D3-R1 the stub engine writes silence and activation failure is reported") {
    {
        // The real (stub) engine: prepare returns NotImplemented, run writes silence.
        UridMap urids;
        Plugin plugin(std::make_unique<pme::plugin::RealEngineAdapter>(), 48000.0, urids.map(),
                      nullptr, nullptr, kRigBlock);
        std::vector<float> left(kRigBlock, 1.0F);
        std::vector<float> right(kRigBlock, 1.0F);
        plugin.connectPort(kPortOutLeft, left.data());
        plugin.connectPort(kPortOutRight, right.data());
        CHECK(plugin.activate());
        plugin.run(kRigBlock);
        CHECK(std::ranges::all_of(left, [](float v) { return v == 0.0F; }));
        CHECK(std::ranges::all_of(right, [](float v) { return v == 0.0F; }));
        plugin.deactivate();
    }
    {
        Rig rig(true, pme::Status::NotImplemented);
        CHECK(rig.plugin->activate());
        rig.emptyInput();
        rig.run();
        CHECK(std::ranges::all_of(rig.left, [](float v) { return v == 0.0F; }));
    }
    {
        Rig rig(true, pme::Status::InvalidArgument);
        CHECK_FALSE(rig.plugin->activate());
    }
}

TEST_CASE("D3-R1 activation passes the host sample rate and maximum block length") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    CHECK(rig.engine->lastConfig.fs == doctest::Approx(48000.0));
    CHECK(rig.engine->lastConfig.maxBlock == static_cast<int>(kRigBlock));
    CHECK(rig.engine->lastConfig.channels == 2);

    UridMap urids;
    Plugin defaulted(std::make_unique<RecEngine>(), 44100.0, urids.map(), nullptr, nullptr, 0);
    CHECK(defaulted.maxBlock() == kDefaultMaxBlock);
}

TEST_CASE("D3-R1 a block larger than the maximum is rendered in chunks with re-based offsets") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    std::vector<float> big(std::size_t{kRigBlock} * 2, 1.0F);
    rig.plugin->connectPort(kPortOutLeft, big.data());
    rig.plugin->connectPort(kPortOutRight, big.data());
    rig.begin().midi(700, 0x90, 60, 100).end();
    rig.run(kRigBlock * 2);
    REQUIRE(rig.engine->blocks.size() == 2);
    CHECK(rig.engine->blocks[0].frames == static_cast<int>(kRigBlock));
    const std::vector<pme::Event> second = blockEvents(*rig.engine, 1);
    REQUIRE(!second.empty());
    CHECK(second.back().offset == 700 - static_cast<int>(kRigBlock));
}
