// SPDX-License-Identifier: MIT
// Covers: ORC-11, ORC-12
//
// ORC-11 Longitudinal forcing (SPEC-ORC): "Pair sum vs quadrature of
//   -(EA - T)/(2 rhoA) Int y_x^2 chi_m' dx" -- same value to 1e-9 relative.
// ORC-12 Tension modulation (SPEC-ORC): "sum k_n^2 q_n^2 vs Int y_x^2 dx" -- same value to 1e-12
//   relative. Also Delta T = (EA - T) S / (2 L) against the test's own EA and T.
//
// Both oracles use the same states: a plain steel string (0.65 m, d = 1 mm, f1 = 110 Hz) and the
// wound bass string of the A2 oracles (1.2 m, core 1.6 mm, f1 = 27.5 Hz), N = 60 transverse modes,
// random float32 states drawn from a fixed-seed SplitMix64 (amplitudes of a forte note: modal
// displacement of order 1 mm / n), vertical and horizontal polarisation filled independently, and
// every longitudinal mode m = 1..16. The sample rate is 192 kHz so that the bandwidth rule keeps
// all 16 longitudinal modes (the plain string's 16th is at 62 kHz); the value of f_m^L does not
// depend on it.
//
// Expected values (geometric_oracle_common.hpp), none from the engine:
//   * f_m^L by composite Gauss-Legendre quadrature (32 points, 48 and 96 panels; the difference
//     is the quadrature's own error and is required below 1e-13 of the largest |f_m^L|);
//   * S by the same quadrature of y_x^2 + z_x^2, and sum k_n^2 (q_v^2 + q_h^2) in float64;
//   * Delta T = (EA - T) S / (2 L) with EA = E_c pi d_c^2 / 4, T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2
//     (SPEC-STR "Tension for pitch"), from the string description, not from the compiled tables
//     (which are only cross-checked).
// The code under test is reached through the rig of geometric_rig.hpp (GeoRig::longitudinalForce,
// S, deltaT); its backend is chosen in CMakeLists.txt.
//
// Normalisation check of the SPEC pair sum (see "ORC-11 SPEC pair-sum prefactor ..."): the
// -(EA - T) K_m / (2 rhoA sqrt(2 L)) prefactor and the delta pairs reproduce the quadrature to
// round-off, so SPEC-GEO and SPEC-ORC agree on sign and constant.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "geometric_oracle_common.hpp"
#include "geometric_rig.hpp"
#include "pme/test/tolerance.hpp"

namespace {

namespace geo = pme::oracle::geo;
namespace str = pme::oracle::str;
using geo::kPi;

constexpr int kModes = 60;             // transverse modes N
constexpr int kLongModes = 16;         // every longitudinal mode m <= 16
constexpr double kFs = 192000.0;       // Hz: keeps all 16 longitudinal modes of both strings
constexpr double kAmplitude = 1.0e-3;  // m, forte-note modal displacement scale
constexpr double kOrc11Tol = 1.0e-9;   // SPEC-ORC row ORC-11, relative to the largest |f_m^L|
constexpr double kOrc12Tol = 1.0e-12;  // SPEC-ORC row ORC-12, relative
constexpr double kQuadTol = 1.0e-13;   // the reference quadrature's own error (panel halving)

struct Fixture {
    str::StringSpec spec;
    pme::StringParams params;
    pme::StringModes modes;
    double ea{};
    double tension{};
    double rhoA{};
};

Fixture makeFixture(std::size_t specIndex) {
    Fixture f;
    f.spec = str::oracleStrings().at(specIndex);
    f.ea = geo::coreEA(f.spec);
    f.tension = str::tensionForPitch(f.spec, f.spec.f1);
    f.rhoA = str::linearDensity(f.spec);
    f.params = str::toParams(f.spec, f.spec.f1);
    // fMax between partial 60 and 61 keeps exactly N = 60 modes.
    const double f0 = str::f0Of(f.spec, f.tension);
    const double b = str::inharmonicityOf(f.spec, f.tension);
    const double fMax = str::stiffPartial(f0, b, kModes) + 0.5 * f0;
    REQUIRE(str::is(pme::compileString(f.params, kFs, fMax, {}, f.modes), pme::Status::Ok));
    REQUIRE(f.modes.omega.size() == static_cast<std::size_t>(kModes));
    // Cross-check the inputs the test derived itself against the compiled tables (A2 is verified
    // by ORC-03 and ORC-04; this only guards the setup).
    CHECK(pme::test::relative_error(f.modes.tension, f.tension) <= 1.0e-9);
    CHECK(pme::test::relative_error(f.modes.rhoA, f.rhoA) <= 1.0e-12);
    return f;
}

geo::GeoRig makeRig(const Fixture& f, const geo::ModalStates& s) {
    geo::GeoSetup setup;
    setup.modes = f.modes;
    setup.params = f.params;
    setup.longitudinalModes = kLongModes;
    setup.fs = kFs;
    geo::GeoRig rig(setup);
    REQUIRE(rig.ok());
    REQUIRE(rig.longitudinalKept() == kLongModes);
    REQUIRE(rig.transverseModes() >= static_cast<std::size_t>(kModes));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kModes); ++i) {
        rig.qV()[i] = s.qv[i];
        rig.qH()[i] = s.qh[i];
    }
    return rig;
}

