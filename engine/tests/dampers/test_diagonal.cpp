// SPDX-License-Identifier: MIT
// Covers: A6-R1, A6-T3
// The diagonal damper: coefficient formulas, vertical polarisation only, ramped refresh, the
// overdamping check, and damping against pedal depth.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <numbers>
#include <cmath>
#include <vector>

#include "fixtures.hpp"
#include "pme/dampers/controller.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/osc/ramp.hpp"

namespace {

using namespace pme;
using namespace pme::dmp;
using pme::dmp_test::examplePad;
using pme::dmp_test::kFs;

double relErr(double a, double b) { return std::fabs(a - b) / std::max(std::fabs(b), 1e-300); }

}  // namespace

TEST_CASE("A6-R1 sigma and omega squared follow the SPEC-DMP formulas") {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    REQUIRE(m.omega.size() > 30);
    const DamperParams p = examplePad();
    DiagonalDamper d;
    REQUIRE(d.configure(m, p) == Status::Ok);
    CHECK(d.modes() == m.omega.size());

    double worstSigma = 0.0;
    double worstOmega = 0.0;
    for (double e : {0.0, 0.1, 0.5, 1.0}) {
        for (std::size_t i = 0; i < m.omega.size(); ++i) {
            const double n = static_cast<double>(i + 1);
            // Direct from SPEC-STR "Forces and observations" and SPEC-DMP "Diagonal".
            const double phi = std::sqrt(2.0 / m.L) * std::sin(n * std::numbers::pi * p.xRatio) *
                               string::windowFactor(string::WindowKind::RaisedCosine, m.k[i], p.width);
            const double sigma = m.sigma[i] + e * p.Rd * phi * phi / (2.0 * m.rhoA);
            const double omega2 = m.omega[i] * m.omega[i] + e * p.Kd * phi * phi / m.rhoA;
            worstSigma = std::max(worstSigma, relErr(d.sigmaAt(i, e), sigma));
            worstOmega = std::max(worstOmega, relErr(d.omegaAt(i, e) * d.omegaAt(i, e), omega2));
            osc::OscCoeffsF64 c;
            osc::OscCoeffsF64 ref;
            REQUIRE(d.coefficients(i, e, c) == Status::Ok);
            REQUIRE(osc::compileOscillatorF64(std::sqrt(omega2), sigma, m.fs, ref) == Status::Ok);
            CHECK(relErr(c.beta, ref.beta) < 1e-12);
            CHECK(relErr(c.kappa, ref.kappa) < 1e-12);
            CHECK(relErr(c.gamma, ref.gamma) < 1e-12);
        }
    }
    MESSAGE("worst relative error sigma " << worstSigma << ", omega^2 " << worstOmega);
    CHECK(worstSigma < 1e-12);
    CHECK(worstOmega < 1e-12);
    // The float32 shapes of A2 agree with the float64 copy.
    std::vector<float> f32(m.omega.size());
    string::windowedShapes(m, p.xRatio * m.L, string::WindowKind::RaisedCosine, p.width, f32);
    for (std::size_t i = 0; i < f32.size(); ++i) {
        CHECK(std::fabs(static_cast<double>(f32[i]) - d.phiBar()[i]) <=
              1e-6 * std::fabs(d.phiBar()[i]) + 1e-12);
    }
    // Out-of-range mode index.
    osc::OscCoeffsF64 c;
    CHECK(d.coefficients(m.omega.size(), 0.5, c) == Status::InvalidArgument);
}

TEST_CASE("A6-R1 only the vertical polarisation is touched and e = 0 is the string") {
    StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    m.oscH.shrink_to_fit();
    const auto oscHBefore = m.oscH;
    const auto oscVBefore = m.oscV;
    DiagonalDamper d;
    REQUIRE(d.configure(m, examplePad()) == Status::Ok);
    osc::ModeBank bank;
    bank.resize(m.omega.size());
    REQUIRE(d.initialise(0.0, bank, 0) == Status::Ok);
    for (std::size_t i = 0; i < m.omega.size(); ++i) {
        // At e = 0 the coefficients are the string's own vertical coefficients, bit for bit.
        CHECK(bank.beta()[i] == m.oscV[i].beta);
        CHECK(bank.kappa()[i] == m.oscV[i].kappa);
        CHECK(bank.gamma()[i] == m.oscV[i].gamma);
        CHECK(bank.omegaHat2()[i] == m.oscV[i].omegaHat2);
        CHECK(bank.sigmaHat()[i] == m.oscV[i].sigmaHat);
    }
    REQUIRE(d.initialise(1.0, bank, 0) == Status::Ok);
    bool anyChanged = false;
    for (std::size_t i = 0; i < m.omega.size(); ++i) {
        anyChanged = anyChanged || bank.beta()[i] != m.oscV[i].beta;
        // Extra damping lowers beta, extra stiffness raises kappa at the same frequency.
        CHECK(bank.beta()[i] <= m.oscV[i].beta);
    }
    CHECK(anyChanged);
    // The horizontal and vertical tables of the string are never written.
    REQUIRE(m.oscH.size() == oscHBefore.size());
    for (std::size_t i = 0; i < m.oscH.size(); ++i) {
        CHECK(m.oscH[i].beta == oscHBefore[i].beta);
        CHECK(m.oscH[i].kappa == oscHBefore[i].kappa);
        CHECK(m.oscV[i].beta == oscVBefore[i].beta);
    }
    // A bank that is too small is refused.
    osc::ModeBank small;
    small.resize(3);
    CHECK(d.initialise(0.5, small, 0) == Status::InvalidArgument);
    CHECK(d.initialise(0.5, bank, 1) == Status::InvalidArgument);
}

