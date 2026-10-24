// SPDX-License-Identifier: MIT
// Covers: A2-R1, A2-T1
// Wound-string mass and stiffness against hand-worked values, plain strings, invalid inputs.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "fixtures.hpp"
#include "pme/string.hpp"
#include "pme/string/string_model.hpp"
#include "pme/test/tolerance.hpp"

namespace {

constexpr double kPi = std::numbers::pi;
using pme::string_test::plainSteel;
using pme::string_test::woundBass;

// Hand-worked SPEC-STR "Wound strings" with the formula written out term by term.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): dc, rhoC, dow, rhoW, eta
double handRhoA(double dc, double rhoC, double dow, double rhoW, double eta) {
    const double core = rhoC * dc * dc;                      // rho_c d_c^2
    const double wind = eta * rhoW * (dow * dow - dc * dc);  // eta_w rho_w (d_o^2 - d_c^2)
    return (kPi / 4.0) * (core + wind);
}
double handEI(double dc, double E, double eps) {
    return (1.0 + eps) * kPi * E * (dc * dc * dc * dc) / 64.0;
}

}  // namespace

TEST_CASE("A2-T1 plain steel string mass and stiffness") {
    const auto p = plainSteel(0.5, 440.0);
    // d = 1 mm: rhoA = pi/4 * 7850 * 1e-6 = 6.16573...e-3 kg/m; EI = pi * 2e11 * 1e-12 / 64.
    const double rhoA = kPi / 4.0 * 7850.0 * 1.0e-6;
    const double EI = kPi * 200.0e9 * 1.0e-12 / 64.0;
    PME_CHECK_REL(pme::string::linearDensity(p), rhoA, 1e-12);
    PME_CHECK_REL(pme::string::bendingStiffness(p), EI, 1e-12);
    PME_CHECK_REL(pme::string::linearDensity(p), 6.1654e-3, 1e-4);  // sanity of the hand value
    PME_CHECK_REL(pme::string::bendingStiffness(p), 9.8175e-3, 1e-4);
}

TEST_CASE("A2-R1 wound bass string mass and stiffness") {
    auto p = woundBass(1.9, 27.5);
    // d_c = 1.6 mm, d_o = 4.7 mm, eta_w = 0.85, eps_w = 0.
    const double rhoA = handRhoA(1.6e-3, 7850.0, 4.7e-3, 8960.0, 0.85);
    PME_CHECK_REL(pme::string::linearDensity(p), rhoA, 1e-12);
    PME_CHECK_REL(pme::string::bendingStiffness(p), handEI(1.6e-3, 200.0e9, 0.0), 1e-12);
    // Winding term worked numerically: d_o^2 - d_c^2 = 22.09e-6 - 2.56e-6 = 19.53e-6.
    PME_CHECK_REL(rhoA, kPi / 4.0 * (7850.0 * 2.56e-6 + 0.85 * 8960.0 * 19.53e-6), 1e-12);

    p.epsWind = 0.1;                                            // case c
    PME_CHECK_REL(pme::string::linearDensity(p), rhoA, 1e-12);  // eps_w does not change mass
    PME_CHECK_REL(pme::string::bendingStiffness(p), handEI(1.6e-3, 200.0e9, 0.1), 1e-12);
    PME_CHECK_REL(pme::string::bendingStiffness(p),
                  1.1 * pme::string::bendingStiffness(woundBass(1.9, 27.5)), 1e-14);
}

TEST_CASE("A2-T1 packing factors 0.75 and 0.9") {
    for (const double eta : {0.75, 0.9}) {
        auto p = woundBass(1.9, 27.5);
        p.packing = eta;
        PME_CHECK_REL(pme::string::linearDensity(p), handRhoA(1.6e-3, 7850.0, 4.7e-3, 8960.0, eta),
                      1e-12);
    }
}