void runOrc11(std::size_t specIndex, bool vertical, bool horizontal, std::uint64_t seed) {
    const Fixture f = makeFixture(specIndex);
    const geo::ModalStates s =
        geo::randomStates(kModes, f.spec.L, seed, kAmplitude, vertical, horizontal);
    // Expected value: the quadrature.
    double quadError = 0.0;
    const std::vector<double> expected = geo::quadratureForce(
        f.spec.L, f.ea, f.tension, f.rhoA, s.qv, s.qh, kLongModes, quadError, nullptr);
    CHECK(quadError <= kQuadTol);
    double largest = 0.0;
    for (const double v : expected) {
        largest = std::max(largest, std::fabs(v));
    }
    REQUIRE(largest > 0.0);
    // Engine.
    geo::GeoRig rig = makeRig(f, s);
    std::vector<double> got(static_cast<std::size_t>(kLongModes), 0.0);
    rig.longitudinalForce(got);
    double worst = 0.0;
    std::size_t worstMode = 0;
    for (std::size_t m = 0; m < got.size(); ++m) {
        const double err = std::fabs(got[m] - expected[m]) / largest;
        if (err > worst) {
            worst = err;
            worstMode = m + 1;
        }
        INFO("mode " << m + 1 << ": engine " << got[m] << " quadrature " << expected[m]);
        CHECK(err <= kOrc11Tol);
    }
    str::report("ORC-11 " + f.spec.name + (vertical ? " v" : "") + (horizontal ? " h" : "") +
                ": worst |f_m - quadrature| / max|f| = " + str::sci(worst) +
                " at m = " + std::to_string(worstMode) + " (largest |f| = " + str::sci(largest) +
                " 1/s^2 m, quadrature self-error " + str::sci(quadError) + ")");
}

void runOrc12(std::size_t specIndex, bool vertical, bool horizontal, std::uint64_t seed) {
    const Fixture f = makeFixture(specIndex);
    const geo::ModalStates s =
        geo::randomStates(kModes, f.spec.L, seed, kAmplitude, vertical, horizontal);
    double quadError = 0.0;
    double sQuad = 0.0;
    static_cast<void>(
        geo::quadratureForce(f.spec.L, f.ea, f.tension, f.rhoA, s.qv, s.qh, 1, quadError, &sQuad));
    CHECK(quadError <= kQuadTol);
    REQUIRE(sQuad > 0.0);
    // The modal sum written out in the test (SPEC-ORC row), against the quadrature.
    const double sModal = geo::specS(f.spec.L, s.qv, s.qh);
    CHECK(pme::test::relative_error(sModal, sQuad) <= kOrc12Tol);
    // Engine.
    geo::GeoRig rig = makeRig(f, s);
    const double sEngine = rig.S();
    const double errS = pme::test::relative_error(sEngine, sQuad);
    CHECK(errS <= kOrc12Tol);
    // Delta T = (EA - T) S / (2 L), own EA and T.
    const double dtExpected = (f.ea - f.tension) * sQuad / (2.0 * f.spec.L);
    const double errDt = pme::test::relative_error(rig.deltaT(), dtExpected);
    CHECK(errDt <= kOrc12Tol);
    str::report("ORC-12 " + f.spec.name + (vertical ? " v" : "") + (horizontal ? " h" : "") +
                ": S relative error " + str::sci(errS) + " (modal sum vs quadrature " +
                str::sci(pme::test::relative_error(sModal, sQuad)) + "), Delta T relative error " +
                str::sci(errDt) + ", Delta T = " + str::sci(dtExpected) +
                " N (T = " + str::sci(f.tension) + " N, EA = " + str::sci(f.ea) + " N)");
}

}  // namespace

