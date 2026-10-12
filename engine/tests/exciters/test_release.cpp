// SPDX-License-Identifier: MIT
// Covers: A5-R4, A5-T4
// Release and check at d_check, double strikes without special code, and repetition: a hammer
// re-launched in flight keeps its position continuous (A5-T4).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "pme/osc/mode_bank.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

TEST_CASE("A5-R4 the hammer leaves after rebound and the strings then equal a free run bit for bit") {
    const std::vector<StringModes> modes = unison(1, false);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.5, 0.0) == Status::Ok);
    int released = -1;
    for (int n = 0; n < 5000 && note.inFlight(); ++n) {
        note.step();
        released = n;
    }
    REQUIRE_FALSE(note.inFlight());
    CHECK(released > 0);
    CHECK(note.releasedEnergy() > 0.0);
    CHECK_FALSE(note.potential().active());
    const std::size_t hi = note.segment().size() - 1;
    CHECK(note.segment().u()[hi] == 0.0);  // frozen: u = 0, held position
    const double held = note.segment().q()[hi];

    // Independent free run of the string modes from the state at release.
    const std::size_t n = modes[0].k.size();
    std::vector<double> q(note.segment().q(), note.segment().q() + n);
    std::vector<double> u(note.segment().u(), note.segment().u() + n);
    std::vector<double> beta(n);
    std::vector<double> kappa(n);
    std::vector<double> gamma(n);
    const std::vector<double> force(n, 0.0);
    const double dt = 1.0 / kFs;
    for (std::size_t i = 0; i < n; ++i) {
        const double sd = modes[0].oscV[i].sigmaHat * dt;
        beta[i] = (1.0 - sd) / (1.0 + sd);
        kappa[i] = modes[0].oscV[i].omegaHat2 * dt * dt / (1.0 + sd);
        gamma[i] = dt * dt / (1.0 + sd);
    }
    for (int s = 0; s < 600; ++s) {
        note.step();
        osc::stepModes<double, double>(q.data(), u.data(), beta.data(), kappa.data(),
                                       gamma.data(), force.data(), n);
    }
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(note.segment().q()[i] == q[i]);  // bit-identical
        CHECK(note.segment().u()[i] == u[i]);
    }
    CHECK(note.segment().q()[hi] == held);
    CHECK(note.segment().u()[hi] == 0.0);
}

TEST_CASE("A5-R4 a bass double strike with a slow heavy hammer gives two contact intervals") {
    // A 40 Hz string of 1 m and a 15 g hammer at key velocity 0.05: the string returns and meets
    // the slow hammer again before it leaves. No special code: the contact law does it.
    StringParams sp = plainSteel(1.0, 40.0, false);
    const std::vector<StringModes> modes{compile(sp, 14000.0)};
    REQUIRE_FALSE(modes[0].k.empty());
    HammerParams h = pianoHammer();
    h.mass = 15.0e-3;
    h.K = 1.0e9;
    h.width = 0.02;
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.05, 0.0) == Status::Ok);
    int contactIntervals = 0;
    bool wasIn = false;
    int steps = 0;
    while (note.inFlight() && steps < 100000) {
        const std::span<const double> z(note.segment().q(), note.segment().size());
        const bool inContact = note.potential().maxPenetration(z) > 0.0;
        if (inContact && !wasIn) {
            ++contactIntervals;
        }
        wasIn = inContact;
        note.step();
        ++steps;
    }
    REQUIRE_FALSE(note.inFlight());
    MESSAGE("contact intervals " << contactIntervals << ", released after " << steps << " samples");
    CHECK(contactIntervals == 2);
    CHECK(note.diagnostics().contacts == 2);
}

TEST_CASE("A5-T4 re-launching in flight inside d_check keeps x_H continuous") {
    const std::vector<StringModes> modes = unison(1, false);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.5, 0.0) == Status::Ok);
    const std::size_t hi = note.segment().size() - 1;
    std::vector<double> x;  // x_H at every sample from the launch
    x.push_back(note.headPosition());
    double vOld = 0.0;
    for (int n = 0; n < 5000; ++n) {
        note.step();
        x.push_back(note.headPosition());
        const std::span<const double> z(note.segment().q(), note.segment().size());
        if (note.potential().penetration(z, 0) < -1.0e-3 && note.headVelocity() < 0.0) {
            vOld = note.headVelocity();
            break;
        }
    }
    REQUIRE(vOld < 0.0);
    REQUIRE(note.inFlight());
    const double dCheck = pianoHammer().dCheck;
    {
        const std::span<const double> z(note.segment().q(), note.segment().size());
        REQUIRE(note.potential().penetration(z, 0) > -dCheck);
    }
    const double uOld = note.segment().u()[hi];
    const std::size_t k = x.size() - 1;  // the sample of the re-launch
    // x_H(k) = x_H(k - 1) + old velocity dt (the position sequence before the launch).
    CHECK(x[k] == doctest::Approx(x[k - 1] + uOld).epsilon(1e-12));

    double vNew = 0.0;
    REQUIRE(hammerVelocity(0.9, pianoHammer(), vNew) == Status::Ok);
    REQUIRE(note.launch(0.9, 0.0) == Status::Ok);
    CHECK(note.headPosition() == x[k]);  // no jump at the re-launch sample
    CHECK(note.headVelocity() == doctest::Approx(vNew).epsilon(1e-12));  // the new v_H
    // The positions that follow advance by the new velocity (until contact).
    const double dt = 1.0 / kFs;
    double prev = note.headPosition();
    for (int n = 0; n < 3; ++n) {
        note.step();
        const double now = note.headPosition();
        const std::span<const double> z(note.segment().q(), note.segment().size());
        if (note.potential().penetration(z, 0) > 0.0) {
            break;
        }
        CHECK(now - prev == doctest::Approx(vNew * dt).epsilon(1e-9));
        prev = now;
    }
    CHECK(note.diagnostics().strikes == 2);
}

