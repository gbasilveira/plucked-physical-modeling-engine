// SPDX-License-Identifier: MIT
// Covers: A5-R6
// Flexible shank: the normal-coordinate transform of the head-shank pair, the stiff-shank limit,
// and the rigid path when the shank is absent.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "pme/string/string_model.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

TEST_CASE("A5-R6 the shank normal modes reproduce the two-mass free response") {
    const std::vector<StringModes> modes = unison(1, true);
    HammerParams h = pianoHammer();
    StruckNoteDesc d = describe(modes, h);
    ShankParams sp;
    sp.mass = 0.012;
    sp.frequency = 3000.0;
    sp.sigma = 0.0;
    d.shank = sp;
    StruckNote<double> note;
    REQUIRE(note.configure(d, kFs, nullptr) == Status::Ok);
    const std::size_t hi = note.segment().size() - 2;  // X, then Y
    const double mHead = h.mass;
    const double mTot = h.mass + sp.mass;
    const double displ = 1.0e-3;
    // Head displaced by `displ`, shank at 0, both at rest: X = m_H d / M, Y = d.
    note.segment().q()[hi] = mHead / mTot * displ;
    note.segment().q()[hi + 1] = displ;
    const double dt = 1.0 / kFs;
    const double omega = 2.0 * std::numbers::pi * sp.frequency;
    const double theta = omega * dt;
    std::vector<double> y;
    double worst = 0.0;
    for (int n = 0; n < 400; ++n) {
        y.push_back(note.segment().q()[hi + 1]);
        // Head position against the analytic two-mass solution (u = 0 at the start means the
        // discrete cosine is shifted by half a sample).
        const double expect = mHead / mTot * displ + sp.mass / mTot * displ *
                                                         std::cos(theta * (n + 0.5)) /
                                                         std::cos(0.5 * theta);
        worst = std::max(worst, std::abs(note.headPosition() - expect) / displ);
        note.step();
    }
    MESSAGE("worst head-position error vs the two-mass solution " << sci(worst));
    CHECK(worst <= 1e-10);
    // Free-response frequency from the recurrence y[n+1] + y[n-1] = 2 cos(theta) y[n].
    double fWorst = 0.0;
    for (std::size_t n = 1; n + 1 < y.size(); ++n) {
        if (std::abs(y[n]) < 0.5 * displ) {
            continue;
        }
        const double c = (y[n + 1] + y[n - 1]) / (2.0 * y[n]);
        const double f = std::acos(c) / (2.0 * std::numbers::pi * dt);
        fWorst = std::max(fWorst, std::abs(f / sp.frequency - 1.0));
    }
    MESSAGE("worst relative frequency error " << sci(fWorst));
    CHECK(fWorst <= 1e-10);
}

TEST_CASE("A5-R6 a very stiff shank gives the contact time and peak force of a rigid hammer") {
    const std::vector<StringModes> modes = unison(1, true);
    HammerParams elastic = pianoHammer();
    elastic.muHC = 0.0;
    ShankParams sp;
    sp.mass = 0.002;
    sp.frequency = 20000.0;
    sp.sigma = 300.0;

    // Head 8 g + shank 2 g, stiffly joined, behaves as a 10 g rigid hammer.
    HammerParams rigidHeavy = elastic;
    rigidHeavy.mass = elastic.mass + sp.mass;
    StruckNote<double> rigid;
    REQUIRE(rigid.configure(describe(modes, rigidHeavy), kFs, nullptr) == Status::Ok);
    StruckNoteDesc d = describe(modes, elastic);
    d.shank = sp;
    StruckNote<double> flexible;
    REQUIRE(flexible.configure(d, kFs, nullptr) == Status::Ok);
    for (StruckNote<double>* n : {&rigid, &flexible}) {
        REQUIRE(n->launch(0.5, 0.0) == Status::Ok);
        for (int s = 0; s < 800; ++s) {
            n->step();
        }
        REQUIRE(n->diagnostics().contacts == 1);
    }
    const double tc0 = rigid.diagnostics().lastContactDuration;
    const double tc1 = flexible.diagnostics().lastContactDuration;
    const double f0 = rigid.diagnostics().lastPeakForce;
    const double f1 = flexible.diagnostics().lastPeakForce;
    MESSAGE("contact time rigid " << sci(tc0, 6) << " shank " << sci(tc1, 6) << " (ratio-1 "
                                  << sci(tc1 / tc0 - 1.0) << "); peak force rigid " << sci(f0, 6)
                                  << " shank " << sci(f1, 6) << " (ratio-1 " << sci(f1 / f0 - 1.0)
                                  << ")");
    CHECK(std::abs(tc1 / tc0 - 1.0) <= 0.01);
    CHECK(std::abs(f1 / f0 - 1.0) <= 0.01);
}

