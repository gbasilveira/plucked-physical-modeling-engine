// SPDX-License-Identifier: MIT
// Covers: A7-R4, A7-R5, A7-R6
// GeometricSegment: the bridge hook and its order (SPEC-GEO "Transmission to the body", SPEC-EXEC
// step 2), the per-segment flags and bit-identity with the linear segment (A7-R5) and the
// geometric tier selector (A7-R6, D-13).
#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/modal_segment.hpp"
#include "pme/bridge/model.hpp"
#include "pme/instrument.hpp"
#include "pme/string/longitudinal_segment.hpp"
#include "pme/string/longitudinal_tier.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::BridgeEnd;
using pme::Status;
using pme::string::GeometricConfig;
using pme::string::GeometricQuality;
using pme::string::GeometricTier;

/// A bridge point that only records the calls the segment makes (instrumented order).
template <class S>
struct SpyPoint {
    std::vector<std::string> events;
    S wv{};
    S wh{};
    S fv{};
    S fh{};
    S extra{};
    int verticalCalls{0};
    [[nodiscard]] S wV() const { return wv; }
    [[nodiscard]] S wH() const { return wh; }
    void addForce(S v, S h) {
        events.emplace_back("addForce");
        fv = v;
        fh = h;
    }
    void addVerticalForce(S f) {
        events.emplace_back("addVerticalForce");
        extra = f;
        ++verticalCalls;
    }
    void advance() { events.emplace_back("advance"); }
};

pme::bridge::ParallelModel massModel(double mass, double hz) {
    pme::bridge::ParallelModel model;
    const double omega = 2.0 * kPi * hz;
    REQUIRE(pme::bridge::massSpringDamperModel(mass, mass * omega * omega, 0.0, model) ==
            Status::Ok);
    return model;
}

}  // namespace

TEST_CASE("A7-R4 the hook receives kappa_L F_L + kappa_T Delta T before the bridge advances") {
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.longitudinalModes = 5;
    cfg.kappaL = 0.026;
    cfg.kappaT = 0.05;
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), cfg);
    REQUIRE(b.status == Status::Ok);
    auto& seg = b.seg;
    trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.4, seg.qV(), seg.qH());
    seg.initialisePsi();
    SpyPoint<double> point;
    // Run until the longitudinal modes carry some motion.
    for (int s = 0; s < 300; ++s) {
        point.events.clear();
        const double total = seg.addBridgeForces(point);
        point.advance();
        seg.advance(0.0, 0.0);
        REQUIRE(point.events.size() == 3);
        CHECK(point.events[0] == "addForce");
        CHECK(point.events[1] == "addVerticalForce");
        CHECK(point.events[2] == "advance");
        CHECK(total == point.fv + point.extra);
    }
    // Recompute the hook value independently from the SPEC text.
    const double ea = b.params.ECore * kPi * b.params.dCore * b.params.dCore / 4.0;
    const double shape = std::sqrt(2.0 / b.modes.L);
    double sum = 0.0;
    double yx = 0.0;
    double zx = 0.0;
    for (std::size_t i = 0; i < b.modes.k.size(); ++i) {
        const double k = b.modes.k[i];
        sum += k * k * (seg.qV()[i] * seg.qV()[i] + seg.qH()[i] * seg.qH()[i]);
        const double cosL = std::cos(k * b.modes.L);
        yx += seg.qV()[i] * shape * k * cosL;
        zx += seg.qH()[i] * shape * k * cosL;
    }
    double xix = 0.0;
    for (std::size_t m = 0; m < seg.longitudinalCount(); ++m) {
        const double K = static_cast<double>(m + 1) * kPi / b.modes.L;
        xix += seg.xi()[m] * shape * K * std::cos(K * b.modes.L);
    }
    const double eaMinusT = ea - b.modes.tension;
    const double deltaT = eaMinusT * sum / (2.0 * b.modes.L);
    const double fL = ea * xix + 0.5 * eaMinusT * (yx * yx + zx * zx);
    const double expected = 0.026 * fL + 0.05 * deltaT;
    point.events.clear();
    static_cast<void>(seg.addBridgeForces(point));
    std::cout << "A7-R4 hook: kappa_L F_L = " << sci(0.026 * fL)
              << " N, kappa_T dT = " << sci(0.05 * deltaT) << " N, hook = " << sci(point.extra)
              << " N\n";
    CHECK(point.extra == doctest::Approx(expected).epsilon(1e-12));
    CHECK(seg.deltaT() == doctest::Approx(deltaT).epsilon(1e-12));
    CHECK(seg.longitudinalBridgeForce() == doctest::Approx(fL).epsilon(1e-12));
    CHECK(seg.verticalBridgeExtra() == point.extra);
    CHECK(std::abs(expected) > 0.0);
}

