// SPDX-License-Identifier: MIT
// Covers: A6-T2, A6-R3, A6-R4, A6-R5
// Table-driven key, sustain and sostenuto sequences (expected values written by hand from
// SPEC-DMP and SI-0016), the lift fraction, the one-pole timing and the undamped keys.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "fixtures.hpp"
#include "pme/dampers/controller.hpp"
#include "pme/dampers/lift.hpp"

namespace {

using namespace pme;
using namespace pme::dmp;

enum class Op : std::uint8_t { On, Off, Depth, Sustain, Sostenuto };

struct Expect {
    int key;
    double lKey;
    double lPed;
    double l;
    bool latched;
};

struct Step {
    Op op;
    int key;       // ignored for the pedals
    double value;  // NoteOff: release velocity; Depth, Sustain, Sostenuto: depth
    std::vector<Expect> expect;
};

struct Sequence {
    const char* name;
    std::vector<int> undamped;  // keys without a damper
    std::vector<Step> steps;
};

constexpr int kA = 60;
constexpr int kB = 64;
constexpr int kTop = 100;

// Expected values are written by hand from SPEC-DMP "Lift fraction" and "Sostenuto" (SI-0016)
// with P0 = 0.35, P1 = 0.65.
const std::vector<Sequence>& table() {
    static const std::vector<Sequence> t{
        {"key alone",
         {},
         {{Op::On, kA, 0.0, {{kA, 1.0, 0.0, 1.0, false}, {kB, 0.0, 0.0, 0.0, false}}},
          {Op::Off, kA, 0.5, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"sustain alone",
         {},
         {{Op::Sustain, 0, 1.0, {{kA, 0.0, 1.0, 1.0, false}, {kB, 0.0, 1.0, 1.0, false}}},
          {Op::Sustain, 0, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"half pedal at P = 0.35", {}, {{Op::Sustain, 0, 0.35, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"half pedal at P = 0.5", {}, {{Op::Sustain, 0, 0.5, {{kA, 0.0, 0.5, 0.5, false}}}}},
        {"half pedal at P = 0.65", {}, {{Op::Sustain, 0, 0.65, {{kA, 0.0, 1.0, 1.0, false}}}}},
        {"sostenuto with keys held then released",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::On, kB, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}, {kB, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 0.0, {{kA, 1.0, 0.0, 1.0, true}, {kB, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 0.0, 0.0, 0.0, false}, {kB, 1.0, 0.0, 1.0, false}}}}},
        {"key pressed after sostenuto is not latched",
         {},
         {{Op::Sostenuto, 0, 1.0, {{kA, 0.0, 0.0, 0.0, false}}},
          {Op::On, kA, 0.0, {{kA, 1.0, 0.0, 1.0, false}}},
          {Op::Off, kA, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"NoteOff of a latched key keeps it lifted",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"sostenuto released with the key still held",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 1.0, 0.0, 1.0, false}}}}},
        {"key without a damper is always lifted and never latched",
         {kTop},
         {{Op::Sustain, 0, 0.0, {{kTop, 1.0, 0.0, 1.0, false}}},
          {Op::On, kTop, 0.0, {{kTop, 1.0, 0.0, 1.0, false}}},
          {Op::Off, kTop, 0.0, {{kTop, 1.0, 0.0, 1.0, false}}},
          {Op::Sostenuto, 0, 1.0, {{kTop, 1.0, 0.0, 1.0, false}}},
          {Op::Sostenuto, 0, 0.0, {{kTop, 1.0, 0.0, 1.0, false}}}}},
        {"half key control with KeyDepth",
         {},
         {{Op::Depth, kA, 0.3, {{kA, 0.3, 0.0, 0.3, false}}},
          {Op::Sustain, 0, 0.5, {{kA, 0.3, 0.5, 0.5, false}}},
          {Op::Depth, kA, 0.8, {{kA, 0.8, 0.5, 0.8, false}}},
          {Op::Off, kA, 0.0, {{kA, 0.0, 0.5, 0.5, false}}}}},
        {"sostenuto at depth 0.49 is not down",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 0.49, {{kA, 1.0, 0.0, 1.0, false}}},
          {Op::Off, kA, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"sostenuto at depth 0.5 is down",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 0.5, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 0.0, {{kA, 1.0, 0.0, 1.0, true}}}}},
        {"key at depth 0.49 is not latched",
         {},
         {{Op::Depth, kA, 0.49, {{kA, 0.49, 0.0, 0.49, false}}},
          {Op::Sostenuto, 0, 1.0, {{kA, 0.49, 0.0, 0.49, false}}}}},
        {"key at depth 0.5 is latched with l_key = 1",
         {},
         {{Op::Depth, kA, 0.5, {{kA, 0.5, 0.0, 0.5, false}}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Depth, kA, 0.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"sostenuto has no hysteresis",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 0.6, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 0.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.5, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.49, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"sustain and sostenuto together",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 0.0, {}},
          {Op::Sustain, 0, 1.0, {{kA, 1.0, 1.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 0.0, 1.0, 1.0, false}}},
          {Op::Sustain, 0, 0.0, {{kA, 0.0, 0.0, 0.0, false}}}}},
        {"latched key ignores KeyDepth",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Depth, kA, 0.1, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 0.1, 0.0, 0.1, false}}}}},
        {"key retriggered while latched",
         {},
         {{Op::On, kA, 0.0, {}},
          {Op::Sostenuto, 0, 1.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Off, kA, 0.0, {}},
          {Op::On, kA, 0.0, {{kA, 1.0, 0.0, 1.0, true}}},
          {Op::Sostenuto, 0, 0.0, {{kA, 1.0, 0.0, 1.0, false}}}}},
        {"half pedal with a lower key depth",
         {},
         {{Op::Sustain, 0, 0.5, {}}, {Op::Depth, kA, 0.2, {{kA, 0.2, 0.5, 0.5, false}}}}},
        {"sustain below the half pedal region is ignored",
         {},
         {{Op::Sustain, 0, 0.2, {{kA, 0.0, 0.0, 0.0, false}}},
          {Op::Sustain, 0, 0.34, {{kA, 0.0, 0.0, 0.0, false}}}}},
    };
    return t;
}

