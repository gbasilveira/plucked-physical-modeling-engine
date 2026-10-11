// SPDX-License-Identifier: MIT
// Covers: A4-R2, A4-R5
// Real-time safety of the per-sample path of a BridgePoint with three segments (float32 states):
// under the rt-safety preset any allocation or blocking call inside the scope aborts the process.
#include <doctest/doctest.h>

#include <cmath>

#include "pme/rtcheck/rtcheck.hpp"
#include "test_support.hpp"

TEST_CASE("A4-R2 the per-sample path of a BridgePoint with three segments allocates nothing") {
    using namespace pme::bridge_test;
    Rig<float> rig;
    REQUIRE(buildThreeSegmentRig(rig) == pme::Status::Ok);
    REQUIRE(rig.segs.size() == 3);
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int s = 0; s < 256; ++s) {
            rig.point.setExternalVelocity(1e-6F * static_cast<float>(s % 7));  // A4-R5 hook
            rig.point.addVerticalForce(1e-3F);                                  // A4-R5 hook
            rig.step();
            sink += static_cast<double>(rig.point.wV()) + static_cast<double>(rig.point.wH());
        }
        sink += rig.energy();
    }
    CHECK(std::isfinite(sink));
    CHECK(sink != 0.0);
}
