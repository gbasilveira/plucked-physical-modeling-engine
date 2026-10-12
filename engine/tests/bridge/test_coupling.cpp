// SPDX-License-Identifier: MIT
// Covers: A4-R2, A4-R3, A4-R4
// The coupling order, unison sharing of one bridge point (speaking, rear and duplex segments) and
// the polarisation rotation (SPEC-BRG "Discrete coupling order", "Unison coupling",
// "Polarisation coupling").
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

#include "pme/bridge/bridge_point.hpp"
#include "pme/string/string_model.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::bridge_test;
using pme::BridgeEnd;
using pme::Status;
using pme::bridge::BridgePoint;
using pme::bridge::ParallelModel;
using pme::bridge::Stage;

constexpr double kPi = std::numbers::pi;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): mass, f_b, r_b in SPEC-BODY order
ParallelModel springBridge(double massKg = 0.4, double fb = 300.0, double r = 0.0) {
    ParallelModel model;
    const double w = 2.0 * kPi * fb;
    REQUIRE(pme::bridge::massSpringDamperModel(massKg, massKg * w * w, r, model) == Status::Ok);
    return model;
}

void excite(Rig<double>& rig, std::size_t seg, double amp, double ampH = 0.0) {
    for (std::size_t i = 0; i < rig.segs[seg].modeCount(); ++i) {
        const auto n = static_cast<double>(i + 1);
        rig.segs[seg].qV()[i] = amp / (n * n);
        rig.segs[seg].qH()[i] = ampH / (n * n);
    }
}

}  // namespace

