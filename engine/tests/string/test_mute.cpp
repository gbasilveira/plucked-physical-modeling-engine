// SPDX-License-Identifier: MIT
// Covers: A2-R7
// Rear segments (BridgeEnd At0): tension from a prescribed T, and the mute helper.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include "fixtures.hpp"
#include "pme/osc.hpp"
#include "pme/string/string_model.hpp"
#include "pme/test/tolerance.hpp"

namespace {

bool bitEqual(const pme::OscCoeffs& a, const pme::OscCoeffs& b) {
    // Field-wise: the struct has padding bytes that memcmp would compare.
    return std::memcmp(&a.beta, &b.beta, sizeof(float)) == 0 &&
           std::memcmp(&a.kappa, &b.kappa, sizeof(float)) == 0 &&
           std::memcmp(&a.gamma, &b.gamma, sizeof(float)) == 0 &&
           std::memcmp(&a.omegaHat2, &b.omegaHat2, sizeof(double)) == 0 &&
           std::memcmp(&a.sigmaHat, &b.sigmaHat, sizeof(double)) == 0;
}

}  // namespace

TEST_CASE("A2-R7 a rear segment given the speaking tension reproduces it") {
    const auto speaking = pme::string_test::mid();
    pme::StringModes sp;
    REQUIRE(pme::compileString(speaking, 48000.0, 16000.0, {}, sp) == pme::Status::Ok);

    auto rear = speaking;  // same material and diameters, its own length L_d
    rear.L = 0.09;
    rear.bridgeEnd = pme::BridgeEnd::At0;
    rear.f1Target = pme::string::f1ForTension(rear, sp.tension);
    pme::StringModes rm;
    const std::array<double, 1> pts{0.03};
    REQUIRE(pme::compileString(rear, 48000.0, 16000.0, pts, rm) == pme::Status::Ok);
    PME_CHECK_REL(rm.tension, sp.tension, 1e-12);
    PME_CHECK_REL(rm.rhoA, sp.rhoA, 1e-14);
    PME_CHECK_REL(rm.EI, sp.EI, 1e-14);
    CHECK(rm.L == 0.09);
    // The rear string is short, so its fundamental is much higher than the speaking one.
    CHECK(rm.omega[0] > sp.omega[0]);
}

TEST_CASE("A2-R7 applyMute with m_d = 0 leaves the tables bit-identical") {
    auto p = pme::string_test::bass();
    p.detuneH = 3.0;
    const std::array<double, 2> pts{0.3, 0.9};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    const pme::StringModes before = m;
    CHECK(pme::string::applyMute(m, 0.0, 20.0, 3.0) == pme::Status::Ok);
    CHECK(m.sigma == before.sigma);
    CHECK(m.omega == before.omega);
    CHECK(m.k == before.k);
    CHECK(m.phiBar == before.phiBar);
    REQUIRE(m.oscV.size() == before.oscV.size());
    for (std::size_t i = 0; i < m.oscV.size(); ++i) {
        CHECK(bitEqual(m.oscV[i], before.oscV[i]));
        CHECK(bitEqual(m.oscH[i], before.oscH[i]));
    }
    // sigma_felt = 0 is also a no-op.
    CHECK(pme::string::applyMute(m, 1.0, 0.0, 3.0) == pme::Status::Ok);
    CHECK(m.sigma == before.sigma);
}

TEST_CASE("A2-R7 applyMute with m_d = 1 adds sigma_felt exactly and recompiles") {
    auto p = pme::string_test::mid();
    p.detuneH = 4.0;
    const std::array<double, 1> pts{0.2};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    const pme::StringModes before = m;
    CHECK(pme::string::applyMute(m, 1.0, 20.0, 4.0) == pme::Status::Ok);
    REQUIRE(m.sigma.size() == before.sigma.size());
    const double hs = std::exp2(4.0 / 1200.0);
    for (std::size_t i = 0; i < m.sigma.size(); ++i) {
        CHECK(m.sigma[i] == before.sigma[i] + 20.0);
        pme::OscCoeffs v;
        pme::OscCoeffs h;
        REQUIRE(pme::compileOscillator(m.omega[i], m.sigma[i], 48000.0, v) == pme::Status::Ok);
        REQUIRE(pme::compileOscillator(m.omega[i] * hs, m.sigma[i], 48000.0, h) ==
                pme::Status::Ok);
        CHECK(bitEqual(m.oscV[i], v));
        CHECK(bitEqual(m.oscH[i], h));
    }
    CHECK(m.omega == before.omega);
    CHECK(m.phiBar == before.phiBar);

    // Partial mute: sigma_mute = m_d sigma_felt.
    m = before;
    CHECK(pme::string::applyMute(m, 0.25, 20.0, 4.0) == pme::Status::Ok);
    CHECK(m.sigma[5] == before.sigma[5] + 5.0);
}

TEST_CASE("A2-R7 applyMute rejects bad arguments and overdamping and leaves the tables") {
    auto p = pme::string_test::bass();  // f1 = 27.5 Hz, omega_1 = 173 rad/s
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    const pme::StringModes before = m;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(pme::string::applyMute(m, -0.1, 20.0, 0.0) == pme::Status::InvalidArgument);
    CHECK(pme::string::applyMute(m, 1.1, 20.0, 0.0) == pme::Status::InvalidArgument);
    CHECK(pme::string::applyMute(m, nan, 20.0, 0.0) == pme::Status::InvalidArgument);
    CHECK(pme::string::applyMute(m, 0.5, -1.0, 0.0) == pme::Status::InvalidArgument);
    CHECK(pme::string::applyMute(m, 0.5, nan, 0.0) == pme::Status::InvalidArgument);
    CHECK(pme::string::applyMute(m, 0.5, 20.0, nan) == pme::Status::InvalidArgument);
    // sigma_felt above omega_1 overdamps mode 1: no mode survives.
    CHECK(pme::string::applyMute(m, 1.0, 1000.0, 0.0) == pme::Status::Rejected);
    CHECK(m.sigma == before.sigma);
    CHECK(m.k.size() == before.k.size());
    CHECK(m.oscV.size() == before.oscV.size());
}
