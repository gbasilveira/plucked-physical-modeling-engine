// SPDX-License-Identifier: MIT
// Covers: A5-R1
// Velocity map and launch: the exact formula, invalid arguments, and contact one sample after the
// launch for every hammer velocity.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "pme/exciters/hammer.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

TEST_CASE("A5-R1 velocity map follows the SPEC-HAM formula at nu = 0.01 0.5 and 1") {
    HammerParams h = pianoHammer();
    h.vMin = 0.3;
    h.vMax = 6.0;
    h.gammaV = 1.5;
    for (const double nu : {0.01, 0.5, 1.0}) {
        double v = 0.0;
        REQUIRE(hammerVelocity(nu, h, v) == Status::Ok);
        const double expected = 0.3 + (6.0 - 0.3) * std::pow(nu, 1.5);  // SPEC-HAM Velocity map
        CHECK(v == doctest::Approx(expected).epsilon(1e-15));
    }
    double v = 0.0;
    REQUIRE(hammerVelocity(1.0, h, v) == Status::Ok);
    CHECK(v == doctest::Approx(6.0).epsilon(1e-15));
    // nu above 1 is clamped (MIDI velocity / 127 never exceeds 1).
    double vClamped = 0.0;
    REQUIRE(hammerVelocity(1.5, h, vClamped) == Status::Ok);
    CHECK(vClamped == v);
}

TEST_CASE("A5-R1 invalid key velocity returns InvalidArgument and leaves the output unchanged") {
    const HammerParams h = pianoHammer();
    for (const double nu :
         {0.0, -0.5, std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        double v = 123.0;
        CHECK(hammerVelocity(nu, h, v) == Status::InvalidArgument);
        CHECK(v == 123.0);
    }
}

TEST_CASE("A5-R1 the hammer touches the string one sample after launch for every v_H") {
    const std::vector<StringModes> modes = unison(1, true);
    REQUIRE(modes[0].k.size() > 10);
    for (const double nu : {0.02, 0.1, 0.3, 0.6, 1.0}) {
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        REQUIRE(note.launch(nu, 0.0) == Status::Ok);
        double vH = 0.0;
        REQUIRE(hammerVelocity(nu, pianoHammer(), vH) == Status::Ok);
        const double gap = vH / kFs;  // SPEC-HAM Velocity map: gap v_H dt
        const std::span<const double> z(note.segment().q(), note.segment().size());
        // At the launch sample the hammer is v_H dt from the string, moving at v_H ...
        CHECK(note.potential().penetration(z, 0) == doctest::Approx(-gap).epsilon(1e-12));
        CHECK(note.headVelocity() == doctest::Approx(vH).epsilon(1e-12));
        // ... one sample later it is on the string (no action latency) ...
        note.step();
        const std::span<const double> z1(note.segment().q(), note.segment().size());
        CHECK(std::abs(note.potential().penetration(z1, 0)) <= 1e-12 * gap);
        // ... and the next sample it penetrates by v_H dt (contact force from here on).
        note.step();
        const std::span<const double> z2(note.segment().q(), note.segment().size());
        CHECK(note.potential().penetration(z2, 0) > 0.0);
        CHECK(note.potential().value(z2) > 0.0);
    }
}

TEST_CASE("A5-R1 launch rejects an invalid key velocity or shift and changes nothing") {
    const std::vector<StringModes> modes = unison(1, true);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    CHECK(note.launch(0.0, 0.0) == Status::InvalidArgument);
    CHECK(note.launch(0.5, std::numeric_limits<double>::quiet_NaN()) == Status::InvalidArgument);
    CHECK_FALSE(note.inFlight());
    CHECK(note.diagnostics().strikes == 0);
}