TEST_CASE("A7-R4 the hook reaches a real BridgePoint in the same sample") {
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.longitudinalModes = 5;
    cfg.kappaL = 0.026;
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), cfg);
    REQUIRE(b.status == Status::Ok);
    pme::bridge::BridgePoint<double> with;
    pme::bridge::BridgePoint<double> manual;
    const auto model = massModel(0.3, 300.0);
    REQUIRE(with.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    REQUIRE(manual.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    auto& seg = b.seg;
    trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.0, seg.qV(), seg.qH());
    seg.initialisePsi();
    for (int s = 0; s < 400; ++s) {
        const double wv = with.wV();
        const double wh = with.wH();
        // The segment adds its forces, the second point is fed the same values by hand.
        const double total = seg.addBridgeForces(with);
        manual.addForce(seg.forceV(wv) + seg.verticalBridgeExtra(), seg.forceH(wh));
        with.advance();
        manual.advance();
        CHECK(with.wV() == manual.wV());
        CHECK(total == seg.forceV(wv) + seg.verticalBridgeExtra());
        seg.advance(with.accelV(), with.accelH());
    }
    CHECK(std::abs(with.wV()) > 0.0);
}

TEST_CASE("A7-R5 a segment with every flag off is bit-identical to the linear ModalSegment") {
    // Same string, same pluck, same bridge: ModalSegment (A4) on one point, GeometricSegment on
    // another; every flag off (tension modulation off, M_L = 0, kappas 0 or ignored).
    const pme::StringParams params = plainSteel(0.65, 262.0, 1.0e-3, 0.4);
    const pme::StringModes m = compile(params);
    REQUIRE_FALSE(m.k.empty());
    pme::bridge::ModalSegment<float> linear;
    REQUIRE(linear.configure(m, BridgeEnd::AtL) == Status::Ok);
    pme::bridge::BridgePoint<float> pointA;
    pme::bridge::BridgePoint<float> pointB;
    const auto model = massModel(0.3, 300.0);
    REQUIRE(pointA.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    REQUIRE(pointB.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);

    GeometricConfig cfg;  // everything off
    cfg.kappaL = 0.026;   // transmissions set, but nothing to transmit
    cfg.kappaT = 0.05;
    Built<float> b;
    build(b, params, cfg);
    REQUIRE(b.status == Status::Ok);
    CHECK_FALSE(b.seg.usesSav());
    CHECK_FALSE(b.seg.tensionModulation());
    CHECK_FALSE(b.seg.longitudinal());
    trianglePluck<float>(m, 5.0e-3, 0.12 * m.L, 0.3, linear.qV(), linear.qH());
    trianglePluck<float>(m, 5.0e-3, 0.12 * m.L, 0.3, b.seg.qV(), b.seg.qH());

    bool identical = true;
    for (int s = 0; s < 3000; ++s) {
        pointA.addForce(linear.forceV(pointA.wV()), linear.forceH(pointA.wH()));
        pointA.advance();
        linear.advance(pointA.accelV(), pointA.accelH());

        static_cast<void>(b.seg.addBridgeForces(pointB));
        pointB.advance();
        b.seg.advance(pointB.accelV(), pointB.accelH());

        identical = identical && pointA.wV() == pointB.wV() && pointA.wH() == pointB.wH();
        for (std::size_t i = 0; i < m.k.size(); ++i) {
            identical =
                identical && linear.qV()[i] == b.seg.qV()[i] && linear.qH()[i] == b.seg.qH()[i];
        }
    }
    CHECK(identical);
    CHECK(std::abs(pointA.wV()) > 0.0);
}

TEST_CASE("A7-R5 the flags enable each part separately and a zero transmission calls no hook") {
    const pme::StringParams params = plainSteel(0.65, 262.0);
    struct Case {
        const char* name;
        bool tension;
        int ml;
        double kappaL;
        double kappaT;
        bool hookCalled;
    };
    const Case cases[] = {
        {"nothing", false, 0, 0.026, 0.05, false},
        {"tension only, kappa_T = 0", true, 0, 0.026, 0.0, false},
        {"tension only, kappa_T set", true, 0, 0.026, 0.05, true},
        {"longitudinal only, kappa_L = 0", false, 5, 0.0, 0.05, false},
        {"longitudinal only, kappa_L set", false, 5, 0.026, 0.05, true},
        {"both", true, 5, 0.026, 0.05, true},
    };
    for (const Case& c : cases) {
        CAPTURE(c.name);
        GeometricConfig cfg;
        cfg.tensionModulation = c.tension;
        cfg.longitudinalModes = c.ml;
        cfg.kappaL = c.kappaL;
        cfg.kappaT = c.kappaT;
        Built<double> b;
        build(b, params, cfg);
        REQUIRE(b.status == Status::Ok);
        CHECK(b.seg.tensionModulation() == c.tension);
        CHECK(b.seg.longitudinal() == (c.ml > 0));
        CHECK(b.seg.usesSav() == c.tension);
        CHECK(b.seg.longitudinalCount() == static_cast<std::size_t>(c.ml));
        trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.3, b.seg.qV(), b.seg.qH());
        b.seg.initialisePsi();
        SpyPoint<double> point;
        for (int s = 0; s < 50; ++s) {
            static_cast<void>(b.seg.addBridgeForces(point));
            b.seg.advance(0.0, 0.0);
        }
        CHECK((point.verticalCalls > 0) == c.hookCalled);
        if (!c.tension) {
            CHECK(b.seg.deltaT() == 0.0);
        }
        if (c.ml == 0) {
            CHECK(b.seg.longitudinalBridgeForce() == 0.0);
            CHECK(b.seg.longitudinalEnergy() == 0.0);
        }
    }
    // The description's flags map onto the config.
    pme::SegmentDesc desc;
    desc.tensionModulation = true;
    desc.longitudinalModes = 6;
    desc.kappaL = 0.02;
    desc.kappaT = 0.01;
    const GeometricConfig fromDesc = pme::string::geometricConfigFrom(desc, 18000.0);
    CHECK(fromDesc.tensionModulation);
    CHECK(fromDesc.longitudinalModes == 6);
    CHECK(fromDesc.kappaL == 0.02);
    CHECK(fromDesc.kappaT == 0.01);
    CHECK(fromDesc.fMax == 18000.0);
    CHECK(fromDesc.b1L == 0.0);  // longitudinal losses: internal parameters, default 0
    CHECK(fromDesc.b3L == 0.0);
    const pme::SegmentDesc defaults;
    CHECK_FALSE(pme::string::geometricConfigFrom(defaults, 1.0).tensionModulation);
    CHECK(pme::string::geometricConfigFrom(defaults, 1.0).longitudinalModes == 8);
}