void apply(DamperController& c, const Step& s) {
    switch (s.op) {
        case Op::On:
            c.noteOn(s.key);
            break;
        case Op::Off:
            c.noteOff(s.key, s.value);
            break;
        case Op::Depth:
            c.keyDepth(s.key, s.value);
            break;
        case Op::Sustain:
            c.setSustain(s.value);
            break;
        case Op::Sostenuto:
            c.setSostenuto(s.value);
            break;
    }
}

DamperController makeController(const std::vector<int>& undamped) {
    DamperController c;
    REQUIRE(c.configure(pme::dmp_test::kFs, 0.35, 0.65) == Status::Ok);
    for (int k = 0; k < kKeyCount; ++k) {
        DamperParams p = pme::dmp_test::examplePad();
        for (int u : undamped) {
            if (u == k) {
                p.hasDamper = false;
            }
        }
        REQUIRE(c.setKey(k, p) == Status::Ok);
    }
    return c;
}

}  // namespace

TEST_CASE("A6-T2 table of key sustain and sostenuto sequences") {
    CHECK(table().size() >= 15);
    for (const Sequence& seq : table()) {
        CAPTURE(seq.name);
        DamperController c = makeController(seq.undamped);
        int index = 0;
        for (const Step& step : seq.steps) {
            CAPTURE(index);
            apply(c, step);
            for (const Expect& e : step.expect) {
                CAPTURE(e.key);
                const KeyLift got = c.lift(e.key);
                CHECK(got.lKey == doctest::Approx(e.lKey).epsilon(1e-12));
                CHECK(got.lPed == doctest::Approx(e.lPed).epsilon(1e-12));
                CHECK(got.l == doctest::Approx(e.l).epsilon(1e-12));
                CHECK(got.latched == e.latched);
            }
            ++index;
        }
    }
}

TEST_CASE("A6-R5 hasDamper false gives e = 0 whatever the inputs") {
    DamperController c = makeController({kTop});
    CHECK_FALSE(c.hasDamper(kTop));
    CHECK(c.hasDamper(kA));
    c.noteOn(kTop);
    c.noteOff(kTop, 1.0);
    c.keyDepth(kTop, 0.3);
    c.setSustain(0.0);
    c.setSostenuto(1.0);
    for (int i = 0; i < 5000; ++i) {
        c.advance();
        CHECK(c.engagement(kTop) == 0.0);
    }
    c.setSostenuto(0.0);
    c.setSustain(1.0);
    c.advance(100);
    CHECK(c.engagement(kTop) == 0.0);
    CHECK(c.target(kTop) == 0.0);
}

