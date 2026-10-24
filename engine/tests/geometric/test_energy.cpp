// SPDX-License-Identifier: MIT
// Covers: A7-R1, A7-R3
// Energy of the default geometric tier (SPEC-GEO "Energy balance of the default tier", SI-0015):
// the transverse energy (modes, bridge, psi including V_KC) is conserved and passive; the one-way
// longitudinal energy E_L is reported separately and scales as the square of the ratio.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::Status;
using pme::string::GeometricConfig;

struct Run {
    double e0{0.0};          ///< Transverse energy after the first step (J).
    double worstStep{0.0};   ///< Largest relative change of the transverse energy in one step.
    double worstDrift{0.0};  ///< Largest relative deviation from e0.
    double meanRatio{0.0};   ///< Time mean of E_L / E_transverse.
    double peakRatio{0.0};   ///< Peak of E_L / E_transverse.
    double meanEL{0.0};      ///< Time mean of E_L (J).
    std::size_t keptL{0};    ///< Longitudinal modes kept.
};

/// Lossless string on a lossless mass-spring bridge (m = 0.3 kg, 300 Hz), tension modulation and
/// longitudinal modes on, both transmissions off (so the bridge receives no extra work from the
/// one-way longitudinal field). `amp` is the pluck height.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test knobs
Run runEnergy(double amp, int samples, bool usePoint, bool slopeForce = false) {
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.longitudinalModes = 8;
    cfg.fMax = 20000.0;
    cfg.sav.driftRelaxation = false;
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), cfg);
    REQUIRE(b.status == Status::Ok);
    pme::bridge::BridgePoint<double> point;
    pme::bridge::ParallelModel model;
    const double omega = 2.0 * kPi * 300.0;
    REQUIRE(pme::bridge::massSpringDamperModel(0.3, 0.3 * omega * omega, 0.0, model) == Status::Ok);
    REQUIRE(point.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    auto& seg = b.seg;
    trianglePluck<double>(b.modes, amp, 0.12 * b.modes.L, 0.4, seg.qV(), seg.qH());
    seg.initialisePsi();

    Run run;
    run.keptL = seg.longitudinalCount();
    double prev = 0.0;
    double sumRatio = 0.0;
    double sumEL = 0.0;
    for (int n = 0; n < samples; ++n) {
        if (usePoint) {
            static_cast<void>(seg.addBridgeForces(point));
            if (slopeForce) {
                // Diagnostic only (SPEC issue): the transverse force of the extra tension at the
                // bridge, -Delta T y_x(L) on the vertical axis; SPEC-BRG's F_b has no such term.
                point.addVerticalForce(-seg.deltaT() *
                                       pme::string::slopeAtBridge<double>(
                                           seg.longitudinalModes(),
                                           std::span<const double>(seg.qV(), seg.modeCount())));
            }
            point.advance();
            seg.advance(point.accelV(), point.accelH());
        } else {
            seg.advance(0.0, 0.0);
        }
        double e = 0.0;
        if (usePoint) {
            e = point.modeEnergy() + seg.transverseEnergy(point.wV(), point.incrementV(),
                                                          point.wH(), point.incrementH());
        } else {
            e = seg.transverseEnergy(0.0, 0.0, 0.0, 0.0);
        }
        if (n == 0) {
            run.e0 = e;
        } else {
            run.worstStep = std::max(run.worstStep, std::abs(e - prev) / run.e0);
            run.worstDrift = std::max(run.worstDrift, std::abs(e - run.e0) / run.e0);
        }
        prev = e;
        const double el = seg.longitudinalEnergy();
        sumEL += el;
        sumRatio += el / e;
        run.peakRatio = std::max(run.peakRatio, el / e);
    }
    run.meanRatio = sumRatio / samples;
    run.meanEL = sumEL / samples;
    return run;
}

}  // namespace

TEST_CASE(
    "A7-R1 SI-0015 the transverse energy balance holds per step on a rigid bridge with "
    "longitudinal modes on") {
    const Run run = runEnergy(5.0e-3, 100000, false);
    std::cout << "SI-0015 transverse energy (rigid bridge): E0 = " << sci(run.e0)
              << " J, worst per-step relative change " << sci(run.worstStep)
              << ", worst drift over 1e5 samples " << sci(run.worstDrift) << ", kept " << run.keptL
              << " longitudinal modes\n";
    CHECK(run.worstStep <= 1e-10);
    CHECK(run.worstDrift <= 1e-10);
    CHECK(run.keptL == 5);
}

TEST_CASE("A7-R1 SI-0015 the transverse energy on a mass-spring bridge is reported and bounded") {
    // With a movable bridge the SPEC-BRG bridge force lacks the force of the extra tension on
    // the transverse axis, Delta T y_x(L) (a spec issue, docs/dev/geometric.md): the balance is
    // then not exact. The test reports the figures and keeps the defect visible; it bounds
    // nothing tighter than 1 percent.
    const Run plain = runEnergy(5.0e-3, 20000, true);
    const Run withSlope = runEnergy(5.0e-3, 20000, true, true);
    std::cout << "SI-0015 mass-spring bridge: worst drift over 2e4 samples "
              << sci(plain.worstDrift) << " as specified, " << sci(withSlope.worstDrift)
              << " with -Delta T y_x(L) added to the bridge force; worst per-step change "
              << sci(plain.worstStep) << " and " << sci(withSlope.worstStep) << "\n";
    CHECK(plain.worstDrift < 1e-2);
    CHECK(withSlope.worstDrift < plain.worstDrift);
}

TEST_CASE(
    "A7-R3 SI-0015 E_L is reported separately and its ratio to the transverse energy scales as "
    "amplitude squared") {
    const int samples = 9600;  // 0.2 s
    const Run lo = runEnergy(1.0e-3, samples, true);
    const Run hi = runEnergy(2.0e-3, samples, true);
    REQUIRE(lo.meanRatio > 0.0);
    const double ratioOfRatios = hi.meanRatio / lo.meanRatio;
    const double elRatio = hi.meanEL / lo.meanEL;
    // A larger pair, reported only: tension modulation shifts the driven pattern as amplitude^2.
    const Run lo2 = runEnergy(2.0e-3, samples, true);
    const Run hi2 = runEnergy(4.0e-3, samples, true);
    std::cout << "SI-0015 E_L / E_transverse: 1 mm mean " << sci(lo.meanRatio) << " (peak "
              << sci(lo.peakRatio) << "), 2 mm mean " << sci(hi.meanRatio) << " (peak "
              << sci(hi.peakRatio) << "); ratio of ratios " << fix(ratioOfRatios, 4)
              << " (expected 4), E_L ratio " << fix(elRatio, 3)
              << " (expected 16); 2 mm / 4 mm: " << sci(lo2.meanRatio) << " / "
              << sci(hi2.meanRatio) << ", ratio of ratios " << fix(hi2.meanRatio / lo2.meanRatio, 4)
              << ", mean E_L " << sci(lo.meanEL) << " and " << sci(hi.meanEL) << " J\n";
    CHECK(std::abs(ratioOfRatios / 4.0 - 1.0) <= 0.05);
    // E_L scales as the fourth power of the transverse amplitude.
    CHECK(std::abs(elRatio / 16.0 - 1.0) <= 0.1);
    CHECK(hi.meanEL > 0.0);
}
