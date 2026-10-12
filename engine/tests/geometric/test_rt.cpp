// SPDX-License-Identifier: MIT
// Covers: A7-R1, A7-R3, A7-R4
// Real-time safety of the per-sample path of a segment with tension modulation and 8 requested
// longitudinal modes (float32 states): under the rt-safety preset any allocation or blocking call
// inside the scope aborts the process.
#include <doctest/doctest.h>

#include <cmath>

#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/rtcheck/rtcheck.hpp"
#include "test_support.hpp"

TEST_CASE("A7-R1 a segment with tension modulation and 8 longitudinal modes allocates nothing") {
    using namespace pme::geometric_test;
    pme::string::GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.longitudinalModes = 8;
    cfg.kappaL = 0.026;
    cfg.kappaT = 0.01;
    cfg.fMax = 80000.0;
    Built<float> b;
    build(b, plainSteel(0.65, 262.0), cfg, 14000.0, 96000.0);  // 96 kHz keeps all 8 modes
    REQUIRE(b.status == pme::Status::Ok);
    REQUIRE(b.seg.longitudinalCount() == 8);
    pme::bridge::BridgePoint<float> point;
    pme::bridge::ParallelModel model;
    REQUIRE(pme::bridge::massSpringDamperModel(0.3, 0.3 * 1885.0 * 1885.0, 5.0, model) ==
            pme::Status::Ok);
    REQUIRE(point.configure(pme::BridgePointDesc{}, model, 96000.0) == pme::Status::Ok);
    trianglePluck<float>(b.modes, 4.0e-3, 0.12 * b.modes.L, 0.3, b.seg.qV(), b.seg.qH());
    b.seg.initialisePsi();
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int s = 0; s < 512; ++s) {
            sink += static_cast<double>(b.seg.addBridgeForces(point));
            point.advance();
            b.seg.advance(point.accelV(), point.accelH());
            sink += b.seg.transverseEnergy(
                        static_cast<double>(point.wV()), static_cast<double>(point.incrementV()),
                        static_cast<double>(point.wH()), static_cast<double>(point.incrementH())) +
                    b.seg.longitudinalEnergy();
        }
    }
    CHECK(std::isfinite(sink));
    CHECK(sink != 0.0);
}
