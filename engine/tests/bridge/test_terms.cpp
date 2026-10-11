// SPDX-License-Identifier: MIT
// Covers: A4-R1
// compileBridgeTerms, the horizontal coefficients and bridgeForce (SPEC-BRG "Modal equation with a
// moving support", "Bridge force", "Contract").
#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/bridge/terms.hpp"
#include "test_support.hpp"

namespace {

using pme::BridgeEnd;
using pme::Status;
using namespace pme::bridge_test;

pme::StringModes midModes(double detuneH = 7.0) {
    pme::StringParams p = plainSteel(0.65, 262.0, false);
    p.detuneH = detuneH;
    return compile(p);
}

}  // namespace

TEST_CASE("A4-R1 g_n matches the lifting-function overlaps for both ends") {
    const pme::StringModes m = midModes();
    REQUIRE(m.k.size() > 20);
    for (const BridgeEnd end : {BridgeEnd::AtL, BridgeEnd::At0}) {
        pme::SegmentBridgeTerms t;
        REQUIRE(pme::compileBridgeTerms(m, end, t) == Status::Ok);
        CHECK(t.end == end);
        REQUIRE(t.g.size() == m.k.size());
        REQUIRE(t.a.size() == m.k.size());
        std::vector<double> g64;
        REQUIRE(pme::bridge::liftingOverlaps(m, end, g64) == Status::Ok);
        for (std::size_t i = 0; i < m.k.size(); ++i) {
            const auto n = static_cast<double>(i + 1);
            const double sign = (end == BridgeEnd::AtL && (i % 2U) == 1U) ? -1.0 : 1.0;
            const double expected = sign * std::sqrt(2.0 / m.L) / m.k[i];
            CHECK(std::abs(g64[i] - expected) <= 1e-12 * std::abs(expected));
            CHECK(t.g[i] == static_cast<float>(expected));  // float32 correctly rounded
            CHECK(n >= 1.0);
        }
    }
    // Signs alternate for the speaking end and are constant for the rear end.
    pme::SegmentBridgeTerms tL;
    pme::SegmentBridgeTerms t0;
    REQUIRE(pme::compileBridgeTerms(m, BridgeEnd::AtL, tL) == Status::Ok);
    REQUIRE(pme::compileBridgeTerms(m, BridgeEnd::At0, t0) == Status::Ok);
    CHECK(tL.g[0] > 0.0F);
    CHECK(tL.g[1] < 0.0F);
    CHECK(tL.g[2] > 0.0F);
    CHECK(t0.g[1] > 0.0F);
}

TEST_CASE("A4-R1 a_n uses the discrete omegaHat squared of the coupled polarisation") {
    const pme::StringModes m = midModes(7.0);
    pme::SegmentBridgeTerms t;
    REQUIRE(pme::compileBridgeTerms(m, BridgeEnd::AtL, t) == Status::Ok);
    std::vector<float> aH;
    REQUIRE(pme::bridge::horizontalForceCoefficients(m, BridgeEnd::AtL, aH) == Status::Ok);
    REQUIRE(aH.size() == t.a.size());
    CHECK(t.tOverL == doctest::Approx(m.tension / m.L).epsilon(1e-15));
    bool differs = false;
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const double g = (i % 2U == 1U ? -1.0 : 1.0) * std::sqrt(2.0 / m.L) / m.k[i];
        const double aV = m.rhoA * m.oscV[i].omegaHat2 * g;
        const double aHexp = m.rhoA * m.oscH[i].omegaHat2 * g;
        CHECK(t.a[i] == static_cast<float>(aV));
        CHECK(aH[i] == static_cast<float>(aHexp));
        // the continuous omega^2 would differ at the mode-count-dependent level; the discrete
        // value must be the one used
        const double aContinuous = m.rhoA * m.omega[i] * m.omega[i] * g;
        if (std::abs(aV - aContinuous) > 1e-9 * std::abs(aV)) {
            differs = true;
        }
        if (aH[i] != t.a[i]) {
            differs = true;
        }
    }
    CHECK(differs);
    // With zero detune the two polarisations coincide.
    const pme::StringModes m0 = midModes(0.0);
    pme::SegmentBridgeTerms t0;
    REQUIRE(pme::compileBridgeTerms(m0, BridgeEnd::At0, t0) == Status::Ok);
    std::vector<float> aH0;
    REQUIRE(pme::bridge::horizontalForceCoefficients(m0, BridgeEnd::At0, aH0) == Status::Ok);
    CHECK(aH0 == t0.a);
}