TEST_CASE("A6-R5 configuration errors and out-of-range keys") {
    DamperController c;
    CHECK(c.configure(0.0, 0.35, 0.65) == Status::InvalidArgument);
    CHECK(c.configure(48000.0, 0.65, 0.35) == Status::InvalidArgument);
    CHECK(c.configure(48000.0, 0.5, 0.5) == Status::InvalidArgument);
    CHECK(c.configure(48000.0, -0.1, 0.5) == Status::InvalidArgument);
    CHECK(c.configure(48000.0, 0.2, 1.5) == Status::InvalidArgument);
    CHECK(c.setKey(5, DamperParams{}) == Status::InvalidArgument);  // not configured yet
    REQUIRE(c.configure(48000.0, 0.35, 0.65) == Status::Ok);
    CHECK(c.setKey(-1, DamperParams{}) == Status::InvalidArgument);
    CHECK(c.setKey(128, DamperParams{}) == Status::InvalidArgument);
    DamperParams bad = pme::dmp_test::examplePad();
    bad.tauUp = 0.0;
    CHECK(c.setKey(60, bad) == Status::InvalidArgument);
    CHECK(c.setKey(60, std::nullopt) == Status::Ok);
    CHECK_FALSE(c.hasDamper(60));
    // Events for keys outside 0 .. 127 and non-finite depths are ignored.
    c.noteOn(-3);
    c.noteOff(200, 0.5);
    c.keyDepth(300, 0.5);
    c.keyDepth(61, std::nan(""));
    c.setSustain(std::nan(""));
    c.setSostenuto(std::nan(""));
    CHECK(c.engagement(-1) == 0.0);
    CHECK(c.engagement(500) == 0.0);
    CHECK(c.target(500) == 0.0);
    CHECK(c.projected(500, 5) == 0.0);
    CHECK(c.tauDown(500) == 0.0);
    CHECK_FALSE(c.hasDamper(500));
    CHECK(c.lift(500).l == 0.0);
    CHECK(c.sustainDepth() == 0.0);
    CHECK_FALSE(c.sostenutoDown());
}

