// SPDX-License-Identifier: MIT
// Covers: A2-R2
// Tuning curve (equal temperament, stretch, unison detune), tension for pitch and its inverse.
#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

#include "fixtures.hpp"
#include "pme/string/string_model.hpp"
#include "pme/test/tolerance.hpp"

namespace {
constexpr double kPi = std::numbers::pi;
}

TEST_CASE("A2-R2 tuningF1 at keys 21 69 108 with and without stretch and detune") {
    pme::TuningCurve plain;
    pme::TuningCurve stretched;
    stretched.stretchCents.assign(109, 0.0);
    stretched.stretchCents[21] = -12.5;
    stretched.stretchCents[69] = 0.0;
    stretched.stretchCents[108] = 31.0;
    for (const int key : {21, 69, 108}) {
        const double et = 440.0 * std::pow(2.0, (key - 69) / 12.0);
        PME_CHECK_REL(pme::string::tuningF1(plain, key, 0.0), et, 1e-14);
        const double st = stretched.stretchCents[static_cast<std::size_t>(key)];
        const double expected = et * std::pow(2.0, (st + 1.75) / 1200.0);
        PME_CHECK_REL(pme::string::tuningF1(stretched, key, 1.75), expected, 1e-14);
        PME_CHECK_REL(pme::string::tuningF1(plain, key, -3.0), et * std::pow(2.0, -3.0 / 1200.0),
                      1e-14);
    }
    CHECK(pme::string::tuningF1(plain, 69, 0.0) == 440.0);
    // Reference pitch other than 440 Hz.
    plain.referencePitch = 432.0;
    PME_CHECK_REL(pme::string::tuningF1(plain, 81, 0.0), 864.0, 1e-14);
}

TEST_CASE("A2-R2 stretch is zero beyond the vector and for key minus one") {
    pme::TuningCurve c;
    c.stretchCents = {10.0, 20.0};
    PME_CHECK_REL(pme::string::tuningF1(c, 1, 0.0),
                  440.0 * std::pow(2.0, -68.0 / 12.0) * std::pow(2.0, 20.0 / 1200.0), 1e-14);
    PME_CHECK_REL(pme::string::tuningF1(c, 2, 0.0), 440.0 * std::pow(2.0, -67.0 / 12.0), 1e-14);
    PME_CHECK_REL(pme::string::tuningF1(c, -1, 0.0), 440.0 * std::pow(2.0, -70.0 / 12.0), 1e-14);
}

TEST_CASE("A2-R2 tension for pitch and f1ForTension invert each other") {
    for (const auto& p : {pme::string_test::treble(), pme::string_test::mid(),
                          pme::string_test::bass()}) {
        const double rhoA = pme::string::linearDensity(p);
        const double EI = pme::string::bendingStiffness(p);
        const double T = 4.0 * p.L * p.L * rhoA * p.f1Target * p.f1Target -
                         kPi * kPi * EI / (p.L * p.L);
        PME_CHECK_REL(pme::string::tensionForPitch(p, p.f1Target), T, 1e-12);
        PME_CHECK_REL(pme::string::f1ForTension(p, T), p.f1Target, 1e-12);
        // The compiled table stores the same tension and B = pi^2 EI / (T L^2).
        pme::StringModes m;
        REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
        PME_CHECK_REL(m.tension, T, 1e-12);
        PME_CHECK_REL(m.rhoA, rhoA, 1e-14);
        PME_CHECK_REL(m.EI, EI, 1e-14);
        PME_CHECK_REL(m.inharmonicity, kPi * kPi * EI / (T * p.L * p.L), 1e-12);
    }
}

TEST_CASE("A2-R2 f1 target reproduces the first partial of the pinned table") {
    // omega_1^2 = (T k^2 + EI k^4) / rhoA with T from the formula gives exactly 2 pi f1.
    const auto p = pme::string_test::mid();
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    PME_CHECK_REL(m.omega[0], 2.0 * kPi * p.f1Target, 1e-13);
}
