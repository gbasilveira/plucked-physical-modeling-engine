// SPDX-License-Identifier: MIT
// Covers: A4-T1
// Strings on a lossless mass-spring bridge: the partitioned scheme conserves the energy derived in
// docs/dev/bridge.md (SPEC-BRG "Discrete coupling order"; SPEC-OSC "Discrete energy").
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>

#include "test_support.hpp"

namespace {

using namespace pme::bridge_test;

struct Result {
    double maxDeviation{};  // max |E - E0| / E0
    double slopePer1e6{};   // least-squares slope of (E - E0)/E0, per 1e6 samples
    double e0{};
    std::size_t modes{};
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): theta_b, sample count
Result runLossless(double thetaB, std::size_t samples) {
    Rig<double> rig;
    const pme::StringModes a = compile(plainSteel(0.65, 262.0, true), 16000.0);
    const pme::StringModes b = compile(plainSteel(0.75, 330.0, true), 16000.0);
    REQUIRE(a.k.size() >= 40);
    REQUIRE(b.k.size() >= 40);
    REQUIRE(rig.addSegment(a, pme::BridgeEnd::AtL) == pme::Status::Ok);
    REQUIRE(rig.addSegment(b, pme::BridgeEnd::AtL) == pme::Status::Ok);
    // Lossless mass-spring bridge: m = 0.4 kg, f_b = 300 Hz, r_b = 0, G = 0, G_h = 0.
    const double m = 0.4;
    const double omegaB = 2.0 * std::numbers::pi * 300.0;
    pme::bridge::ParallelModel model;
    REQUIRE(pme::bridge::massSpringDamperModel(m, m * omegaB * omegaB, 0.0, model) ==
            pme::Status::Ok);
    pme::BridgePointDesc desc;
    desc.thetaB = thetaB;
    REQUIRE(rig.point.configure(desc, model, kFs) == pme::Status::Ok);
    // Small initial displacement: 1/n^2 spectrum in the vertical, a smaller one in the horizontal.
    for (std::size_t s = 0; s < rig.segs.size(); ++s) {
        const double amp = (s == 0 ? 1.0e-4 : 7.0e-5);
        for (std::size_t i = 0; i < rig.segs[s].modeCount(); ++i) {
            const auto n = static_cast<double>(i + 1);
            rig.segs[s].qV()[i] = amp / (n * n);
            rig.segs[s].qH()[i] = 0.3 * amp / (n * n) * ((i % 3U) == 0U ? 1.0 : -1.0);
        }
    }
    const std::size_t stride = 100;
    std::vector<double> dev;
    Result res;
    res.modes = a.k.size() + b.k.size();
    rig.step();
    res.e0 = rig.energy();
    REQUIRE(res.e0 > 0.0);
    for (std::size_t n = 1; n < samples; ++n) {
        rig.step();
        if (n % stride == 0) {
            dev.push_back((rig.energy() - res.e0) / res.e0);
        }
    }
    // maximum deviation and least-squares slope against sample index
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    const auto cnt = static_cast<double>(dev.size());
    for (std::size_t i = 0; i < dev.size(); ++i) {
        const double x = static_cast<double>((i + 1) * stride) / 1.0e6;
        res.maxDeviation = std::max(res.maxDeviation, std::abs(dev[i]));
        sx += x;
        sy += dev[i];
        sxx += x * x;
        sxy += x * dev[i];
    }
    res.slopePer1e6 = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
    return res;
}

}  // namespace

TEST_CASE("A4-T1 two lossless strings on a lossless mass-spring bridge conserve energy") {
    const Result r = runLossless(0.0, 1000000);
    std::cout << "A4-T1 theta=0: E0=" << sci(r.e0, 6) << " max|dE|/E0=" << sci(r.maxDeviation)
              << " slope/1e6=" << sci(r.slopePer1e6) << " (" << r.modes << " modes)\n";
    CHECK(r.maxDeviation < 1e-6);
    CHECK(std::abs(r.slopePer1e6) < 1e-6);
}

TEST_CASE("A4-T1 energy conservation holds with the polarisations rotated by 0.1 rad") {
    const Result r = runLossless(0.1, 1000000);
    std::cout << "A4-T1 theta=0.1: E0=" << sci(r.e0, 6) << " max|dE|/E0=" << sci(r.maxDeviation)
              << " slope/1e6=" << sci(r.slopePer1e6) << " (" << r.modes << " modes)\n";
    CHECK(r.maxDeviation < 1e-6);
    CHECK(std::abs(r.slopePer1e6) < 1e-6);
}
