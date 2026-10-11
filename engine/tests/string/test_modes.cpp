// SPDX-License-Identifier: MIT
// Covers: A2-R3, A2-R4, A2-R5, A2-T2
// Mode frequencies (pinned and clamped), losses and the override table, truncation by the
// bandwidth rule for both polarisations, and the mode counts at 44.1, 48 and 96 kHz.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>

#include "fixtures.hpp"
#include "pme/osc.hpp"
#include "pme/string/string_model.hpp"
#include "pme/test/tolerance.hpp"

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kInf = std::numeric_limits<double>::infinity();
using pme::string_test::bass;
using pme::string_test::mid;
using pme::string_test::treble;

// Independent expected N: loop upward over the SPEC formulas in the n^2 w0^2 (1 + B n^2) form
// (the implementation uses the T k^2 + EI k^4 form), stop at the first failure.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): fs, fMax
int expectedN(const pme::StringParams& p, double fs, double fMax) {
    const double rhoA = pme::string::linearDensity(p);
    const double EI = pme::string::bendingStiffness(p);
    const double T =
        4.0 * p.L * p.L * rhoA * p.f1Target * p.f1Target - kPi * kPi * EI / (p.L * p.L);
    const double w0 = kPi / p.L * std::sqrt(T / rhoA);
    const double B = kPi * kPi * EI / (T * p.L * p.L);
    const double dt = 1.0 / fs;
    int n = 0;
    for (int m = 1; m < 100000; ++m) {
        const auto nn = static_cast<double>(m);
        const double w = nn * w0 * std::sqrt(1.0 + B * nn * nn);
        const double sigma = p.b1 + p.b2 * std::sqrt(w) + p.b3 * w * w;
        bool ok = true;
        for (const double scale : {1.0, std::pow(2.0, p.detuneH / 1200.0)}) {
            const double wp = w * scale;
            if (sigma >= wp) {
                ok = false;
                continue;
            }
            const double wd = std::sqrt((wp - sigma) * (wp + sigma));
            if (wd * dt > 0.95 * kPi || wd / (2.0 * kPi) > fMax) {
                ok = false;
            }
        }
        if (!ok) {
            break;
        }
        n = m;
    }
    return n;
}

}  // namespace

TEST_CASE("A2-T2 mode counts match the bandwidth rule at 44.1 48 and 96 kHz") {
    struct Named {
        const char* name{};
        pme::StringParams p{};
    };
    const std::array<Named, 3> strings{Named{"treble", treble()}, Named{"mid", mid()},
                                       Named{"bass", bass()}};
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        for (const double fMax : {16000.0, kInf}) {
            for (const auto& s : strings) {
                INFO(s.name << " fs=" << fs << " fMax=" << fMax);
                const int expected = expectedN(s.p, fs, fMax);
                pme::StringModes m;
                REQUIRE(pme::compileString(s.p, fs, fMax, {}, m) == pme::Status::Ok);
                CHECK(pme::string::modeCount(s.p, fs, fMax) == expected);
                CHECK(static_cast<int>(m.k.size()) == expected);
                CHECK(m.omega.size() == m.k.size());
                CHECK(m.sigma.size() == m.k.size());
                CHECK(m.oscV.size() == m.k.size());
                CHECK(m.oscH.size() == m.k.size());
                MESSAGE("N = " << expected << " (" << std::string(s.name) << ", fs " << fs
                               << ", fMax " << fMax << ")");
            }
        }
    }
}

TEST_CASE("A2-T2 f_max binds") {
    const auto p = mid();
    const int n = pme::string::modeCount(p, 48000.0, 5000.0);
    CHECK(n == expectedN(p, 48000.0, 5000.0));
    // Mode n+1 is above 5 kHz, mode n is not (damped frequency, SPEC-OSC).
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 5000.0, {}, m) == pme::Status::Ok);
    CHECK(m.omega.back() / (2.0 * kPi) <= 5000.0 * 1.0000001);
    CHECK(n < pme::string::modeCount(p, 48000.0, kInf));
}

