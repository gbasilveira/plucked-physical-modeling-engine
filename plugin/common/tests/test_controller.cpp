// SPDX-License-Identifier: MIT
// D1-R1, D1-R3, D1-R4, D1-R5, D1-R7: the controller with a recording fake engine and with the stub
// engine. Audio-path calls run under ScopedRealtimeThread (the trap aborts in the rt-safety build).
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "fake_engine.hpp"
#include "pme/plugin/controller.hpp"
#include "pme/rtcheck/rtcheck.hpp"

using pme::plugin::Controller;
using pme::plugin::RenderMode;
using pme::plugin::test::FakeEngine;

namespace {

struct Rig {
    explicit Rig(pme::Status prepare = pme::Status::Ok) {
        auto fake = std::make_unique<FakeEngine>(prepare);
        engine = fake.get();
        controller = std::make_unique<Controller>(std::move(fake));
    }
    FakeEngine* engine{nullptr};
    std::unique_ptr<Controller> controller;
};

/// One host block through the audio path.
void block(Controller& c, std::uint32_t frames, float* const* channels, std::size_t count,
           const std::vector<pme::Event>& input = {}) {
    const pme::rtcheck::ScopedRealtimeThread realtime;
    c.beginBlock(frames);
    for (const pme::Event& e : input) {
        if (const auto* on = std::get_if<pme::NoteOn>(&e.payload)) {
            c.events().noteOn(static_cast<std::uint32_t>(e.offset), on->key, on->velocity);
        } else if (const auto* off = std::get_if<pme::NoteOff>(&e.payload)) {
            c.events().noteOff(static_cast<std::uint32_t>(e.offset), off->key,
                               off->releaseVelocity);
        } else if (const auto* p = std::get_if<pme::Pedal>(&e.payload)) {
            c.events().midi(static_cast<std::uint32_t>(e.offset), 0xB0, 64,
                            static_cast<std::uint8_t>(p->value * 127.0F));
        }
    }
    c.render(frames, std::span<float* const>(channels, count));
}

}  // namespace

TEST_CASE("D1-R1 activation prepares the engine with the selected channel count") {
    Rig r;
    CHECK(r.controller->channels() == 2);
    REQUIRE(r.controller->setChannels(8));
    CHECK_FALSE(r.controller->setChannels(9));
    CHECK_FALSE(r.controller->setChannels(0));
    REQUIRE((r.controller->activate(44100.0, 512) == pme::Status::Ok));
    CHECK(r.engine->lastConfig.channels == 8);
    CHECK(r.engine->lastConfig.fs == 44100.0);
    CHECK(r.engine->lastConfig.maxBlock == 512);
    CHECK(r.engine->lastConfig.subBlock == 32);
    CHECK(r.engine->lastConfig.fMaxStrings == 16000.0);
    CHECK(r.engine->lastConfig.threads == r.controller->threadsSetting());
    CHECK(r.controller->engineAvailable());
}

TEST_CASE("D1-R1 the stub engine's NotImplemented is an unavailable engine, not a failure") {
    pme::plugin::Controller c(std::make_unique<pme::plugin::RealEngineAdapter>());
    REQUIRE((c.activate(48000.0, 256) == pme::Status::Ok));
    CHECK(c.active());
    CHECK_FALSE(c.engineAvailable());
    std::array<float, 256> l{};
    std::array<float, 256> r{};
    l.fill(1.0F);
    r.fill(1.0F);
    std::array<float*, 2> ch{l.data(), r.data()};
    block(c, 256, ch.data(), ch.size());
    for (std::size_t i = 0; i < 256; ++i) {
        CHECK(l[i] == 0.0F);
        CHECK(r[i] == 0.0F);
    }
}