TEST_CASE(
    "A7-R5 longitudinal modes without tension modulation are driven by the transverse slopes") {
    GeometricConfig cfg;
    cfg.longitudinalModes = 5;
    cfg.kappaL = 0.026;
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), cfg);
    REQUIRE(b.status == Status::Ok);
    trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.0, b.seg.qV(), b.seg.qH());
    double xiMax = 0.0;
    for (int s = 0; s < 500; ++s) {
        b.seg.advance(0.0, 0.0);
        for (std::size_t m = 0; m < b.seg.longitudinalCount(); ++m) {
            xiMax = std::max(xiMax, std::abs(b.seg.xi()[m]));
        }
    }
    CHECK(xiMax > 0.0);
    CHECK_FALSE(b.seg.usesSav());
}

TEST_CASE("A7-R5 configure rejects inconsistent input and leaves the segment unchanged") {
    const pme::StringParams params = plainSteel(0.65, 262.0);
    const pme::StringModes m = compile(params);
    const pme::string::BridgeCoupling good = couplingFor(m, BridgeEnd::AtL);
    REQUIRE(good.g.size() == m.k.size());
    pme::string::GeometricSegment<float> seg;
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    pme::string::BridgeCoupling shortG = good;
    shortG.g.pop_back();
    CHECK(seg.configure(m, params, shortG, cfg) == Status::InvalidArgument);
    CHECK(seg.configure(pme::StringModes{}, params, good, cfg) == Status::InvalidArgument);
    GeometricConfig negative = cfg;
    negative.longitudinalModes = -1;
    CHECK(seg.configure(m, params, good, negative) == Status::InvalidArgument);
    GeometricConfig badLoss = cfg;
    badLoss.longitudinalModes = 4;
    badLoss.b1L = -1.0;
    CHECK(seg.configure(m, params, good, badLoss) == Status::InvalidArgument);
    // EA - T <= 0 with a feature on.
    const pme::StringParams taut = plainSteel(0.65, 4500.0);
    const pme::StringModes mt = compile(taut);
    REQUIRE_FALSE(mt.k.empty());
    CHECK(seg.configure(mt, taut, couplingFor(mt, BridgeEnd::AtL), cfg) == Status::InvalidArgument);
    CHECK(seg.modeCount() == 0);
    // ... but a plain linear segment of that string is fine.
    CHECK(seg.configure(mt, taut, couplingFor(mt, BridgeEnd::AtL), GeometricConfig{}) ==
          Status::Ok);
}

