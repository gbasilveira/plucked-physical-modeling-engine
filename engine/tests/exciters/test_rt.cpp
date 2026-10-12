// SPDX-License-Identifier: MIT
// Covers: A5-R2, A5-R3, A5-R4, A5-R5
// Real-time safety of a trichord strike with a mass-spring-damper bridge point (float32): under
// the rt-safety preset any allocation or blocking call inside the scope aborts the process.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "pme/rtcheck/rtcheck.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

TEST_CASE("A5-R2 a trichord strike on a mass-spring-damper bridge allocates nothing") {
    const std::vector<StringModes> modes = unison(3, false);
    for (const bool stulov : {false, true}) {
        HammerParams h = pianoHammer();
        if (stulov) {
            h.law = FeltLaw::Stulov;
            h.stulovEps = 0.3;
            h.stulovTau = 5.0e-4;
        }
        bridge::BridgePoint<float> point;
        REQUIRE(configureBridge(point) == Status::Ok);
        StruckNote<float> note;
        REQUIRE(note.configure(describe(modes, h), kFs, &point) == Status::Ok);
        double sink = 0.0;
        {
            pme::rtcheck::ScopedRealtimeThread realtime;
            for (int s = 0; s < 3000; ++s) {
                if (s == 0 || s == 1500) {
                    static_cast<void>(note.launch(0.7, 0.4));  // R4: re-launch in flight / restart
                }
                static_cast<void>(note.setShift(0.4 + 0.0001 * s));  // R5: ramping shift
                note.step();
                sink += static_cast<double>(point.wV());
            }
            sink += note.energy();
            sink += note.diagnostics().lastPeakForce;
        }
        CHECK(std::isfinite(sink));
        CHECK(sink != 0.0);
        CHECK(note.diagnostics().strikes == 2);
    }
}