TEST_CASE("D1-R1 InvalidArgument from the engine fails the activation") {
    Rig r(pme::Status::InvalidArgument);
    CHECK((r.controller->activate(48000.0, 256) == pme::Status::InvalidArgument));
    CHECK_FALSE(r.controller->active());
    pme::plugin::Controller stub(std::make_unique<pme::plugin::RealEngineAdapter>());
    CHECK((stub.activate(0.0, 256) == pme::Status::InvalidArgument));  // invalid sample rate
    CHECK_FALSE(stub.active());
}

TEST_CASE("D1-R6 the thread setting reaches the engine only when the host has no pool") {
    struct Pool final : pme::plugin::TaskRunner::HostPool {
        bool requestExec(std::uint32_t) noexcept override { return true; }
    };
    Rig r;
    r.controller->setThreadsSetting(100);
    CHECK(r.controller->threadsSetting() == 8);
    r.controller->setThreadsSetting(3);
    REQUIRE((r.controller->activate(48000.0, 256) == pme::Status::Ok));
    CHECK(r.engine->lastConfig.threads == 3);
    Pool pool;
    r.controller->setHostPool(&pool);
    REQUIRE((r.controller->activate(48000.0, 256) == pme::Status::Ok));
    CHECK(r.engine->lastConfig.threads == 1);
}

TEST_CASE("D1-R7 render mode is recorded and changes nothing the engine receives") {
    std::vector<pme::Event> input;
    input.push_back({5, pme::NoteOn{60, 0.5F}});
    input.push_back({9, pme::NoteOff{60, 0.0F}});
    std::array<std::vector<pme::Event>, 2> received;
    std::array<pme::EngineConfig, 2> configs;
    const std::array<RenderMode, 2> modes{RenderMode::Realtime, RenderMode::Offline};
    for (std::size_t m = 0; m < 2; ++m) {
        Rig r;
        r.controller->setRenderMode(modes[m]);
        CHECK(r.controller->renderMode() == modes[m]);
        REQUIRE((r.controller->activate(48000.0, 64) == pme::Status::Ok));
        std::array<float, 64> a{};
        std::array<float, 64> b{};
        std::array<float*, 2> ch{a.data(), b.data()};
        block(*r.controller, 64, ch.data(), 2, input);
        block(*r.controller, 64, ch.data(), 2, input);
        received[m] = r.engine->events;
        configs[m] = r.engine->lastConfig;
        CHECK(r.engine->calls.size() == 2);
        CHECK(r.engine->calls[0].frames == 64);
        CHECK_FALSE(configs[m].referenceMode);  // never touched by the render mode
    }
    REQUIRE(received[0].size() == received[1].size());
    for (std::size_t i = 0; i < received[0].size(); ++i) {
        CHECK(received[0][i].offset == received[1][i].offset);
        CHECK(received[0][i].payload.index() == received[1][i].payload.index());
    }
    CHECK(configs[0].maxBlock == configs[1].maxBlock);
}

TEST_CASE("D1-R5 tail is infinite while a note is held or the sustain pedal is down") {
    Rig r;
    REQUIRE((r.controller->activate(48000.0, 64) == pme::Status::Ok));
    std::array<float, 64> a{};
    std::array<float, 64> b{};
    std::array<float*, 2> ch{a.data(), b.data()};
    constexpr std::uint32_t infinite = std::numeric_limits<std::uint32_t>::max();
    CHECK(r.controller->tailSamples() == 0);
    block(*r.controller, 64, ch.data(), 2, {{0, pme::NoteOn{60, 1.0F}}});
    CHECK(r.controller->tailSamples() == infinite);
    block(*r.controller, 64, ch.data(), 2, {{0, pme::NoteOff{60, 0.0F}}});
    CHECK(r.controller->tailSamples() == 0);
    block(*r.controller, 64, ch.data(), 2, {{0, pme::Pedal{pme::PedalKind::Sustain, 0.5F}}});
    CHECK(r.controller->tailSamples() == infinite);
    block(*r.controller, 64, ch.data(), 2, {{0, pme::Pedal{pme::PedalKind::Sustain, 0.0F}}});
    CHECK(r.controller->tailSamples() == 0);
    block(*r.controller, 64, ch.data(), 2, {{0, pme::NoteOn{61, 1.0F}}});
    CHECK(r.controller->tailSamples() == infinite);
    r.controller->reset();
    CHECK(r.controller->tailSamples() == 0);
    CHECK(r.engine->resetCalls == 1);
    CHECK(Controller::voiceCount() == 128);
}

