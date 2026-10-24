// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <type_traits>
#include <variant>

#include "pme/events.hpp"

// P0.4-R5: event types cross lock-free SPSC queues, so they must be trivially copyable.
static_assert(std::is_trivially_copyable_v<pme::NoteOn>);
static_assert(std::is_trivially_copyable_v<pme::NoteOff>);
static_assert(std::is_trivially_copyable_v<pme::KeyDepth>);
static_assert(std::is_trivially_copyable_v<pme::Pedal>);
static_assert(std::is_trivially_copyable_v<pme::Pluck>);
static_assert(std::is_trivially_copyable_v<pme::Bend>);
static_assert(std::is_trivially_copyable_v<pme::Param>);
static_assert(std::is_trivially_copyable_v<pme::Event>);
static_assert(std::is_trivially_destructible_v<pme::Event>);
static_assert(std::variant_size_v<decltype(pme::Event::payload)> == 7);

TEST_CASE("P0.4-R5 events carry the SPEC-API fields and a sample offset") {
    pme::Event e;
    e.offset = 17;
    e.payload = pme::Pluck{3, 0.12, 0.5, 0.75f};
    REQUIRE(std::holds_alternative<pme::Pluck>(e.payload));
    const auto& p = std::get<pme::Pluck>(e.payload);
    CHECK(e.offset == 17);
    CHECK(p.segment == 3);
    CHECK(p.position == doctest::Approx(0.12));
    CHECK(p.angle == doctest::Approx(0.5));
    CHECK(p.strength == doctest::Approx(0.75f));

    e.payload = pme::NoteOn{60, 0.8f};
    CHECK(std::get<pme::NoteOn>(e.payload).key == 60);
    e.payload = pme::NoteOff{60, 0.25f};
    CHECK(std::get<pme::NoteOff>(e.payload).releaseVelocity == doctest::Approx(0.25f));
    e.payload = pme::KeyDepth{60, 0.5f};
    CHECK(std::get<pme::KeyDepth>(e.payload).depth == doctest::Approx(0.5f));
    e.payload = pme::Pedal{pme::PedalKind::Sostenuto, 1.0f};
    CHECK(std::get<pme::Pedal>(e.payload).which == pme::PedalKind::Sostenuto);
    e.payload = pme::Bend{1, 200.0};
    CHECK(std::get<pme::Bend>(e.payload).cents == doctest::Approx(200.0));
    e.payload = pme::Param{1001, 0.5};
    CHECK(std::get<pme::Param>(e.payload).id == 1001u);
}

TEST_CASE("P0.4-R5 a default event is a NoteOn at offset 0") {
    const pme::Event e;
    CHECK(e.offset == 0);
    CHECK(std::holds_alternative<pme::NoteOn>(e.payload));
}
