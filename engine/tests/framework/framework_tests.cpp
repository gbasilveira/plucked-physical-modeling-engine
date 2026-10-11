// SPDX-License-Identifier: MIT
// Tests of the tolerance helpers (P0.3-R1).

#include <cmath>
#include <limits>
#include <pme/test/tolerance.hpp>

TEST_CASE("P0.3-R1 relative error uses the expected value as scale") {
    CHECK(pme::test::relative_error(1.001, 1.0) == doctest::Approx(1e-3).epsilon(1e-6));
    CHECK(pme::test::relative_error(0.5, 0.0) == 0.5);
    PME_CHECK_REL(100.0 + 1e-9, 100.0, 1e-10);
}

TEST_CASE("P0.3-R1 cents are 1200 log2 of the ratio") {
    CHECK(pme::test::cents(880.0, 440.0) == doctest::Approx(1200.0));
    CHECK(pme::test::cents(220.0, 440.0) == doctest::Approx(-1200.0));
    PME_CHECK_CENTS(440.0 * std::pow(2.0, 0.005 / 1200.0), 440.0, 0.01);
}

TEST_CASE("P0.3-R1 decibels are 20 log10 of the amplitude ratio") {
    CHECK(pme::test::decibels(10.0, 1.0) == doctest::Approx(20.0));
    PME_CHECK_DB(1.0 + 1e-3, 1.0, 0.01);
    PME_CHECK_DB_VALUE(-20.05, -20.0, 0.1);
}

TEST_CASE("P0.3-R1 sample differences scale with the sample rate") {
    CHECK(pme::test::samples_between(0.0105, 0.0100, 48000.0) == doctest::Approx(24.0));
    PME_CHECK_SAMPLES(0.01, 0.01 + 0.5 / 48000.0, 48000.0, 1.0);
}

TEST_CASE("P0.3-R1 NaN never satisfies a tolerance") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(pme::test::relative_error(nan, 1.0) <= 1.0);
    CHECK_FALSE(std::fabs(pme::test::cents(nan, 440.0)) <= 1.0);
}