TEST_CASE("A7-R6 the resolver maps Auto by the quality level and keeps explicit tiers") {
    using pme::string::resolveGeometricTier;
    CHECK(resolveGeometricTier(GeometricTier::Auto, GeometricQuality::Realtime) ==
          GeometricTier::Default);
    CHECK(resolveGeometricTier(GeometricTier::Auto, GeometricQuality::HighQuality) ==
          GeometricTier::Exact);
    CHECK(resolveGeometricTier(GeometricTier::Default, GeometricQuality::Realtime) ==
          GeometricTier::Default);
    CHECK(resolveGeometricTier(GeometricTier::Default, GeometricQuality::HighQuality) ==
          GeometricTier::Default);
    CHECK(resolveGeometricTier(GeometricTier::Exact, GeometricQuality::Realtime) ==
          GeometricTier::Exact);
    CHECK(resolveGeometricTier(GeometricTier::Exact, GeometricQuality::HighQuality) ==
          GeometricTier::Exact);
    // ICR-0002 values: Auto = 0, Default = 1, Exact = 2; Realtime = 0, HighQuality = 1.
    CHECK(static_cast<int>(GeometricTier::Auto) == 0);
    CHECK(static_cast<int>(GeometricTier::Default) == 1);
    CHECK(static_cast<int>(GeometricTier::Exact) == 2);
    CHECK(static_cast<int>(GeometricQuality::Realtime) == 0);
    CHECK(static_cast<int>(GeometricQuality::HighQuality) == 1);
}