TEST_CASE("A5-R6 a softer shank changes the contact (the option is live)") {
    const std::vector<StringModes> modes = unison(1, true);
    HammerParams elastic = pianoHammer();
    elastic.muHC = 0.0;
    ShankParams sp;
    sp.mass = 0.006;
    sp.frequency = 1500.0;
    sp.sigma = 100.0;
    StruckNote<double> rigid;
    REQUIRE(rigid.configure(describe(modes, elastic), kFs, nullptr) == Status::Ok);
    StruckNoteDesc d = describe(modes, elastic);
    d.shank = sp;
    StruckNote<double> flexible;
    REQUIRE(flexible.configure(d, kFs, nullptr) == Status::Ok);
    for (StruckNote<double>* n : {&rigid, &flexible}) {
        REQUIRE(n->launch(0.5, 0.0) == Status::Ok);
        for (int s = 0; s < 800; ++s) {
            n->step();
        }
    }
    CHECK(std::abs(flexible.diagnostics().lastContactDuration /
                       rigid.diagnostics().lastContactDuration -
                   1.0) > 0.01);
    // Launch: both masses at v_H with zero deflection.
    StruckNote<double> fresh;
    REQUIRE(fresh.configure(d, kFs, nullptr) == Status::Ok);
    REQUIRE(fresh.launch(0.5, 0.0) == Status::Ok);
    double v = 0.0;
    REQUIRE(hammerVelocity(0.5, elastic, v) == Status::Ok);
    const std::size_t hi = fresh.segment().size() - 2;
    CHECK(fresh.segment().q()[hi + 1] == 0.0);
    CHECK(fresh.segment().u()[hi + 1] == 0.0);
    CHECK(fresh.headVelocity() == doctest::Approx(v).epsilon(1e-12));
}

TEST_CASE("A5-R6 an absent shank is bit-identical to the rigid code path") {
    const std::vector<StringModes> modes = unison(3, false);
    const HammerParams h = pianoHammer();
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.7, 0.3) == Status::Ok);

    // The same note assembled by hand from the rigid pieces, with no shank branch.
    const double dt = 1.0 / kFs;
    std::size_t total = 0;
    for (const StringModes& m : modes) {
        total += m.k.size();
    }
    nl::SavSegment<double> seg;
    REQUIRE(seg.configure(total, 1, nl::kMaxDashpots, kFs) == Status::Ok);
    std::vector<StringSlice> slices(3);
    std::size_t off = 0;
    for (std::size_t s = 0; s < 3; ++s) {
        const StringModes& m = modes[s];
        for (std::size_t i = 0; i < m.k.size(); ++i) {
            const double sd = m.oscV[i].sigmaHat * dt;
            osc::OscCoeffsF64 c;
            c.beta = (1.0 - sd) / (1.0 + sd);
            c.kappa = m.oscV[i].omegaHat2 * dt * dt / (1.0 + sd);
            c.gamma = dt * dt / (1.0 + sd);
            c.omegaHat2 = m.oscV[i].omegaHat2;
            c.sigmaHat = m.oscV[i].sigmaHat;
            REQUIRE(seg.setCoordinate(off + i, c, m.rhoA) == Status::Ok);
        }
        slices[s].offset = off;
        slices[s].phiBar.assign(m.k.size(), 0.0F);
        string::windowedShapes(m, h.strikeRatio * m.L, string::WindowKind::RaisedCosine, h.width,
                               slices[s].phiBar);
        off += m.k.size();
    }
    REQUIRE(seg.setLumped(total, h.mass) == Status::Ok);
    HammerPotential<double> pot;
    REQUIRE(pot.configure(std::move(slices), total, 0.0, 1.0) == Status::Ok);
    HammerFelt<double> felt;
    REQUIRE(felt.configure(h, pot, total + 1, kFs) == Status::Ok);
    StrikeMap map;
    REQUIRE(strikeMap(h, UnaCordaParams{}, SoftPedal::UnaCorda, 3, 0.3, map) == Status::Ok);
    pot.setStrikeMap(map);
    pot.setActive(true);
    double vH = 0.0;
    REQUIRE(launchVelocity(0.7, 0.3, h, UnaCordaParams{}, SoftPedal::UnaCorda, vH) == Status::Ok);
    seg.q()[total] = -vH * dt;
    seg.u()[total] = vH * dt;
    felt.reset();

    std::vector<double> force(total + 1, 0.0);
    for (int n = 0; n < 100; ++n) {  // before the release: the hand-made note has none
        note.step();
        const std::span<const double> z(seg.q(), seg.size());
        felt.update(pot, z, force);
        const std::array<const nl::Potential<double>*, 1> pots{&pot};
        nl::savStep<double>(seg, pots, felt.dashpots(), force);
    }
    REQUIRE(note.inFlight());
    for (std::size_t i = 0; i < seg.size(); ++i) {
        CHECK(note.segment().q()[i] == seg.q()[i]);
    }
    CHECK(note.segment().size() == seg.size());
}
