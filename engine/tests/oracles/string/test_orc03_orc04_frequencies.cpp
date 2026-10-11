// SPDX-License-Identifier: MIT
// Covers: ORC-03, ORC-04
//
// ORC-03 Stiff-string frequencies and ORC-04 Tension for pitch (SPEC-ORC; SPEC-STR "Modes",
// "Wound strings", "Tension for pitch", "Truncation").
//
// ORC-03 setup: compiled table vs n f0 sqrt(1 + B n^2). Expected: the exact formula. Tolerance:
// 10^-9 relative.
// ORC-04 setup: closed-form T, then the first partial. Expected: the f1 target. Tolerance: 10^-9
// relative.
//
// Method (independent of the engine sources, public header pme/string.hpp only):
//  - rhoA, EI, T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2, B = pi^2 EI / (T L^2) and f0 = sqrt(T/rhoA)/2L
//    are evaluated in float64 in the test from the SPEC-STR formulas (string_oracle_common.hpp),
//    not through the engine's linearDensity, bendingStiffness or f1ForTension helpers.
//  - ORC-03: for each string of oracleStrings() (plain steel 110 Hz, plain steel 2 kHz with
//    B about 1e-2, wound bass 27.5 Hz with eps_w = 0.1 and losses, and a thin string with B about
//    1e-5), pinned, fs = 48 kHz, f_max = +inf, every kept mode satisfies
//    |omega[n-1] / 2 pi - n f0 sqrt(1 + B n^2)| <= 1e-9 n f0 sqrt(1 + B n^2). The mode count is
//    checked against the bandwidth rule bounds that need no engine value: every n with
//    f_n <= 20 kHz must be kept (omega_d dt <= 0.95 pi holds up to 0.475 fs = 22.8 kHz) and no n
//    with f_n above 0.475 fs plus 1 % may be kept. The table k[n-1] = n pi / L is checked at
//    1e-12 and tension, inharmonicity at 1e-9 as well.
//  - ORC-03 clamped: Boundary::Clamped multiplies every omega_n by Fletcher's factor
//    1 + 2 sqrt(B)/pi + 4 B/pi^2 with the B above (SPEC-STR "Modes"); tension is still computed
//    from f1Target by the pinned formula (A2-R3; if the implementer scales T for clamped ends
//    this test fails and the report quotes the measured values).
//  - ORC-04: for each string and for f1 at keys 21, 45, 69, 108 (equal temperament, A4 = 440 Hz,
//    computed here), T is evaluated in closed form; combinations with T <= 0 are skipped (they are
//    non-physical, the engine must answer InvalidArgument; checked too). Compiled tension must
//    match T and omega[0] / 2 pi must match the f1 target, both at 1e-9 relative.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <pme/status.hpp>
#include <pme/string.hpp>
#include <pme/test/tolerance.hpp>
#include <string>
#include <vector>

#include "string_oracle_common.hpp"

namespace {

using pme::oracle::str::kInf;
using pme::oracle::str::kPi;
using pme::oracle::str::StringSpec;

constexpr double kFs = 48000.0;
constexpr double kTol = 1e-9;  // SPEC-ORC tolerance of ORC-03 and ORC-04, relative

/// Checks one compiled table against n f0 sqrt(1 + B n^2) (times `factor`). Returns the worst
/// relative error and sets worstN.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double checkTable(const StringSpec& spec, const pme::StringModes& modes, double f0, double B,
                  double factor, int& worstN) {
    namespace str = pme::oracle::str;
    const std::size_t N = modes.omega.size();
    double worst = 0.0;
    worstN = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const int n = static_cast<int>(i) + 1;
        const double expected = factor * str::stiffPartial(f0, B, n);
        const double actual = modes.omega[i] / (2.0 * kPi);
        const double err = pme::test::relative_error(actual, expected);
        if (err > worst) {
            worst = err;
            worstN = n;
        }
        INFO(spec.name << ": n = " << n << " f_engine = " << actual << " f_formula = " << expected
                       << " rel err = " << err);
        CHECK(err <= kTol);
    }
    return worst;
}

/// Mode-count bounds from the bandwidth rule (SPEC-OSC "Bandwidth" and SPEC-STR "Truncation") that
/// can be stated without the engine: all n with f_n <= 20 kHz kept, none above 0.475 fs + 1 %.
void checkModeCount(const StringSpec& spec, const pme::StringModes& modes, double f0, double B,
                    double factor) {
    namespace str = pme::oracle::str;
    int lowCount = 0;
    int highCount = 0;
    for (int n = 1; n < 100000; ++n) {
        const double f = factor * str::stiffPartial(f0, B, n);
        if (f <= 20000.0) {
            lowCount = n;
        }
        if (f <= 0.475 * kFs * 1.01) {
            highCount = n;
        } else {
            break;
        }
    }
    INFO(spec.name << ": N = " << modes.omega.size() << " lower bound " << lowCount
                   << " upper bound " << highCount);
    CHECK(static_cast<int>(modes.omega.size()) >= lowCount);
    CHECK(static_cast<int>(modes.omega.size()) <= highCount);
    CHECK(modes.k.size() == modes.omega.size());
    CHECK(modes.sigma.size() == modes.omega.size());
    CHECK(modes.oscV.size() == modes.omega.size());
}

}  // namespace