TEST_CASE("A6-R3 lift fraction and engagement target") {
    CHECK(pedalLift(0.0, 0.35, 0.65) == 0.0);
    CHECK(pedalLift(0.35, 0.35, 0.65) == 0.0);
    CHECK(pedalLift(0.5, 0.35, 0.65) == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(pedalLift(0.65, 0.35, 0.65) == 1.0);
    CHECK(pedalLift(1.0, 0.35, 0.65) == 1.0);
    CHECK(pedalLift(0.2, 0.1, 0.9) == doctest::Approx(0.125).epsilon(1e-12));
    CHECK(liftFraction(0.3, 0.7) == 0.7);
    CHECK(liftFraction(0.9, 0.7) == 0.9);
    // e_target = (1 - l)^eta_d
    CHECK(engagementTarget(0.0, 2.0) == 1.0);
    CHECK(engagementTarget(1.0, 2.0) == 0.0);
    CHECK(engagementTarget(0.5, 2.0) == doctest::Approx(0.25).epsilon(1e-14));
    CHECK(engagementTarget(0.5, 3.0) == doctest::Approx(0.125).epsilon(1e-14));
    CHECK(engagementTarget(0.25, 1.0) == doctest::Approx(0.75).epsilon(1e-14));
    CHECK(engagementTarget(-1.0, 2.0) == 1.0);  // clamped
    CHECK(engagementTarget(2.0, 2.0) == 0.0);
    CHECK(validateHalfPedal(0.35, 0.65) == Status::Ok);
    CHECK(validateHalfPedal(0.65, 0.35) == Status::InvalidArgument);
    CHECK(validateHalfPedal(0.4, 0.4) == Status::InvalidArgument);
    CHECK(validateHalfPedal(std::nan(""), 0.4) == Status::InvalidArgument);

    // The controller applies it: sustain at P = 0.5 and eta_d = 2 targets e = 0.25.
    DamperController c = makeController({});
    c.setSustain(0.5);
    CHECK(c.target(kA) == doctest::Approx(0.25).epsilon(1e-12));
    c.setSustain(0.0);
    CHECK(c.target(kA) == 1.0);
    c.noteOn(kA);
    CHECK(c.target(kA) == 0.0);
}

TEST_CASE("A6-R4 one-pole timing with tau_up and tau_down") {
    DamperParams p = pme::dmp_test::examplePad();
    p.tauUp = 0.004;  // 192 samples at 48 kHz
    p.tau0 = 0.012;   // 576 samples
    p.kRelease = 2.0;
    const double fs = 48000.0;

    // Landing, nu_rel = 0: after tau_down e = 1 - exp(-1) of the way from 0 to 1.
    EngagementFollower f;
    f.configure(p, fs, 0.0);
    f.setTarget(1.0, 0.0);
    CHECK(f.tauDown() == doctest::Approx(0.012).epsilon(1e-15));
    for (int i = 0; i < 576; ++i) {
        f.step();
    }
    CHECK(std::fabs(f.value() - (1.0 - std::exp(-1.0))) < 1e-12);

    // Lifting: after tau_up e = exp(-1) of the way from 1 to 0.
    f.configure(p, fs, 1.0);
    f.setTarget(0.0, 0.0);
    CHECK(f.tauUp() == 0.004);
    for (int i = 0; i < 192; ++i) {
        f.step();
    }
    CHECK(std::fabs(f.value() - std::exp(-1.0)) < 1e-12);

    // tau_down shortens with the release velocity: tau_0 / (1 + k_r nu_rel).
    for (double nu : {0.0, 0.25, 0.5, 1.0}) {
        CAPTURE(nu);
        CHECK(landingTau(0.012, 2.0, nu) ==
              doctest::Approx(0.012 / (1.0 + 2.0 * nu)).epsilon(1e-15));
        EngagementFollower g;
        g.configure(p, fs, 0.0);
        g.setTarget(1.0, nu);
        CHECK(g.tauDown() == doctest::Approx(0.012 / (1.0 + 2.0 * nu)).epsilon(1e-15));
    }
    CHECK(landingTau(0.012, 2.0, 5.0) == doctest::Approx(0.004).epsilon(1e-15));  // clamped to 1
    CHECK(landingTau(0.012, 2.0, -1.0) == 0.012);

    // The closed-form look-ahead equals stepping.
    f.configure(p, fs, 0.2);
    f.setTarget(0.9, 0.0);
    const double ahead = f.projected(100);
    for (int i = 0; i < 100; ++i) {
        f.step();
    }
    CHECK(std::fabs(ahead - f.value()) < 1e-13);
    CHECK(f.projected(0) == f.value());

    // The follower snaps onto the target and stops reporting motion.
    f.configure(p, fs, 1.0);
    f.setTarget(0.0, 0.0);
    int steps = 0;
    while (f.step()) {
        ++steps;
        REQUIRE(steps < 100000);
    }
    CHECK(f.value() == 0.0);
    CHECK_FALSE(f.moving());
    CHECK(f.projected(1000000) == 0.0);  // snapped far ahead

    // Through the controller: release velocity of the last NoteOff sets tau_down.
    DamperController c = makeController({});
    c.noteOn(kA);
    c.advance(48000);
    CHECK(c.engagement(kA) == 0.0);
    c.noteOff(kA, 1.0);
    CHECK(c.tauDown(kA) == doctest::Approx(0.012 / 3.0).epsilon(1e-14));
    CHECK(c.anyMoving());
    const int n = static_cast<int>(std::lround(0.012 / 3.0 * fs));  // one tau_down
    c.advance(n);
    CHECK(std::fabs(c.engagement(kA) - (1.0 - std::exp(-1.0))) < 1e-12);
    CHECK(c.projected(kA, 100000) == 1.0);
    c.advance(48000);
    CHECK(c.engagement(kA) == 1.0);
    CHECK_FALSE(c.anyMoving());
}

TEST_CASE("A6-R4 parameter validation") {
    DamperParams p = pme::dmp_test::examplePad();
    CHECK(validateParams(p) == Status::Ok);
    auto bad = [&](auto mutate) {
        DamperParams q = p;
        mutate(q);
        return validateParams(q);
    };
    CHECK(bad([](DamperParams& q) { q.xRatio = -0.1; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.xRatio = 1.1; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.width = -1.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.Kd = -1.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.Rd = -1.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.tau0 = 0.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.tauUp = -1.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.kRelease = -1.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.etaD = 0.0; }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) { q.Rd = std::nan(""); }) == Status::InvalidArgument);
    CHECK(bad([](DamperParams& q) {
              q.Kd = 0.0;
              q.Rd = 0.0;
          }) == Status::Ok);
}