TEST_CASE("A4-R1 invalid inputs return InvalidArgument and leave out unchanged") {
    const pme::StringModes good = midModes();
    pme::SegmentBridgeTerms out;
    out.tOverL = 7.0;
    out.g = {1.0F};
    out.a = {2.0F};
    out.end = BridgeEnd::At0;

    auto expectInvalid = [&](const pme::StringModes& bad) {
        CHECK(pme::compileBridgeTerms(bad, BridgeEnd::AtL, out) == Status::InvalidArgument);
        CHECK(out.tOverL == 7.0);
        CHECK(out.g == std::vector<float>{1.0F});
        CHECK(out.a == std::vector<float>{2.0F});
        CHECK(out.end == BridgeEnd::At0);
    };
    expectInvalid(pme::StringModes{});
    {
        pme::StringModes b = good;
        b.k.pop_back();
        expectInvalid(b);
    }
    {
        pme::StringModes b = good;
        b.omega.pop_back();
        expectInvalid(b);
    }
    {
        pme::StringModes b = good;
        b.sigma.pop_back();
        expectInvalid(b);
    }
    {
        pme::StringModes b = good;
        b.oscV.pop_back();
        expectInvalid(b);
    }
    {
        pme::StringModes b = good;
        b.oscH.pop_back();
        expectInvalid(b);
    }
    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        pme::StringModes b = good;
        b.L = bad;
        expectInvalid(b);
        b = good;
        b.tension = bad;
        expectInvalid(b);
        b = good;
        b.rhoA = bad;
        expectInvalid(b);
        b = good;
        b.k[3] = bad;
        expectInvalid(b);
    }
    {
        pme::StringModes b = good;
        b.oscV[2].omegaHat2 = std::numeric_limits<double>::quiet_NaN();
        expectInvalid(b);
        b = good;
        b.oscH[2].omegaHat2 = std::numeric_limits<double>::infinity();
        expectInvalid(b);
        b = good;
        b.omega[1] = std::numeric_limits<double>::quiet_NaN();
        expectInvalid(b);
        b = good;
        b.sigma[1] = std::numeric_limits<double>::infinity();
        expectInvalid(b);
    }
    std::vector<float> aH{3.0F};
    CHECK(pme::bridge::horizontalForceCoefficients(pme::StringModes{}, BridgeEnd::AtL, aH) ==
          Status::InvalidArgument);
    CHECK(aH == std::vector<float>{3.0F});
    std::vector<double> g64{4.0};
    CHECK(pme::bridge::liftingOverlaps(pme::StringModes{}, BridgeEnd::AtL, g64) ==
          Status::InvalidArgument);
    CHECK(g64 == std::vector<double>{4.0});
    std::vector<double> a64{5.0};
    CHECK(pme::bridge::forceCoefficients(pme::StringModes{}, BridgeEnd::AtL,
                                         pme::bridge::Polarisation::Vertical,
                                         a64) == Status::InvalidArgument);
    CHECK(a64 == std::vector<double>{5.0});
    // Valid input overwrites out.
    CHECK(pme::compileBridgeTerms(good, BridgeEnd::AtL, out) == Status::Ok);
    CHECK(out.g.size() == good.k.size());
}

TEST_CASE("A4-R1 bridgeForce is one dot product plus the static term") {
    const pme::StringModes m = midModes();
    pme::SegmentBridgeTerms t;
    REQUIRE(pme::compileBridgeTerms(m, BridgeEnd::AtL, t) == Status::Ok);
    const std::size_t n = t.a.size();
    std::vector<float> q(n);
    std::vector<double> q64(n);
    for (std::size_t i = 0; i < n; ++i) {
        q[i] = 1e-4F * static_cast<float>(std::sin(1.7 * static_cast<double>(i + 1)));
        q64[i] = static_cast<double>(q[i]);
    }
    const float wb = 2.5e-4F;
    double ref = 0.0;
    double mag = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ref += static_cast<double>(t.a[i]) * q64[i];
        mag += std::abs(static_cast<double>(t.a[i]) * q64[i]);
    }
    const double refStatic = -t.tOverL * static_cast<double>(wb);
    const float f = pme::bridge::bridgeForce(t, q.data(), wb);
    // float32 accumulation of n products: error bound n * eps * sum |terms|
    const double bound = static_cast<double>(n) * 6e-8 * (mag + std::abs(refStatic));
    CHECK(std::abs(static_cast<double>(f) - (ref + refStatic)) <= bound);

    // Static term alone: q = 0 gives -(T/L) w_b.
    const std::vector<float> zero(n, 0.0F);
    const float fs = pme::bridge::bridgeForce(t, zero.data(), wb);
    CHECK(static_cast<double>(fs) ==
          doctest::Approx(-t.tOverL * static_cast<double>(wb)).epsilon(1e-6));
    // Dot product alone: w_b = 0.
    const float fd = pme::bridge::bridgeForce(t, q.data(), 0.0F);
    CHECK(std::abs(static_cast<double>(fd) - ref) <= static_cast<double>(n) * 6e-8 * mag);

    // The double instantiation with the same coefficients matches the float64 dot product.
    const auto fdbl = pme::bridge::bridgeForce<double>(t, q64.data(), static_cast<double>(wb));
    CHECK(std::abs(fdbl - (ref + refStatic)) <= 1e-12 * (mag + std::abs(refStatic)));
}
