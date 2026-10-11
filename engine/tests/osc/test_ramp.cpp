// SPDX-License-Identifier: MIT
// Covers: A1-R6
// Coefficient ramp utility: the 0.1% rule and linear interpolation ending on the target.
#include <array>
#include <cmath>

#include <doctest/doctest.h>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/ramp.hpp"

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kFs = 48000.0;

pme::OscCoeffs compile(double omega, double sigma) {
    pme::OscCoeffs c;
    REQUIRE(pme::compileOscillator(omega, sigma, kFs, c) == pme::Status::Ok);
    return c;
}

}  // namespace

TEST_CASE("A1-R6 changes at 0.09 percent switch and changes at 0.11 percent ramp") {
    const double omega = 2.0 * kPi * 440.0;
    const double sigma = 3.0;
    CHECK_FALSE(pme::osc::exceedsRampThreshold(omega, sigma, omega * 1.0009, sigma));
    CHECK(pme::osc::exceedsRampThreshold(omega, sigma, omega * 1.0011, sigma));
    CHECK_FALSE(pme::osc::exceedsRampThreshold(omega, sigma, omega * 0.9991, sigma));
    CHECK(pme::osc::exceedsRampThreshold(omega, sigma, omega * 0.9989, sigma));
    CHECK_FALSE(pme::osc::exceedsRampThreshold(omega, sigma, omega, sigma * 1.0009));
    CHECK(pme::osc::exceedsRampThreshold(omega, sigma, omega, sigma * 1.0011));
    CHECK_FALSE(pme::osc::exceedsRampThreshold(omega, sigma, omega, sigma));
    // sigma from or to 0 always ramps.
    CHECK(pme::osc::exceedsRampThreshold(omega, 0.0, omega, 1e-9));
    CHECK(pme::osc::exceedsRampThreshold(omega, 5.0, omega, 0.0));
    CHECK_FALSE(pme::osc::exceedsRampThreshold(omega, 0.0, omega, 0.0));

    const pme::OscCoeffs cur = compile(omega, sigma);
    for (const int s : {16, 32}) {
        const pme::OscCoeffs small = compile(omega * 1.0009, sigma);
        const pme::OscCoeffs big = compile(omega * 1.0011, sigma);
        const auto planSmall = pme::osc::planRamp(cur, omega, sigma, small, omega * 1.0009, sigma, s);
        const auto planBig = pme::osc::planRamp(cur, omega, sigma, big, omega * 1.0011, sigma, s);
        CHECK_FALSE(planSmall.ramped);
        CHECK(planBig.ramped);
        // Below the threshold the target applies from the first sample.
        for (int i = 0; i < s; ++i) {
            float b = 0.0F;
            float k = 0.0F;
            float g = 0.0F;
            pme::osc::coefficientsAt(planSmall, i, b, k, g);
            CHECK(b == small.beta);
            CHECK(k == small.kappa);
            CHECK(g == small.gamma);
        }
    }
}

TEST_CASE("A1-R6 the ramp is linear per sample and ends exactly on the target") {
    const double omega = 2.0 * kPi * 1000.0;
    const double omegaT = 1.01 * omega;
    const pme::OscCoeffs cur = compile(omega, 0.5);
    const pme::OscCoeffs tgt = compile(omegaT, 0.5);
    for (const int s : {16, 32}) {
        const auto plan = pme::osc::planRamp(cur, omega, 0.5, tgt, omegaT, 0.5, s);
        REQUIRE(plan.ramped);
        float prevK = cur.kappa;
        double firstStep = 0.0;
        for (int i = 0; i < s; ++i) {
            float b = 0.0F;
            float k = 0.0F;
            float g = 0.0F;
            pme::osc::coefficientsAt(plan, i, b, k, g);
            const double expectK = static_cast<double>(cur.kappa) +
                                   (static_cast<double>(tgt.kappa) - cur.kappa) * (i + 1) / s;
            CHECK(std::fabs(static_cast<double>(k) - expectK) <=
                  2e-7 * std::fabs(static_cast<double>(tgt.kappa)));
            const double step = static_cast<double>(k) - prevK;
            if (i == 0) {
                firstStep = step;
            } else if (i + 1 < s) {
                // float32 quantisation of kappa: the steps agree within two ulp.
                const double ulp = static_cast<double>(std::nextafter(tgt.kappa, 1.0F) - tgt.kappa);
                CHECK(std::fabs(step - firstStep) <= 2.0 * ulp);
            }
            prevK = k;
            if (i == s - 1) {
                CHECK(b == tgt.beta);
                CHECK(k == tgt.kappa);
                CHECK(g == tgt.gamma);
            }
        }
    }
}

TEST_CASE("A1-R6 applyRampsAt writes the ramp of each mode into the bank and keeps the state") {
    const double omega = 2.0 * kPi * 500.0;
    std::array<pme::osc::CoeffRamp, 2> ramps{};
    pme::osc::ModeBank bank;
    bank.resize(3);
    const pme::OscCoeffs c0 = compile(omega, 1.0);
    const pme::OscCoeffs c1 = compile(omega * 1.02, 1.0);
    for (std::size_t i = 0; i < 3; ++i) {
        bank.setMode(i, c0);
        bank.q()[i] = 0.5F;
        bank.u()[i] = 0.25F;
    }
    ramps[0] = pme::osc::planRamp(c0, omega, 1.0, c1, omega * 1.02, 1.0, 16);
    ramps[1] = pme::osc::planRamp(c0, omega, 1.0, c0, omega, 1.0, 16);  // unchanged: not ramped
    CHECK(ramps[0].ramped);
    CHECK_FALSE(ramps[1].ramped);
    pme::osc::applyRampsAt(bank, 1, ramps, 15);
    CHECK(bank.kappa()[0] == c0.kappa);  // mode 0 untouched
    CHECK(bank.kappa()[1] == c1.kappa);  // last sample: exactly the target
    CHECK(bank.beta()[1] == c1.beta);
    CHECK(bank.gamma()[1] == c1.gamma);
    CHECK(bank.kappa()[2] == c0.kappa);
    CHECK(bank.q()[1] == 0.5F);
    CHECK(bank.u()[1] == 0.25F);
    pme::osc::applyRampsAt(bank, 1, ramps, 0);
    CHECK(bank.kappa()[1] > c0.kappa);
    CHECK(bank.kappa()[1] < c1.kappa);
}

TEST_CASE("A1-R6 a sub-block of zero length is never ramped") {
    const pme::OscCoeffs c0 = compile(2.0 * kPi * 500.0, 1.0);
    const pme::OscCoeffs c1 = compile(2.0 * kPi * 600.0, 1.0);
    const auto plan = pme::osc::planRamp(c0, 2.0 * kPi * 500.0, 1.0, c1, 2.0 * kPi * 600.0, 1.0, 0);
    CHECK_FALSE(plan.ramped);
}