TEST_CASE("ORC-03 stiff-string frequencies pinned") {
    namespace str = pme::oracle::str;
    for (const StringSpec& spec : str::oracleStrings()) {
        const double T = str::tensionForPitch(spec, spec.f1);
        REQUIRE(T > 0.0);
        const double B = str::inharmonicityOf(spec, T);
        const double f0 = str::f0Of(spec, T);

        pme::StringModes modes;
        const pme::Status st =
            pme::compileString(str::toParams(spec, spec.f1), kFs, kInf, {}, modes);
        INFO(spec.name << " B = " << B << " f0 = " << f0 << " T = " << T);
        REQUIRE(pme::oracle::str::is(st, pme::Status::Ok));
        REQUIRE(!modes.omega.empty());

        int worstN = 0;
        const double worst = checkTable(spec, modes, f0, B, 1.0, worstN);
        checkModeCount(spec, modes, f0, B, 1.0);
        PME_CHECK_REL(modes.tension, T, kTol);
        PME_CHECK_REL(modes.inharmonicity, B, kTol);
        for (std::size_t i = 0; i < modes.k.size(); ++i) {
            PME_CHECK_REL(modes.k[i], static_cast<double>(i + 1) * kPi / spec.L, 1e-12);
        }
        str::report("ORC-03 pinned " + spec.name + ": B = " + str::sci(B) +
                    ", N = " + std::to_string(modes.omega.size()) +
                    ", worst rel err = " + str::sci(worst) + " at n = " + std::to_string(worstN));
    }
}

TEST_CASE("ORC-03 clamped stiff-string frequencies with Fletcher correction") {
    namespace str = pme::oracle::str;
    for (const StringSpec& spec : str::oracleStrings()) {
        const double T = str::tensionForPitch(spec, spec.f1);
        REQUIRE(T > 0.0);
        const double B = str::inharmonicityOf(spec, T);
        const double f0 = str::f0Of(spec, T);
        const double factor = str::fletcherFactor(B);

        pme::StringParams params = str::toParams(spec, spec.f1);
        params.boundary = pme::Boundary::Clamped;
        pme::StringModes modes;
        const pme::Status st = pme::compileString(params, kFs, kInf, {}, modes);
        INFO(spec.name << " B = " << B << " Fletcher factor = " << factor);
        REQUIRE(pme::oracle::str::is(st, pme::Status::Ok));
        REQUIRE(!modes.omega.empty());

        int worstN = 0;
        const double worst = checkTable(spec, modes, f0, B, factor, worstN);
        checkModeCount(spec, modes, f0, B, factor);
        str::report("ORC-03 clamped " + spec.name + ": factor = " + str::sci(factor - 1.0) +
                    " above 1, N = " + std::to_string(modes.omega.size()) +
                    ", worst rel err = " + str::sci(worst) + " at n = " + std::to_string(worstN));
    }
}

TEST_CASE("ORC-04 tension for pitch at keys 21 45 69 108 and the string's own f1") {
    namespace str = pme::oracle::str;
    const std::array<int, 4> keys{21, 45, 69, 108};
    int checked = 0;
    double worstT = 0.0;
    double worstF = 0.0;
    for (const StringSpec& spec : str::oracleStrings()) {
        std::vector<double> targets{spec.f1};
        for (const int key : keys) {
            targets.push_back(str::keyFrequency(key));
        }
        for (const double f1 : targets) {
            const double T = str::tensionForPitch(spec, f1);  // SPEC-STR "Tension for pitch"
            INFO(spec.name << " f1 target = " << f1 << " closed-form T = " << T);
            pme::StringModes modes;
            const pme::Status st =
                pme::compileString(str::toParams(spec, f1), kFs, kInf, {}, modes);
            if (T <= 0.0) {
                // Non-physical: the engine must refuse (ADR-0006 InvalidArgument), not compile.
                CHECK(pme::oracle::str::is(st, pme::Status::InvalidArgument));
                continue;
            }
            // f1 above 0.475 fs would be Rejected by the bandwidth rule; keys up to 108 are
            // 4.19 kHz, far below it.
            REQUIRE(pme::oracle::str::is(st, pme::Status::Ok));
            REQUIRE(!modes.omega.empty());
            const double errT = pme::test::relative_error(modes.tension, T);
            const double f1Engine = modes.omega[0] / (2.0 * kPi);
            const double errF = pme::test::relative_error(f1Engine, f1);
            INFO("tension = " << modes.tension << " first partial = " << f1Engine);
            CHECK(errT <= kTol);
            CHECK(errF <= kTol);
            worstT = std::max(worstT, errT);
            worstF = std::max(worstF, errF);
            ++checked;
        }
    }
    CHECK(checked >= 10);  // enough combinations are physical for the check to mean something
    str::report("ORC-04: " + std::to_string(checked) +
                " (string, f1) combinations, worst tension " + "rel err = " + str::sci(worstT) +
                ", worst first-partial rel err = " + str::sci(worstF));
}