TEST_CASE("A5-T4 a hammer that has been released restarts at the launch gap") {
    const std::vector<StringModes> modes = unison(1, false);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.5, 0.0) == Status::Ok);
    for (int n = 0; n < 5000 && note.inFlight(); ++n) {
        note.step();
    }
    REQUIRE_FALSE(note.inFlight());
    const double frozen = note.headPosition();
    double vNew = 0.0;
    REQUIRE(hammerVelocity(0.7, pianoHammer(), vNew) == Status::Ok);
    REQUIRE(note.launch(0.7, 0.0) == Status::Ok);
    const std::span<const double> z(note.segment().q(), note.segment().size());
    const double gap = vNew / kFs;
    CHECK(note.inFlight());
    CHECK(note.potential().active());
    CHECK(note.potential().penetration(z, 0) == doctest::Approx(-gap).epsilon(1e-9));
    CHECK(note.headPosition() != frozen);
    CHECK(note.headVelocity() == doctest::Approx(vNew).epsilon(1e-12));

    // In flight but farther than d_check from the string (artificially placed): launch gap again.
    const std::size_t hi = note.segment().size() - 1;
    note.segment().q()[hi] = static_cast<double>(note.segment().q()[hi]) - 0.02;
    REQUIRE(note.potential().penetration(z, 0) < -pianoHammer().dCheck);
    REQUIRE(note.launch(0.4, 0.0) == Status::Ok);
    double vFar = 0.0;
    REQUIRE(hammerVelocity(0.4, pianoHammer(), vFar) == Status::Ok);
    CHECK(note.potential().penetration(z, 0) == doctest::Approx(-vFar / kFs).epsilon(1e-9));
}

TEST_CASE("A5-T4 re-striking during contact has no jump and the energy is accounted") {
    const std::vector<StringModes> modes = unison(1, true);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.4, 0.0) == Status::Ok);
    const std::size_t hi = note.segment().size() - 1;
    for (int n = 0; n < 5000; ++n) {
        note.step();
        const std::span<const double> z(note.segment().q(), note.segment().size());
        if (note.potential().penetration(z, 0) > 2.0e-4) {
            break;
        }
    }
    const std::span<const double> z(note.segment().q(), note.segment().size());
    REQUIRE(note.potential().penetration(z, 0) > 0.0);  // touching
    const double mass = pianoHammer().mass;
    const double dt = 1.0 / kFs;
    const double xBefore = note.headPosition();
    const double eBefore = note.energy();
    const double vOldHalf = note.segment().u()[hi] / dt;
    double vNew = 0.0;
    REQUIRE(hammerVelocity(0.9, pianoHammer(), vNew) == Status::Ok);
    REQUIRE(note.launch(0.9, 0.0) == Status::Ok);
    CHECK(note.headPosition() == xBefore);  // no jump
    CHECK(note.headVelocity() == doctest::Approx(vNew).epsilon(1e-12));
    const double eAfter = note.energy();
    const double expected = 0.5 * mass * (vNew * vNew - vOldHalf * vOldHalf);
    MESSAGE("re-strike energy change " << sci(eAfter - eBefore, 6) << " expected "
                                       << sci(expected, 6));
    CHECK(eAfter - eBefore == doctest::Approx(expected).epsilon(1e-12));

    // After the re-strike the felt still only removes energy (balance per step).
    double ePrev = eAfter;
    double worst = 0.0;
    for (int n = 0; n < 400; ++n) {
        const std::uint32_t resets = note.segment().counters().resets;
        const double released = note.releasedEnergy();
        note.step();
        const double e = note.energy();
        const double diss = nl::savDissipation<double>(note.segment(), note.felt().dashpots());
        if (note.segment().counters().resets == resets && note.releasedEnergy() == released) {
            worst = std::max(worst, std::abs(e - ePrev + diss) / eAfter);
        }
        ePrev = e;
    }
    MESSAGE("worst per-step balance error after the re-strike " << sci(worst));
    CHECK(worst <= 1e-12);
}