TEST_CASE("ORC-11 SPEC pair-sum prefactor agrees with the quadrature (formula check)") {
    // Independent of the engine: the SPEC-GEO pair sum with prefactor
    // -(EA - T) K_m / (2 rhoA sqrt(2 L)) against the SPEC-ORC quadrature, on both strings. A
    // constant factor or a sign between the two SPEC statements would show here and is then a
    // spec issue, not something to adapt in either side.
    for (const std::size_t index : {std::size_t{0}, std::size_t{2}}) {
        const Fixture f = makeFixture(index);
        const geo::ModalStates s =
            geo::randomStates(kModes, f.spec.L, 0xA7A7A7ULL + index, kAmplitude, true, true);
        double quadError = 0.0;
        const std::vector<double> quad = geo::quadratureForce(
            f.spec.L, f.ea, f.tension, f.rhoA, s.qv, s.qh, kLongModes, quadError, nullptr);
        const std::vector<double> pair =
            geo::specPairSumForce(f.spec.L, f.ea, f.tension, f.rhoA, s.qv, s.qh, kLongModes);
        double largest = 0.0;
        for (const double v : quad) {
            largest = std::max(largest, std::fabs(v));
        }
        double worst = 0.0;
        for (std::size_t m = 0; m < quad.size(); ++m) {
            worst = std::max(worst, std::fabs(pair[m] - quad[m]) / largest);
        }
        CHECK(quadError <= kQuadTol);
        CHECK(worst <= kOrc11Tol);
        // The ratio of the two over all modes: exactly 1, so no hidden constant.
        double num = 0.0;
        double den = 0.0;
        for (std::size_t m = 0; m < quad.size(); ++m) {
            num += pair[m] * quad[m];
            den += quad[m] * quad[m];
        }
        CHECK(std::fabs(num / den - 1.0) <= 1.0e-12);
        str::report("ORC-11 prefactor check, " + f.spec.name + ": worst " + str::sci(worst) +
                    ", least-squares ratio - 1 = " + str::sci(num / den - 1.0));
    }
}

TEST_CASE("ORC-11 longitudinal forcing of the plain string with both polarisations and 16 modes") {
    runOrc11(0, true, true, 0xC0FFEE11ULL);
}

TEST_CASE("ORC-11 longitudinal forcing of the wound string with both polarisations and 16 modes") {
    runOrc11(2, true, true, 0xC0FFEE12ULL);
}

TEST_CASE("ORC-11 longitudinal forcing of the wound string with the vertical polarisation only") {
    // The horizontal polarisation adds its own pair sum (SPEC-GEO sums over P); with it empty the
    // result must be the vertical one alone.
    runOrc11(2, true, false, 0xC0FFEE13ULL);
}

TEST_CASE("ORC-11 longitudinal forcing of the plain string with the horizontal polarisation only") {
    runOrc11(0, false, true, 0xC0FFEE14ULL);
}

TEST_CASE("ORC-12 tension modulation S and Delta T of the plain string with both polarisations") {
    runOrc12(0, true, true, 0xC0FFEE21ULL);
}

TEST_CASE("ORC-12 tension modulation S and Delta T of the wound string with both polarisations") {
    runOrc12(2, true, true, 0xC0FFEE22ULL);
}

TEST_CASE("ORC-12 tension modulation S and Delta T of the wound string with the vertical only") {
    runOrc12(2, true, false, 0xC0FFEE23ULL);
}