TEST_CASE("A7-R6 the Exact tier reports NotImplemented until A10 provides it") {
    GeometricTier resolved = GeometricTier::Auto;
    CHECK(pme::string::selectGeometricTier(GeometricTier::Auto, GeometricQuality::Realtime,
                                           resolved) == Status::Ok);
    CHECK(resolved == GeometricTier::Default);
    CHECK(pme::string::selectGeometricTier(GeometricTier::Default, GeometricQuality::HighQuality,
                                           resolved) == Status::Ok);
    CHECK(resolved == GeometricTier::Default);
    CHECK(pme::string::selectGeometricTier(GeometricTier::Exact, GeometricQuality::Realtime,
                                           resolved) == Status::NotImplemented);
    CHECK(resolved == GeometricTier::Exact);
    CHECK(pme::string::selectGeometricTier(GeometricTier::Auto, GeometricQuality::HighQuality,
                                           resolved) == Status::NotImplemented);

    // Through the segment: only a segment with geometric features asks for the tier.
    const pme::StringParams params = plainSteel(0.65, 262.0);
    const pme::StringModes m = compile(params);
    const pme::string::BridgeCoupling coupling = couplingFor(m, BridgeEnd::AtL);
    for (const bool tension : {true, false}) {
        GeometricConfig cfg;
        cfg.tensionModulation = tension;
        cfg.longitudinalModes = tension ? 0 : 4;
        pme::string::GeometricSegment<float> seg;
        cfg.tier = GeometricTier::Exact;
        CHECK(seg.configure(m, params, coupling, cfg) == Status::NotImplemented);
        cfg.tier = GeometricTier::Auto;
        cfg.quality = GeometricQuality::HighQuality;
        CHECK(seg.configure(m, params, coupling, cfg) == Status::NotImplemented);
        cfg.quality = GeometricQuality::Realtime;
        CHECK(seg.configure(m, params, coupling, cfg) == Status::Ok);
        cfg.tier = GeometricTier::Default;
        cfg.quality = GeometricQuality::HighQuality;
        CHECK(seg.configure(m, params, coupling, cfg) == Status::Ok);
    }
    // A linear segment is unaffected by the tier (no other behaviour change).
    GeometricConfig linearCfg;
    linearCfg.tier = GeometricTier::Exact;
    pme::string::GeometricSegment<float> linear;
    CHECK(linear.configure(m, params, coupling, linearCfg) == Status::Ok);
}

TEST_CASE("A7-R3 the Taylor start gives the longitudinal modes u0 = -gamma f0 / 2") {
    GeometricConfig cfg;
    cfg.longitudinalModes = 5;
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), cfg);
    REQUIRE(b.status == Status::Ok);
    trianglePluck<double>(b.modes, 5.0e-3, 0.12 * b.modes.L, 0.0, b.seg.qV(), b.seg.qH());
    b.seg.startLongitudinal();
    const auto& lm = b.seg.longitudinalModes();
    std::vector<double> f0(b.seg.longitudinalCount());
    pme::string::longitudinalForce<double>(
        lm, std::span<const double>(b.seg.qV(), b.seg.modeCount()),
        std::span<const double>(b.seg.qH(), b.seg.modeCount()), f0);
    const double dt = 1.0 / kFs;
    for (std::size_t m = 0; m < f0.size(); ++m) {
        CHECK(b.seg.longitudinalUs()[m] ==
              doctest::Approx(-0.5 * lm.osc[m].gamma * f0[m]).epsilon(1e-14));
        CHECK(lm.osc[m].gamma == doctest::Approx(dt * dt).epsilon(1e-3));
    }
    b.seg.advance(0.0, 0.0);
    // After one step xi^1 is about the exact start value dt^2 f0 / 2.
    for (std::size_t m = 0; m < f0.size(); ++m) {
        CHECK(b.seg.xi()[m] == doctest::Approx(0.5 * dt * dt * f0[m]).epsilon(2e-3));
    }
}
