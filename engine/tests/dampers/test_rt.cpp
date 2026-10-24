// SPDX-License-Identifier: MIT
// Covers: A6-R1, A6-R2, A6-R3, A6-R4, A6-R5 (real-time safety)
// The controller, the diagonal refresh and the rank-one step allocate and block nothing: under the
// rt-safety preset any allocation inside the scope aborts the process.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "fixtures.hpp"
#include "pme/dampers/controller.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/rank_one.hpp"
#include "pme/rtcheck/rtcheck.hpp"

TEST_CASE("A6-R5 controller events and advance allocate nothing") {
    using namespace pme;
    using namespace pme::dmp;
    DamperController c;
    REQUIRE(c.configure(pme::dmp_test::kFs, 0.35, 0.65) == Status::Ok);
    for (int k = 0; k < kKeyCount; ++k) {
        DamperParams p = pme::dmp_test::examplePad();
        p.hasDamper = k < 100;
        REQUIRE(c.setKey(k, p) == Status::Ok);
    }
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int k = 20; k < 100; k += 3) {
            c.noteOn(k);
        }
        c.setSustain(0.5);
        c.setSostenuto(1.0);
        for (int i = 0; i < 2000; ++i) {
            c.advance();
        }
        for (int k = 20; k < 100; k += 3) {
            c.noteOff(k, 0.7);
            c.keyDepth(k + 1, 0.4);
        }
        c.setSostenuto(0.0);
        c.setSustain(0.0);
        sink += c.advance(4000) ? 1.0 : 0.0;
        for (int k = 0; k < kKeyCount; ++k) {
            sink += c.engagement(k) + c.projected(k, 16) + c.lift(k).l + c.target(k);
        }
    }
    CHECK(sink > 0.0);
}

TEST_CASE("A6-R1 diagonal refresh and ramps allocate nothing") {
    using namespace pme;
    using namespace pme::dmp;
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    DiagonalDamper d;
    REQUIRE(d.configure(m, pme::dmp_test::examplePad()) == Status::Ok);
    osc::ModeBank bank;
    bank.resize(m.omega.size());
    REQUIRE(d.initialise(0.0, bank, 0) == Status::Ok);
    std::vector<osc::CoeffRamp> ramps(m.omega.size());
    std::vector<float> force(m.omega.size(), 0.0F);
    for (std::size_t i = 0; i < m.omega.size(); ++i) {
        bank.q()[i] = 1e-4F;
    }
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        double e = 0.0;
        for (int block = 0; block < 40; ++block) {
            e += 0.025;
            if (d.plan(e, 16, ramps)) {
                for (int s = 0; s < 16; ++s) {
                    osc::applyRampsAt(bank, 0, ramps, s);
                    bank.step(force);
                }
                d.commit(bank, 0);
            }
        }
    }
    CHECK(d.engagementInForce() == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(d.refreshFailures() == 0);
}

TEST_CASE("A6-R2 rank-one step allocates nothing") {
    using namespace pme;
    using namespace pme::dmp;
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    const std::size_t n = m.omega.size();
    nl::SavSegment<float> seg;
    REQUIRE(seg.configure(n, 0, 1, m.fs) == Status::Ok);
    for (std::size_t i = 0; i < n; ++i) {
        osc::OscCoeffsF64 c;
        REQUIRE(osc::compileOscillatorF64(m.omega[i], m.sigma[i], m.fs, c) == Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, m.rhoA) == Status::Ok);
        seg.q()[i] = 1e-4F;
    }
    RankOneDamper<float> damper;
    REQUIRE(damper.configure(m, pme::dmp_test::examplePad(), n) == Status::Ok);
    std::vector<float> force(n, 0.0F);
    const std::array<const nl::Potential<float>*, 0> none{};
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int s = 0; s < 256; ++s) {
            const double e = 0.5 + 0.5 * static_cast<double>(s % 7) / 7.0;
            std::fill(force.begin(), force.end(), 0.0F);
            damper.addSpringForce(e, seg, force);
            const std::array<nl::Dashpot<float>, 1> dash{damper.dashpot(e)};
            nl::savStep<float>(seg, none, dash, force);
            sink += nl::savEnergy(seg) + damper.springEnergy(e, seg) + damper.pad(seg.q());
        }
    }
    CHECK(sink != 0.0);
}