TEST_CASE("A2-T2 the 0.95 pi limit binds") {
    const auto p = treble();
    const int n = pme::string::modeCount(p, 44100.0, kInf);
    CHECK(n == expectedN(p, 44100.0, kInf));
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 44100.0, kInf, {}, m) == pme::Status::Ok);
    CHECK(m.omega.back() / (2.0 * kPi) <= 0.475 * 44100.0);
    // Mode n+1 (formula) is beyond 0.475 fs, mode n is not.
    const double w0 = kPi / p.L * std::sqrt(m.tension / m.rhoA);
    const double np = static_cast<double>(n) + 1.0;
    CHECK(np * w0 * std::sqrt(1.0 + m.inharmonicity * np * np) / (2.0 * kPi) > 0.475 * 44100.0);
    MESSAGE("N (treble, 44.1 kHz, fMax inf) = " << n);
}

TEST_CASE("A2-T2 a large b3 overdamps a high mode") {
    auto p = mid();
    p.b3 = 1.0e-5;  // sigma >= omega once omega >= 1/b3 = 1e5 rad/s (15.9 kHz)
    const int n = pme::string::modeCount(p, 96000.0, kInf);
    CHECK(n == expectedN(p, 96000.0, kInf));
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 96000.0, kInf, {}, m) == pme::Status::Ok);
    CHECK(m.omega.back() < 1.0e5);
    CHECK(m.sigma.back() < m.omega.back());
    // Without the loss, the Nyquist margin would allow many more modes.
    CHECK(n < pme::string::modeCount(mid(), 96000.0, kInf));
}

TEST_CASE("A2-T2 a horizontal detune of 5 cents makes the horizontal polarisation bind") {
    auto p = mid();
    // f_max just above mode 40 and 0.1 % above it: the vertical mode passes, the +5 cent mode
    // (0.29 % higher) does not.
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, kInf, {}, m) == pme::Status::Ok);
    const double f40 = m.omega[39] / (2.0 * kPi);
    const double fMax = f40 * 1.001;
    CHECK(pme::string::modeCount(p, 48000.0, fMax) >= 40);
    p.detuneH = 5.0;
    const int n = pme::string::modeCount(p, 48000.0, fMax);
    CHECK(n == expectedN(p, 48000.0, fMax));
    CHECK(n == 39);
    pme::StringModes h;
    REQUIRE(pme::compileString(p, 48000.0, fMax, {}, h) == pme::Status::Ok);
    CHECK(h.k.size() == 39);
    // A negative detune lowers the horizontal modes: the vertical polarisation binds.
    p.detuneH = -5.0;
    CHECK(pme::string::modeCount(p, 48000.0, fMax) >= 40);
    MESSAGE("N (detuneH +5 cents, fMax " << fMax << ") = " << n);
}

TEST_CASE("A2-R5 horizontal coefficients use the detuned frequency and the same sigma") {
    auto p = mid();
    p.detuneH = 7.5;
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    const double scale = std::exp2(7.5 / 1200.0);
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        pme::OscCoeffs v;
        pme::OscCoeffs h;
        REQUIRE(pme::compileOscillator(m.omega[i], m.sigma[i], 48000.0, v) == pme::Status::Ok);
        REQUIRE(pme::compileOscillator(m.omega[i] * scale, m.sigma[i], 48000.0, h) ==
                pme::Status::Ok);
        CHECK(m.oscV[i].beta == v.beta);
        CHECK(m.oscV[i].kappa == v.kappa);
        CHECK(m.oscV[i].omegaHat2 == v.omegaHat2);
        CHECK(m.oscH[i].kappa == h.kappa);
        CHECK(m.oscH[i].omegaHat2 == h.omegaHat2);
        CHECK(m.oscH[i].sigmaHat == h.sigmaHat);
    }
    // With no detune the polarisations are identical.
    p.detuneH = 0.0;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        CHECK(m.oscH[i].kappa == m.oscV[i].kappa);
        CHECK(m.oscH[i].beta == m.oscV[i].beta);
    }
}

