// SPDX-License-Identifier: MIT
// Covers: A5-R5, A5-T3
// Una corda strike weights, felt-zone stiffness and exponent against the SPEC-HAM formulas, the
// upright soft pedal as velocity scaling, and per-sample ramps of the shift.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "pme/exciters/hammer.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

namespace {

double rel(double a, double b) { return std::abs(a - b) / std::max(std::abs(b), 1e-300); }

// SPEC-HAM Una corda: S(s) between s0 and s1 (cubic smoothstep).
double refS(double s, double s0, double s1) {
    const double t = std::min(std::max((s - s0) / (s1 - s0), 0.0), 1.0);
    return 3.0 * t * t - 2.0 * t * t * t;
}

}  // namespace

TEST_CASE("A5-T3 una corda weights and felt-zone values match the SPEC-HAM tables") {
    const std::array<double, 6> shifts{0.0, 0.3, 0.45, 0.6, 0.9, 1.0};
    HammerParams h = pianoHammer();
    for (const double dp : {0.0, 0.2}) {
        UnaCordaParams u;
        u.kappaUC = 0.6;
        u.deltaPUC = dp;
        for (const std::size_t n : {std::size_t{3}, std::size_t{2}, std::size_t{1}}) {
            for (const int leaving : {0, 2, -1}) {
                if (leaving >= static_cast<int>(n) && n > 1) {
                    continue;
                }
                if (n == 1 && leaving != -1) {
                    continue;
                }
                if (n > 1 && leaving == -1) {
                    continue;
                }
                u.leavingString = leaving;
                for (const double s : shifts) {
                    StrikeMap m;
                    REQUIRE(strikeMap(h, u, SoftPedal::UnaCorda, n, s, m) == Status::Ok);
                    for (std::size_t i = 0; i < n; ++i) {
                        const bool leaves = n > 1 && static_cast<int>(i) == leaving;
                        const double w = leaves ? 1.0 - refS(s, 0.3, 0.9) : 1.0;
                        CHECK(rel(m.weight[i], w) <= 1e-12);
                        CHECK(rel(m.stiffness[i], h.K * (1.0 - s * (1.0 - 0.6))) <= 1e-12);
                        CHECK(rel(m.exponent[i], h.p + s * dp) <= 1e-12);
                    }
                }
            }
        }
    }
}

TEST_CASE("A5-T3 the leaving string is string 0 or 2 of a trichord and none for a single string") {
    HammerParams h = pianoHammer();
    UnaCordaParams u;
    StrikeMap m;
    u.leavingString = 0;
    REQUIRE(strikeMap(h, u, SoftPedal::UnaCorda, 3, 1.0, m) == Status::Ok);
    CHECK(m.weight[0] == 0.0);
    CHECK(m.weight[1] == 1.0);
    CHECK(m.weight[2] == 1.0);
    u.leavingString = 2;
    REQUIRE(strikeMap(h, u, SoftPedal::UnaCorda, 3, 1.0, m) == Status::Ok);
    CHECK(m.weight[0] == 1.0);
    CHECK(m.weight[2] == 0.0);
    u.leavingString = -1;
    REQUIRE(strikeMap(h, u, SoftPedal::UnaCorda, 1, 1.0, m) == Status::Ok);
    CHECK(m.weight[0] == 1.0);
    // A leaving string beyond the note's strings is rejected and leaves the map unchanged.
    u.leavingString = 2;
    StrikeMap before = m;
    CHECK(strikeMap(h, u, SoftPedal::UnaCorda, 2, 0.5, m) == Status::InvalidArgument);
    CHECK(m.weight[0] == before.weight[0]);
    CHECK(strikeMap(h, u, SoftPedal::UnaCorda, 4, 0.5, m) == Status::InvalidArgument);
    CHECK(strikeMap(h, u, SoftPedal::UnaCorda, 3, std::numeric_limits<double>::quiet_NaN(), m) ==
          Status::InvalidArgument);
}

TEST_CASE("A5-T3 the upright soft pedal scales v_H by 1 - s rho_up and leaves the strike unchanged") {
    HammerParams h = pianoHammer();
    UnaCordaParams u;
    u.rhoUpright = 0.3;
    for (const double s : {0.0, 0.3, 0.45, 0.6, 0.9, 1.0}) {
        double plain = 0.0;
        double pedal = 0.0;
        REQUIRE(launchVelocity(0.7, s, h, u, SoftPedal::Upright, pedal) == Status::Ok);
        REQUIRE(hammerVelocity(0.7, h, plain) == Status::Ok);
        CHECK(rel(pedal, plain * (1.0 - s * 0.3)) <= 1e-12);
        StrikeMap m;
        REQUIRE(strikeMap(h, u, SoftPedal::Upright, 3, s, m) == Status::Ok);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(m.weight[i] == 1.0);
            CHECK(m.stiffness[i] == h.K);
            CHECK(m.exponent[i] == h.p);
        }
        // The grand's una corda leaves the velocity alone.
        double grand = 0.0;
        REQUIRE(launchVelocity(0.7, s, h, u, SoftPedal::UnaCorda, grand) == Status::Ok);
        CHECK(grand == plain);
    }
}

TEST_CASE("A5-R5 the smoothstep is the cubic 3t^2 - 2t^3 between s0 and s1") {
    CHECK(smoothstep(0.0, 0.3, 0.9) == 0.0);
    CHECK(smoothstep(0.3, 0.3, 0.9) == 0.0);
    CHECK(smoothstep(0.9, 0.3, 0.9) == 1.0);
    CHECK(smoothstep(1.0, 0.3, 0.9) == 1.0);
    CHECK(smoothstep(0.6, 0.3, 0.9) == doctest::Approx(0.5).epsilon(1e-15));
    CHECK(smoothstep(0.45, 0.3, 0.9) == doctest::Approx(0.15625).epsilon(1e-15));
}

TEST_CASE("A5-R5 the strike map follows the shift per sample and a string with weight zero is unloaded") {
    const std::vector<StringModes> modes = unison(3, true);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    double last = 2.0;
    for (int n = 0; n <= 100; ++n) {
        const double s = static_cast<double>(n) / 100.0;  // a ramp of the shift
        REQUIRE(note.setShift(s) == Status::Ok);
        const double w = note.potential().map().weight[0];
        CHECK(w <= last);  // the leaving string is released monotonically
        last = w;
        CHECK(rel(note.potential().map().stiffness[1], pianoHammer().K * (1.0 - s * 0.4)) <= 1e-12);
    }
    CHECK(note.potential().map().weight[0] == 0.0);
    // At s = 1 string 0 is not struck: only its (bridge-coupled) modes remain, and the hammer
    // loads strings 1 and 2 alone.
    REQUIRE(note.launch(0.6, 1.0) == Status::Ok);
    for (int n = 0; n < 300; ++n) {
        note.step();
    }
    double e0 = 0.0;
    for (std::size_t i = 0; i < note.modeCount(0); ++i) {
        e0 += std::abs(note.segment().q()[note.modeOffset(0) + i]);
    }
    CHECK(e0 == 0.0);  // no contact force reached the leaving string (no bridge in this rig)
    double e1 = 0.0;
    for (std::size_t i = 0; i < note.modeCount(1); ++i) {
        e1 += std::abs(note.segment().q()[note.modeOffset(1) + i]);
    }
    CHECK(e1 > 0.0);
}