TEST_CASE("A6-R1 coefficient refresh ramps above 0.1 percent and switches below it") {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    const DamperParams p = examplePad();
    DiagonalDamper d;
    REQUIRE(d.configure(m, p) == Status::Ok);
    osc::ModeBank bank;
    bank.resize(m.omega.size());
    REQUIRE(d.initialise(0.0, bank, 0) == Status::Ok);
    std::vector<osc::CoeffRamp> ramps(m.omega.size());
    std::size_t totalRamped = 0;
    std::size_t totalSwitched = 0;

    for (int subBlock : {16, 32}) {
        for (double eEnd : {1.0e-6, 1.0e-5, 0.01, 1.0}) {
            CAPTURE(subBlock);
            CAPTURE(eEnd);
            REQUIRE(d.initialise(0.0, bank, 0) == Status::Ok);
            REQUIRE(d.plan(eEnd, subBlock, ramps));
            std::size_t nRamped = 0;
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                const bool expectRamp = osc::exceedsRampThreshold(
                    m.omega[i], m.sigma[i], d.omegaAt(i, eEnd), d.sigmaAt(i, eEnd));
                // The threshold itself, from the SPEC-OSC text.
                const bool direct =
                    std::fabs(d.omegaAt(i, eEnd) - m.omega[i]) > 1e-3 * m.omega[i] ||
                    std::fabs(d.sigmaAt(i, eEnd) - m.sigma[i]) > 1e-3 * m.sigma[i];
                CHECK(expectRamp == direct);
                CHECK(ramps[i].ramped == direct);
                nRamped += ramps[i].ramped ? 1U : 0U;
                osc::OscCoeffsF64 t;
                REQUIRE(d.coefficients(i, eEnd, t) == Status::Ok);
                const double b0 = m.oscV[i].beta;
                const double k0 = m.oscV[i].kappa;
                const double g0 = m.oscV[i].gamma;
                const double b1 = static_cast<float>(t.beta);
                const double k1 = static_cast<float>(t.kappa);
                const double g1 = static_cast<float>(t.gamma);
                for (int s = 0; s < subBlock; ++s) {
                    float b = 0.0F;
                    float k = 0.0F;
                    float g = 0.0F;
                    osc::coefficientsAt(ramps[i], s, b, k, g);
                    const double w = direct ? static_cast<double>(s + 1) / subBlock : 1.0;
                    // Linear ramp over the sub-block, ending exactly on the target.
                    const double eb = b0 + (b1 - b0) * w;
                    const double ek = k0 + (k1 - k0) * w;
                    const double eg = g0 + (g1 - g0) * w;
                    CHECK(std::fabs(b - eb) <= 4e-7 * std::fabs(eb));
                    CHECK(std::fabs(k - ek) <= 4e-7 * std::fabs(ek) + 1e-30);
                    CHECK(std::fabs(g - eg) <= 4e-7 * std::fabs(eg));
                }
            }
            if (eEnd == 1.0) {
                CHECK(nRamped > m.omega.size() / 2);
            }
            if (eEnd == 1.0e-6) {
                CHECK(nRamped < m.omega.size());  // most modes switch without a ramp
            }
            totalRamped += nRamped;
            totalSwitched += m.omega.size() - nRamped;
            // After the sub-block the bank holds the target, including omegaHat2 and sigmaHat.
            d.commit(bank, 0);
            CHECK(d.engagementInForce() == eEnd);
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                osc::OscCoeffsF64 t;
                REQUIRE(d.coefficients(i, eEnd, t) == Status::Ok);
                CHECK(bank.beta()[i] == static_cast<float>(t.beta));
                CHECK(bank.omegaHat2()[i] == t.omegaHat2);
                CHECK(bank.sigmaHat()[i] == t.sigmaHat);
            }
        }
    }
    CHECK(totalRamped > 0);
    CHECK(totalSwitched > 0);
    // An unchanged engagement needs no work; commit without a plan is a no-op.
    CHECK_FALSE(d.plan(d.engagementInForce(), 16, ramps));
    d.commit(bank, 0);
    std::vector<osc::CoeffRamp> tooSmall(2);
    CHECK_FALSE(d.plan(0.5, 16, tooSmall));
    CHECK(d.refreshFailures() == 0);
}