TEST_CASE("A2-R3 pinned frequencies follow n w0 sqrt(1 + B n^2)") {
    for (const auto& p : {treble(), mid(), bass()}) {
        pme::StringModes m;
        REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
        const double w0 = kPi / p.L * std::sqrt(m.tension / m.rhoA);
        for (std::size_t i = 0; i < m.k.size(); ++i) {
            const auto n = static_cast<double>(i + 1);
            PME_CHECK_REL(m.k[i], n * kPi / p.L, 1e-15);
            PME_CHECK_REL(m.omega[i], n * w0 * std::sqrt(1.0 + m.inharmonicity * n * n), 1e-13);
        }
    }
}

TEST_CASE("A2-R3 clamped frequencies equal the pinned ones times Fletcher's factor") {
    for (const auto& base : {treble(), mid(), bass()}) {
        auto pinned = base;
        pinned.detuneH = 0.0;
        auto clamped = base;
        clamped.boundary = pme::Boundary::Clamped;
        pme::StringModes a;
        pme::StringModes c;
        REQUIRE(pme::compileString(pinned, 48000.0, 16000.0, {}, a) == pme::Status::Ok);
        REQUIRE(pme::compileString(clamped, 48000.0, 16000.0, {}, c) == pme::Status::Ok);
        const double B = a.inharmonicity;
        const double factor = 1.0 + 2.0 * std::sqrt(B) / kPi + 4.0 * B / (kPi * kPi);
        CHECK(factor > 1.0);
        const std::size_t n = std::min(a.omega.size(), c.omega.size());
        REQUIRE(n >= 2);
        for (std::size_t i = 0; i < n; ++i) {
            PME_CHECK_REL(c.omega[i], a.omega[i] * factor, 1e-14);
            CHECK(c.k[i] == a.k[i]);  // shapes stay pinned
        }
        // Tension and B are those of the description, not corrected.
        CHECK(c.tension == a.tension);
        CHECK(c.inharmonicity == a.inharmonicity);
    }
}

TEST_CASE("A2-R4 losses follow b1 + b2 sqrt(omega) + b3 omega^2") {
    auto p = mid();
    p.b1 = 0.7;
    p.b2 = 2.0e-3;
    p.b3 = 3.0e-11;
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const double w = m.omega[i];
        PME_CHECK_REL(m.sigma[i], 0.7 + 2.0e-3 * std::sqrt(w) + 3.0e-11 * w * w, 1e-14);
    }
    // Clamped strings use the corrected frequency.
    p.boundary = pme::Boundary::Clamped;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    const double w = m.omega[3];
    PME_CHECK_REL(m.sigma[3], 0.7 + 2.0e-3 * std::sqrt(w) + 3.0e-11 * w * w, 1e-14);
}

TEST_CASE("A2-R4 override table shorter equal and longer than N") {
    const auto base = mid();
    pme::StringModes ref;
    REQUIRE(pme::compileString(base, 48000.0, 16000.0, {}, ref) == pme::Status::Ok);
    const std::size_t n = ref.k.size();
    for (const std::size_t size : {n - 5, n, n + 10}) {
        auto p = base;
        for (std::size_t i = 0; i < size; ++i) {
            p.sigmaOverride.push_back(2.0 + 0.01 * static_cast<double>(i));
        }
        pme::StringModes m;
        REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
        REQUIRE(m.k.size() == n);
        for (std::size_t i = 0; i < n; ++i) {
            if (i < size) {
                CHECK(m.sigma[i] == 2.0 + 0.01 * static_cast<double>(i));  // used exactly
            } else {
                CHECK(m.sigma[i] == ref.sigma[i]);  // the formula for higher n
            }
        }
    }
    // An override of zero is allowed (no loss).
    auto p = base;
    p.sigmaOverride = {0.0};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, {}, m) == pme::Status::Ok);
    CHECK(m.sigma[0] == 0.0);
}