TEST_CASE("D1-R3 host parameter values are clamped, stored and delivered as Param events") {
    Rig r;
    REQUIRE((r.controller->activate(48000.0, 64) == pme::Status::Ok));
    std::array<float, 64> a{};
    std::array<float, 64> b{};
    std::array<float*, 2> ch{a.data(), b.data()};
    {
        const pme::rtcheck::ScopedRealtimeThread realtime;
        r.controller->beginBlock(64);
        r.controller->paramEvent(3, 2001, 1000.0);  // clamped to 24 dB
        r.controller->paramEvent(4, 424242, 1.0);   // unknown id: ignored
        r.controller->render(64, ch);
    }
    REQUIRE(r.engine->events.size() == 1);
    CHECK(r.engine->events[0].offset == 3);
    CHECK(std::get<pme::Param>(r.engine->events[0].payload).value == 24.0);
    CHECK(r.controller->params().value(r.controller->params().indexOf(2001)) == 24.0);
}

TEST_CASE("D1-R4 applied state reaches the engine at the start of the next block") {
    Rig r;
    REQUIRE((r.controller->activate(48000.0, 64) == pme::Status::Ok));
    pme::plugin::StateData s;
    s.instrument = "x.json";
    s.params = {{2001, -12.0}, {123456, 1.0}, {4001, 99.0}};  // unknown id; out of range
    const auto report = r.controller->applyState(s);
    CHECK(report.applied == 2);
    CHECK(report.unknown == 1);
    CHECK(r.controller->instrumentReference() == "x.json");
    CHECK(r.controller->params().value(r.controller->params().indexOf(4001)) == 0.05);  // clamped
    std::array<float, 64> a{};
    std::array<float, 64> b{};
    std::array<float*, 2> ch{a.data(), b.data()};
    block(*r.controller, 64, ch.data(), 2);
    REQUIRE(r.engine->events.size() == 2);
    CHECK(r.engine->events[0].offset == 0);
    CHECK(r.engine->events[1].offset == 0);
    // Loading an empty state returns every value to its default.
    (void)r.controller->applyState(pme::plugin::StateData{});
    CHECK(r.controller->params().value(r.controller->params().indexOf(2001)) == 0.0);
    const auto captured = r.controller->captureState();
    CHECK(captured.params.empty());
}

TEST_CASE("D1-R1 a host block larger than the activated maximum is split with re-based offsets") {
    Rig r;
    REQUIRE((r.controller->activate(48000.0, 100) == pme::Status::Ok));
    std::array<float, 250> a{};
    std::array<float, 250> b{};
    std::array<float*, 2> ch{a.data(), b.data()};
    block(
        *r.controller, 250, ch.data(), 2,
        {{5, pme::NoteOn{60, 1.0F}}, {105, pme::NoteOn{61, 1.0F}}, {249, pme::NoteOff{60, 0.0F}}});
    REQUIRE(r.engine->calls.size() == 3);
    CHECK(r.engine->calls[0].frames == 100);
    CHECK(r.engine->calls[1].frames == 100);
    CHECK(r.engine->calls[2].frames == 50);
    REQUIRE(r.engine->events.size() == 3);
    CHECK(r.engine->events[0].offset == 5);
    CHECK(r.engine->events[1].offset == 5);   // 105 - 100
    CHECK(r.engine->events[2].offset == 49);  // 249 - 200
}