TEST_CASE("A4-R2 the point follows add force then advance then read acceleration") {
    ParallelModel model = springBridge();
    BridgePoint<double> p;
    REQUIRE(p.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    CHECK(p.stage() == Stage::Collecting);
    std::vector<double> w{0.0, 0.0};  // w^{-1}, w^0
    std::vector<Stage> trace;
    std::vector<double> acc;
    for (int n = 0; n < 200; ++n) {
        p.addForce(n < 5 ? 20.0 : 0.0, 0.0);  // step 1: segments add forces
        trace.push_back(p.stage());
        p.advance();  // step 2: advance to w^{n+1}
        trace.push_back(p.stage());
        acc.push_back(p.accelV());  // step 3: segments read delta_tt w^n
        w.push_back(p.wV());
    }
    for (std::size_t i = 0; i < trace.size(); i += 2) {
        CHECK(trace[i] == Stage::Collecting);
        CHECK(trace[i + 1] == Stage::Advanced);
    }
    // delta_tt w^n = (w^{n+1} - 2 w^n + w^{n-1}) / dt^2 from the recorded displacement series
    double maxErr = 0.0;
    double scale = 0.0;
    for (std::size_t n = 0; n < acc.size(); ++n) {
        const double ref = (w[n + 2] - 2.0 * w[n + 1] + w[n]) * kFs * kFs;
        maxErr = std::max(maxErr, std::abs(acc[n] - ref));
        scale = std::max(scale, std::abs(ref));
    }
    CHECK(maxErr <= 1e-6 * scale);
    // A force added after advance acts on the next sample, not the one just formed.
    BridgePoint<double> q;
    REQUIRE(q.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    q.advance();
    q.addForce(100.0, 0.0);
    CHECK(q.stage() == Stage::Collecting);
    CHECK(q.wV() == 0.0);
    q.advance();
    CHECK(q.wV() != 0.0);
}

TEST_CASE("A4-R2 the rig applies the moving-support modal force from the same delta_tt w") {
    // A string mode with the exact recurrence checked by hand: one mode, one step.
    Rig<double> rig;
    const pme::StringModes m = compile(plainSteel(0.65, 262.0, true));
    REQUIRE(rig.addSegment(m, BridgeEnd::AtL) == Status::Ok);
    REQUIRE(rig.point.configure(pme::BridgePointDesc{}, springBridge(), kFs) == Status::Ok);
    excite(rig, 0, 1.0e-4);
    const std::vector<double> q0(rig.segs[0].qV(), rig.segs[0].qV() + rig.segs[0].modeCount());
    const double force = rig.segs[0].forceV(0.0);
    rig.step();
    // After step 1 the bridge has accel a = delta_tt w^0 and mode n moved by
    // u1 = -kappa q0 - gamma g_n a (u0 = 0).
    const double a = rig.point.accelV();
    CHECK(a != 0.0);
    const double dt = 1.0 / kFs;
    // bridge mode driven by the force of step 0: w^1 = Phi^2 dt^2 F / (1) for r = u = 0 initially
    // (gamma = dt^2 / (1 + sigmaHat dt) = dt^2 for sigma = 0)
    CHECK(rig.point.wV() == doctest::Approx(dt * dt * force / 0.4).epsilon(1e-12));
    CHECK(a == doctest::Approx(force / 0.4).epsilon(1e-12));
    const pme::StringModes& mm = m;
    for (std::size_t i = 0; i < 5; ++i) {
        const double kappa = mm.oscV[i].omegaHat2 * dt * dt;  // sigma = 0
        const double g = std::sqrt(2.0 / mm.L) * (i % 2U == 1U ? -1.0 : 1.0) / mm.k[i];
        const double u1 = -kappa * q0[i] - dt * dt * g * a;
        CHECK(rig.segs[0].qV()[i] == doctest::Approx(q0[i] + u1).epsilon(1e-12));
    }
}

TEST_CASE("A4-R3 three segments share one bridge displacement") {
    // Two speaking strings (unison) and one muted rear segment on the same point.
    pme::StringParams pa = plainSteel(0.65, 262.0, false);
    pa.detuneH = 5.0;
    pme::StringParams pb = plainSteel(0.65, 262.0 * std::pow(2.0, 2.0 / 1200.0), false);
    pb.detuneH = 5.0;
    pme::StringParams pr = plainSteel(0.12, 400.0, false);
    pr.bridgeEnd = BridgeEnd::At0;
    pr.detuneH = 5.0;
    const pme::StringModes a = compile(pa);
    const pme::StringModes b = compile(pb);
    pme::StringModes r = compile(pr);
    REQUIRE(!a.k.empty());
    REQUIRE(!b.k.empty());
    REQUIRE(!r.k.empty());
    // SI-0010 / A2 follow-up: the caller passes the string's detuneH to applyMute.
    REQUIRE(pme::string::applyMute(r, 0.8, 20.0, pr.detuneH) == Status::Ok);

    auto build = [&](const std::vector<std::size_t>& order, Rig<double>& rig) {
        const std::array<const pme::StringModes*, 3> ms{&a, &b, &r};
        const std::array<BridgeEnd, 3> ends{BridgeEnd::AtL, BridgeEnd::AtL, BridgeEnd::At0};
        for (const auto i : order) {
            REQUIRE(rig.addSegment(*ms[i], ends[i]) == Status::Ok);
        }
        REQUIRE(rig.point.configure(pme::BridgePointDesc{}, springBridge(0.4, 300.0, 15.0), kFs) ==
                Status::Ok);
        for (std::size_t s = 0; s < order.size(); ++s) {
            excite(rig, s, 1.0e-4 * (1.0 + 0.3 * static_cast<double>(order[s])),
                   0.2e-4 * (1.0 + static_cast<double>(order[s])));
        }
    };
    Rig<double> fwd1;
    Rig<double> fwd2;
    Rig<double> rev;
    build({0, 1, 2}, fwd1);
    build({0, 1, 2}, fwd2);
    build({2, 1, 0}, rev);

    double wMax = 0.0;
    double maxRelDiff = 0.0;
    for (int n = 0; n < 4000; ++n) {
        // the point's total force is the segment-index-ordered sum of the segment forces
        const double wv = fwd1.point.wV();
        const double wh = fwd1.point.wH();
        double fv = 0.0;
        double fh = 0.0;
        for (const auto& s : fwd1.segs) {
            fv += s.forceV(wv);
            fh += s.forceH(wh);
        }
        fwd1.step();
        fwd2.step();
        rev.step();
        // reconstruct the displacement from the summed force through a reference point
        if (n == 0) {
            const double dt = 1.0 / kFs;
            // first step of the bridge mode: w^1 = gamma F / m with gamma = dt^2 / (1 + tanh(sigma
            // dt))
            const double gamma = dt * dt / (1.0 + std::tanh(15.0 / (2.0 * 0.4) * dt));
            CHECK(fwd1.point.wV() == doctest::Approx(gamma * fv / 0.4).epsilon(1e-12));
            CHECK(fwd1.point.wH() == 0.0);
            CHECK(fh !=
                  0.0);  // horizontal force exists but the bridge (theta = 0, G_h = 0) is rigid
        }
        // identical order: bit-identical
        CHECK(fwd1.point.wV() == fwd2.point.wV());
        CHECK(fwd1.point.accelV() == fwd2.point.accelV());
        wMax = std::max(wMax, std::abs(fwd1.point.wV()));
        // different attaching order: equal to rounding of the changed summation order
        maxRelDiff = std::max(maxRelDiff, std::abs(fwd1.point.wV() - rev.point.wV()));
    }
    CHECK(wMax > 0.0);
    CHECK(maxRelDiff <= 1e-9 * wMax);
}

TEST_CASE("A4-R3 a muted rear segment on the bridge only loses energy") {
    pme::StringParams pa = plainSteel(0.65, 262.0, true);
    pme::StringParams pr = plainSteel(0.12, 400.0, true);
    pr.bridgeEnd = BridgeEnd::At0;
    pr.detuneH = 3.0;
    const pme::StringModes a = compile(pa);
    pme::StringModes r = compile(pr);
    pme::StringModes rLossless = r;
    REQUIRE(pme::string::applyMute(r, 1.0, 20.0, pr.detuneH) == Status::Ok);

    auto run = [&](const pme::StringModes& rear) {
        Rig<double> rig;
        REQUIRE(rig.addSegment(a, BridgeEnd::AtL) == Status::Ok);
        REQUIRE(rig.addSegment(rear, BridgeEnd::At0) == Status::Ok);
        REQUIRE(rig.point.configure(pme::BridgePointDesc{}, springBridge(), kFs) == Status::Ok);
        excite(rig, 0, 1.0e-4, 3.0e-5);
        rig.step();
        const double e0 = rig.energy();
        double maxRise = 0.0;
        double last = e0;
        for (int n = 1; n < 96000; ++n) {
            rig.step();
            if (n % 100 == 0) {
                last = rig.energy();
                maxRise = std::max(maxRise, (last - e0) / e0);
            }
        }
        return std::pair<double, double>{(last - e0) / e0, maxRise};
    };
    const auto lossless = run(rLossless);
    const auto muted = run(r);
    CHECK(std::abs(lossless.first) < 1e-9);  // rear segment included in the conserved energy
    CHECK(muted.first < -1e-3);              // damping drains the shared bridge
    CHECK(muted.second < 1e-9);              // and the energy never rises
}

TEST_CASE("A4-R4 theta_b = 0 separates the polarisations bit for bit") {
    const pme::StringModes m = compile(plainSteel(0.65, 262.0, false));
    const double gH = 5.0e-4;  // horizontal conductance: the h polarisation damps
    auto make = [&](Rig<double>& rig, double ampV, double ampH) {
        REQUIRE(rig.addSegment(m, BridgeEnd::AtL) == Status::Ok);
        pme::BridgePointDesc d;
        d.conductanceH = gH;
        REQUIRE(rig.point.configure(d, springBridge(0.4, 300.0, 10.0), kFs) == Status::Ok);
        excite(rig, 0, ampV, ampH);
    };
    Rig<double> both;
    Rig<double> vOnly;
    Rig<double> hOnly;
    make(both, 1.0e-4, 6.0e-5);
    make(vOnly, 1.0e-4, 0.0);
    make(hOnly, 0.0, 6.0e-5);
    std::size_t differing = 0;
    for (int n = 0; n < 20000; ++n) {
        both.step();
        vOnly.step();
        hOnly.step();
        // the vertical polarisation never sees the horizontal one, and vice versa
        if (both.point.wV() != vOnly.point.wV()) {
            ++differing;
        }
        if (both.point.wH() != hOnly.point.wH()) {
            ++differing;
        }
        for (std::size_t i = 0; i < both.segs[0].modeCount(); ++i) {
            if (both.segs[0].qV()[i] != vOnly.segs[0].qV()[i] ||
                both.segs[0].qH()[i] != hOnly.segs[0].qH()[i]) {
                ++differing;
            }
        }
    }
    CHECK(differing == 0);
    CHECK(std::abs(hOnly.point.wH()) > 0.0);
    CHECK(vOnly.point.wH() == 0.0);
    CHECK(hOnly.point.wV() == 0.0);
}

TEST_CASE("A4-R4 theta_b not zero moves energy from the vertical to the horizontal polarisation") {
    const pme::StringModes m = compile(plainSteel(0.65, 262.0, true));
    auto horizontalEnergyShare = [&](double theta) {
        Rig<double> rig;
        REQUIRE(rig.addSegment(m, BridgeEnd::AtL) == Status::Ok);
        pme::BridgePointDesc d;
        d.thetaB = theta;
        REQUIRE(rig.point.configure(d, springBridge(0.4, 300.0, 0.0), kFs) == Status::Ok);
        excite(rig, 0, 1.0e-4, 0.0);
        double hMax = 0.0;
        double vMax = 0.0;
        for (int n = 0; n < 48000; ++n) {
            rig.step();
            for (std::size_t i = 0; i < 3; ++i) {
                hMax = std::max(hMax, std::abs(rig.segs[0].qH()[i]));
                vMax = std::max(vMax, std::abs(rig.segs[0].qV()[i]));
            }
        }
        return hMax / vMax;
    };
    CHECK(horizontalEnergyShare(0.0) == 0.0);
    CHECK(horizontalEnergyShare(0.1) > 1e-3);
    CHECK(horizontalEnergyShare(0.3) > horizontalEnergyShare(0.1));
}

TEST_CASE("A4-R4 the perpendicular axis is a pure conductance G_h in the rotated frame") {
    ParallelModel rigid;
    REQUIRE(pme::bridge::conductanceModel(0.0, rigid) == Status::Ok);
    const double gH = 3.0e-3;
    const double dt = 1.0 / kFs;
    {
        BridgePoint<double> p;
        pme::BridgePointDesc d;
        d.conductanceH = gH;
        REQUIRE(p.configure(d, rigid, kFs) == Status::Ok);
        for (int n = 1; n <= 100; ++n) {
            p.addForce(0.0, 4.0);
            p.advance();
            CHECK(p.wH() == doctest::Approx(n * dt * gH * 4.0).epsilon(1e-12));
            CHECK(p.wV() == 0.0);
        }
        CHECK(p.conductanceDissipatedEnergy() > 0.0);
    }
    {
        // theta = pi/2: a vertical force is purely perpendicular and appears as vertical motion
        BridgePoint<double> p;
        pme::BridgePointDesc d;
        d.thetaB = kPi / 2.0;
        d.conductanceH = gH;
        REQUIRE(p.configure(d, rigid, kFs) == Status::Ok);
        for (int n = 1; n <= 100; ++n) {
            p.addForce(4.0, 0.0);
            p.advance();
        }
        CHECK(p.wV() == doctest::Approx(100 * dt * gH * 4.0).epsilon(1e-12));
        CHECK(std::abs(p.wH()) < 1e-15);
        CHECK(p.wPerpendicular() == doctest::Approx(100 * dt * gH * 4.0).epsilon(1e-12));
        CHECK(p.wParallel() == 0.0);
    }
}

TEST_CASE("A4-R4 the rotation is the standard R of SI-0019") {
    // Asymmetric case: theta_b = 0.3, a purely vertical force on a rigid parallel axis with a
    // perpendicular conductance. With R = [[c, -s], [s, c]]: F_perp = s F_v, so
    // w_perp = n dt G_h s F_v and [w_v; w_h] = R^T [0; w_perp] = [s w_perp; c w_perp].
    // The transpose would give F_perp = -s F_v and a negative w_perp.
    ParallelModel rigid;
    REQUIRE(pme::bridge::conductanceModel(0.0, rigid) == Status::Ok);
    const double theta = 0.3;
    const double gH = 3.0e-3;
    const double dt = 1.0 / kFs;
    pme::BridgePointDesc d;
    d.thetaB = theta;
    d.conductanceH = gH;
    BridgePoint<double> p;
    REQUIRE(p.configure(d, rigid, kFs) == Status::Ok);
    for (int n = 0; n < 50; ++n) {
        p.addForce(4.0, 0.0);
        p.advance();
    }
    const double wPerp = 50 * dt * gH * std::sin(theta) * 4.0;
    CHECK(p.wPerpendicular() == doctest::Approx(wPerp).epsilon(1e-12));
    CHECK(p.wV() == doctest::Approx(std::sin(theta) * wPerp).epsilon(1e-12));
    CHECK(p.wH() == doctest::Approx(std::cos(theta) * wPerp).epsilon(1e-12));
    // A horizontal force: F_perp = c F_h, F_par = -s F_h.
    BridgePoint<double> q;
    REQUIRE(q.configure(d, rigid, kFs) == Status::Ok);
    q.addForce(0.0, 2.0);
    q.advance();
    CHECK(q.wPerpendicular() == doctest::Approx(dt * gH * std::cos(theta) * 2.0).epsilon(1e-12));
    CHECK(q.wV() == doctest::Approx(std::sin(theta) * q.wPerpendicular()).epsilon(1e-12));
}