TEST_CASE("A6-R1 overdamping check on the test strings at full engagement") {
    struct Case {
        const char* name;
        StringParams params;
    };
    const std::vector<Case> cases{{"treble f1 2000 Hz", pme::dmp_test::plainSteel(0.1, 2000.0)},
                                  {"mid f1 262 Hz", pme::dmp_test::plainSteel(0.65, 262.0)},
                                  {"bass f1 27.5 Hz", pme::dmp_test::woundBass(1.9, 27.5)}};
    for (const Case& c : cases) {
        const StringModes m = pme::dmp_test::compile(c.params);
        REQUIRE(m.omega.size() > 0);
        const OverdampingReport r = DiagonalDamper::analyse(m, examplePad());
        MESSAGE(c.name << ": modes " << m.omega.size() << ", worst sigma_d/omega_d " << r.worstRatio
                       << " at mode " << (r.worstMode + 1) << ", e " << r.worstE);
        CHECK(r.status == Status::Ok);
        CHECK(r.worstRatio < 1.0);
        CHECK(r.worstE == 1.0);
        DiagonalDamper d;
        CHECK(d.configure(m, examplePad()) == Status::Ok);
    }
    // A damper that overdamps a mode is rejected, not clamped.
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    DamperParams harsh = examplePad();
    harsh.Rd = 1.0e6;
    const OverdampingReport r = DiagonalDamper::analyse(m, harsh);
    CHECK(r.status == Status::Rejected);
    CHECK(r.rejectedMode < m.omega.size());
    CHECK(r.rejectedE > 0.0);
    DiagonalDamper d;
    CHECK(d.configure(m, harsh) == Status::Rejected);
    CHECK(d.modes() == 0);
    // Invalid input.
    DamperParams bad = examplePad();
    bad.etaD = -1.0;
    CHECK(d.configure(m, bad) == Status::InvalidArgument);
    CHECK(DiagonalDamper::analyse(m, bad).status == Status::Rejected);
    CHECK(DiagonalDamper::analyse(StringModes{}, examplePad()).status == Status::Rejected);
    CHECK(d.configure(StringModes{}, examplePad()) == Status::InvalidArgument);
}

TEST_CASE("A6-T3 damping grows monotonically as the pedal rises from fully down") {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    DiagonalDamper d;
    REQUIRE(d.configure(m, examplePad()) == Status::Ok);
    DamperController c;
    REQUIRE(c.configure(kFs, 0.35, 0.65) == Status::Ok);
    REQUIRE(c.setKey(60, examplePad()) == Status::Ok);

    std::array<double, 5> prev{};
    double prevE = -1.0;
    bool first = true;
    int increasing = 0;
    for (int step = 100; step >= 0; --step) {
        const double depth = static_cast<double>(step) / 100.0;
        c.setSustain(depth);
        c.advance(48000);  // steady state: e has reached e_target
        const double e = c.engagement(60);
        CAPTURE(depth);
        CHECK(e == c.target(60));
        std::array<double, 5> sigma{};
        for (std::size_t i = 0; i < 5; ++i) {
            sigma[i] = d.sigmaAt(i, e);
        }
        if (!first) {
            // Depth decreases: e and sigma_{n,d} never decrease.
            CHECK(e >= prevE);
            for (std::size_t i = 0; i < 5; ++i) {
                CHECK(sigma[i] >= prev[i]);
            }
            if (step >= 35 && step <= 64) {
                // Strictly increasing while the new depth lies in [P0, P1).
                CHECK(e > prevE);
                for (std::size_t i = 0; i < 5; ++i) {
                    CHECK(sigma[i] > prev[i]);
                }
                ++increasing;
            } else {
                // Constant outside [P0, P1]: both depths on the same side.
                CHECK(e == prevE);
                for (std::size_t i = 0; i < 5; ++i) {
                    CHECK(sigma[i] == prev[i]);
                }
            }
        }
        prev = sigma;
        prevE = e;
        first = false;
    }
    CHECK(increasing == 30);
    CHECK(prevE == 1.0);
}
