// SPDX-License-Identifier: MIT
// Covers: A5-R7
// Per-strike diagnostics: contact duration, peak force and the strike counter, against values
// computed independently from a recorded trace.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

TEST_CASE("A5-R7 contact duration and peak force equal values from the recorded trace") {
    const std::vector<StringModes> modes = unison(1, true);
    HammerParams h = pianoHammer();
    h.muHC = 0.0;  // elastic felt: the whole hammer force is the SAV force
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
    REQUIRE(note.launch(0.6, 0.0) == Status::Ok);
    const std::size_t hi = note.segment().size() - 1;
    const double dt = 1.0 / kFs;
    std::vector<double> delta;  // delta at sample n (before step n)
    std::vector<double> force;  // m_H delta_tt x_H at sample n
    for (int n = 0; n < 600; ++n) {
        const std::span<const double> z(note.segment().q(), note.segment().size());
        delta.push_back(note.potential().penetration(z, 0));
        const double uBefore = note.segment().u()[hi];
        note.step();
        force.push_back(h.mass * (note.segment().u()[hi] - uBefore) / (dt * dt));
    }
    // Independent evaluation: zero crossings of delta by linear interpolation.
    double start = -1.0;
    double end = -1.0;
    for (std::size_t n = 1; n < delta.size(); ++n) {
        if (start < 0.0 && delta[n] > 0.0 && delta[n - 1] <= 0.0) {
            start = static_cast<double>(n - 1) + (-delta[n - 1]) / (delta[n] - delta[n - 1]);
        } else if (start >= 0.0 && end < 0.0 && delta[n] <= 0.0) {
            end = static_cast<double>(n - 1) + delta[n - 1] / (delta[n - 1] - delta[n]);
        }
    }
    REQUIRE(start >= 0.0);
    REQUIRE(end > start);
    double peak = 0.0;
    for (std::size_t n = 0; n < force.size(); ++n) {
        if (delta[n] > 0.0) {  // samples in contact (the release zeroes u later: not a force)
            peak = std::max(peak, std::abs(force[n]));
        }
    }
    const HammerDiagnostics& d = note.diagnostics();
    MESSAGE("duration " << sci(d.lastContactDuration, 9) << " vs " << sci((end - start) * dt, 9)
                        << "; peak " << sci(d.lastPeakForce, 9) << " vs " << sci(peak, 9));
    CHECK(d.strikes == 1);
    CHECK(d.contacts == 1);
    CHECK(d.lastContactDuration == doctest::Approx((end - start) * dt).epsilon(1e-9));
    CHECK(d.lastPeakForce == doctest::Approx(peak).epsilon(1e-9));
    CHECK(d.totalContactTime == d.lastContactDuration);
}

TEST_CASE("A5-R7 the runtime reads and clears the counters and each strike is counted") {
    const std::vector<StringModes> modes = unison(2, true);
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
    for (int k = 1; k <= 3; ++k) {
        REQUIRE(note.launch(0.3 * k, 0.0) == Status::Ok);
        for (int n = 0; n < 2500 && note.inFlight(); ++n) {
            note.step();
        }
        CHECK(note.diagnostics().strikes == static_cast<std::uint32_t>(k));
        CHECK(note.diagnostics().contacts >= static_cast<std::uint32_t>(k));
    }
    CHECK(note.diagnostics().lastPeakForce > 0.0);
    CHECK(note.diagnostics().lastContactDuration > 0.0);
    note.clearDiagnostics();
    CHECK(note.diagnostics().strikes == 0);
    CHECK(note.diagnostics().contacts == 0);
    CHECK(note.diagnostics().lastPeakForce == 0.0);
    CHECK(note.diagnostics().totalContactTime == 0.0);
}

TEST_CASE("A5-R7 a harder strike has a larger peak force and a shorter contact") {
    const std::vector<StringModes> modes = unison(1, false);
    double peakSoft = 0.0;
    double peakHard = 0.0;
    double tSoft = 0.0;
    double tHard = 0.0;
    for (const double nu : {0.2, 0.9}) {
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        REQUIRE(note.launch(nu, 0.0) == Status::Ok);
        for (int n = 0; n < 2500 && note.inFlight(); ++n) {
            note.step();
        }
        (nu < 0.5 ? peakSoft : peakHard) = note.diagnostics().lastPeakForce;
        (nu < 0.5 ? tSoft : tHard) = note.diagnostics().lastContactDuration;
    }
    CHECK(peakHard > peakSoft);
    CHECK(tHard < tSoft);
}
