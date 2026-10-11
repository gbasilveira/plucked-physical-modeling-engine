// SPDX-License-Identifier: MIT
// P0.4-R3: compile-function stubs return NotImplemented and leave their out-parameter unchanged;
// Engine::prepare validates its configuration; Status has names.
#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "pme/pme.hpp"

// P0.4-R3 (e): real-time functions are noexcept (ADR-0006).
static_assert(noexcept(std::declval<pme::Engine&>().process(std::declval<const pme::EventList&>(),
                                                            std::declval<pme::AudioOut&>(), 0)));
static_assert(noexcept(std::declval<pme::Engine&>().reset()));
static_assert(
    noexcept(std::declval<pme::Engine&>().popDiagnostic(std::declval<pme::Diagnostic&>())));
static_assert(noexcept(std::declval<pme::Engine&>().setInstrument(
    std::declval<std::shared_ptr<const pme::CompiledInstrument>>())));
static_assert(noexcept(pme::toString(pme::Status::Ok)));
static_assert(noexcept(pme::paramRegistry()));
static_assert(noexcept(pme::findParam(0)));
static_assert(!std::is_copy_constructible_v<pme::Engine>);
static_assert(!std::is_copy_assignable_v<pme::Engine>);
static_assert(std::is_nothrow_move_constructible_v<pme::Engine>);
static_assert(std::is_nothrow_move_assignable_v<pme::Engine>);

namespace {
pme::EngineConfig validConfig() {
    pme::EngineConfig c;
    c.fs = 48000.0;
    c.maxBlock = 256;
    c.subBlock = 32;
    c.channels = 2;
    c.threads = 1;
    c.fMaxStrings = 16000.0;
    return c;
}
}  // namespace

TEST_CASE("P0.4-R3 compile stubs return NotImplemented and leave outputs unchanged") {
    SUBCASE("compileOscillator") {
        pme::OscCoeffs out;
        out.beta = 1.5f;
        out.omegaHat2 = 2.5;
        CHECK(pme::compileOscillator(100.0, 1.0, 48000.0, out) == pme::Status::NotImplemented);
        CHECK(out.beta == 1.5f);
        CHECK(out.omegaHat2 == 2.5);
    }
    SUBCASE("compileString") {
        pme::StringModes out;
        out.tension = 99.0;
        const std::array<double, 2> points{0.1, 0.2};
        CHECK(pme::compileString(pme::StringParams{}, 48000.0, 16000.0, points, out) ==
              pme::Status::NotImplemented);
        CHECK(out.tension == 99.0);
        CHECK(out.k.empty());
    }
    SUBCASE("compileBridgeTerms") {
        pme::SegmentBridgeTerms out;
        out.tOverL = 7.0;
        CHECK(pme::compileBridgeTerms(pme::StringModes{}, pme::BridgeEnd::At0, out) ==
              pme::Status::NotImplemented);
        CHECK(out.tOverL == 7.0);
        CHECK(out.end == pme::BridgeEnd::AtL);
    }
    SUBCASE("compileBody") {
        pme::BodyModes out;
        out.numModes = 5;
        const std::array<pme::Point, 1> pts{{{0.1, 0.2, 0.0}}};
        CHECK(pme::compileBody(pme::BodyParams{}, pts, pts, out) == pme::Status::NotImplemented);
        CHECK(out.numModes == 5);
    }
    SUBCASE("buildLocalBridge") {
        pme::LocalBridge out;
        out.point = 9;
        CHECK(pme::buildLocalBridge(pme::BodyModes{}, 0, 64, 2000.0, out) ==
              pme::Status::NotImplemented);
        CHECK(out.point == 9);
    }
    SUBCASE("compileInstrument") {
        std::shared_ptr<const pme::CompiledInstrument> out;
        CHECK(pme::compileInstrument(pme::InstrumentDesc{}, validConfig(), pme::BodyModes{}, out) ==
              pme::Status::NotImplemented);
        CHECK(out == nullptr);
    }
}

TEST_CASE("P0.4-R3 Engine::prepare checks the configuration") {
    pme::Engine engine;
    CHECK(engine.prepare(validConfig()) == pme::Status::NotImplemented);
    CHECK(engine.prepare(pme::EngineConfig{}) == pme::Status::InvalidArgument);

    auto bad = [&](auto mutate) {
        pme::EngineConfig c = validConfig();
        mutate(c);
        return engine.prepare(c);
    };
    CHECK(bad([](pme::EngineConfig& c) { c.fs = 0.0; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.fs = -48000.0; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.maxBlock = 0; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.subBlock = 8; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.subBlock = 64; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.channels = 0; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.threads = 0; }) == pme::Status::InvalidArgument);
    CHECK(bad([](pme::EngineConfig& c) { c.subBlock = 16; }) == pme::Status::NotImplemented);
}

TEST_CASE("P0.4-R3 a moved-from Engine can be prepared again") {
    pme::Engine a;
    pme::Engine b(std::move(a));
    // NOLINTNEXTLINE(bugprone-use-after-move): a moved-from Engine is documented as re-preparable
    CHECK(a.prepare(validConfig()) == pme::Status::NotImplemented);
    pme::Engine c;
    c = std::move(b);
    CHECK(c.prepare(validConfig()) == pme::Status::NotImplemented);
}

TEST_CASE("P0.4-R3 toString names every Status") {
    CHECK(std::string_view(pme::toString(pme::Status::Ok)) == "Ok");
    CHECK(std::string_view(pme::toString(pme::Status::NotImplemented)) == "NotImplemented");
    CHECK(std::string_view(pme::toString(pme::Status::InvalidArgument)) == "InvalidArgument");
    CHECK(std::string_view(pme::toString(pme::Status::Rejected)) == "Rejected");
    CHECK(std::string_view(pme::toString(static_cast<pme::Status>(200))) == "Unknown");
}

TEST_CASE("P0.4-R3 description types are default-constructible with SPEC-INST defaults") {
    const pme::InstrumentDesc desc;
    CHECK(desc.tuning.referencePitch == 440.0);
    CHECK(desc.defaults.sympatheticGain == 1.0);
    CHECK(desc.defaults.localBridgeModes == 64);
    const pme::SegmentDesc seg;
    CHECK(seg.longitudinalModes == 8);
    const pme::ExciterParams ex;
    CHECK(std::holds_alternative<pme::HammerParams>(ex.e));
    const pme::BarrierParams barrier;
    CHECK(barrier.kind == pme::BarrierKind::Custom);
    const pme::DamperParams damper;
    CHECK(damper.mode == pme::DamperMode::Diagonal);
    CHECK_FALSE(pme::nonlinearApiDeclared);
}