TEST_CASE("A2-T1 plain strings ignore the winding density and packing") {
    auto a = plainSteel(0.5, 440.0);
    auto b = a;
    b.rhoWind = 12345.0;
    b.packing = 0.123;
    CHECK(pme::string::linearDensity(a) == pme::string::linearDensity(b));
    // Not even a non-physical packing matters when there is no winding.
    b.packing = 7.0;
    CHECK(pme::string::validate(b) == pme::Status::Ok);
}

TEST_CASE("A2-T1 invalid inputs return InvalidArgument and leave out unchanged") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::array<double, 1> pts{0.1};
    pme::StringModes sentinel;
    sentinel.tension = 99.0;
    sentinel.k = {1.0, 2.0};

    auto check = [&](const char* what, const pme::StringParams& p, double fs = 48000.0) {
        INFO(what);
        pme::StringModes out = sentinel;
        CHECK(pme::compileString(p, fs, 16000.0, pts, out) == pme::Status::InvalidArgument);
        CHECK(out.tension == 99.0);
        CHECK(out.k.size() == 2);
    };
    const auto base = woundBass(1.9, 27.5);
    {
        pme::StringModes out = sentinel;
        CHECK(pme::compileString(base, 48000.0, 16000.0, pts, out) == pme::Status::Ok);
        CHECK(out.tension != 99.0);
    }
    check("fs zero", base, 0.0);
    check("fs negative", base, -48000.0);
    check("fs nan", base, nan);
    check("fs inf", base, inf);
    auto mod = [&](auto&& f) {
        auto p = base;
        f(p);
        return p;
    };
    check("L zero", mod([](auto& p) { p.L = 0.0; }));
    check("L negative", mod([](auto& p) { p.L = -1.0; }));
    check("dCore zero", mod([](auto& p) { p.dCore = 0.0; }));
    check("rhoCore zero", mod([](auto& p) { p.rhoCore = 0.0; }));
    check("ECore zero", mod([](auto& p) { p.ECore = 0.0; }));
    check("dOuter < dCore", mod([](auto& p) { p.dOuter = 1.0e-3; }));
    check("wound rhoWind zero", mod([](auto& p) { p.rhoWind = 0.0; }));
    check("wound packing zero", mod([](auto& p) { p.packing = 0.0; }));
    check("wound packing above 1", mod([](auto& p) { p.packing = 1.01; }));
    check("epsWind negative", mod([](auto& p) { p.epsWind = -0.1; }));
    check("f1 zero", mod([](auto& p) { p.f1Target = 0.0; }));
    check("f1 negative", mod([](auto& p) { p.f1Target = -5.0; }));
    check("b1 negative", mod([](auto& p) { p.b1 = -1.0; }));
    check("b2 negative", mod([](auto& p) { p.b2 = -1.0; }));
    check("b3 negative", mod([](auto& p) { p.b3 = -1.0; }));
    check("override negative", mod([](auto& p) { p.sigmaOverride = {1.0, -1.0}; }));
    check("override nan", mod([&](auto& p) { p.sigmaOverride = {nan}; }));
    // Tension from the formula not positive: f1 far below the stiffness-only frequency.
    check("tension not positive", mod([](auto& p) { p.f1Target = 1.0e-3; }));
    // A registered point outside [0, L].
    for (const double x0 : {-0.01, 1.91, nan}) {
        pme::StringModes out = sentinel;
        const std::array<double, 1> bad{x0};
        CHECK(pme::compileString(base, 48000.0, 16000.0, bad, out) == pme::Status::InvalidArgument);
        CHECK(out.tension == 99.0);
    }
}

TEST_CASE("A2-T1 Rejected when mode 1 fails the bandwidth rule") {
    auto p = plainSteel(0.5, 440.0);
    pme::StringModes out;
    out.tension = 99.0;
    // fMax below the first partial.
    CHECK(pme::compileString(p, 48000.0, 100.0, {}, out) == pme::Status::Rejected);
    CHECK(out.tension == 99.0);
    // Overdamped fundamental.
    p.b1 = 1.0e5;
    CHECK(pme::compileString(p, 48000.0, 16000.0, {}, out) == pme::Status::Rejected);
    CHECK(out.tension == 99.0);
}
