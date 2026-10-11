// SPDX-License-Identifier: MIT
// Tolerance helpers for tests, in the units SPEC-ORC uses (P0.3-R1): relative error, cents, dB and
// samples. Every check prints the actual value, the expected value, the measured error and the
// tolerance when it fails. No engine dependency.
#pragma once

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include <doctest/doctest.h>

namespace pme::test {

// |actual - expected| / |expected|; |actual - expected| when expected is zero.
inline double relative_error(double actual, double expected) {
    const double diff = std::fabs(actual - expected);
    return expected == 0.0 ? diff : diff / std::fabs(expected);
}

// Signed pitch difference 1200 log2(actual / expected), in cents. Both must be positive.
inline double cents(double actual, double expected) {
    return 1200.0 * std::log2(actual / expected);
}

// Signed amplitude ratio 20 log10(actual / expected), in dB. Both must be positive.
inline double decibels(double actual, double expected) {
    return 20.0 * std::log10(actual / expected);
}

// Difference between two durations, expressed in samples at the given sample rate.
inline double samples_between(double actual_seconds, double expected_seconds,
                              double sample_rate_hz) {
    return (actual_seconds - expected_seconds) * sample_rate_hz;
}

inline std::string describe(const char* kind, double actual, double expected, double error,
                            double tolerance, const char* unit) {
    std::ostringstream out;
    out << std::setprecision(17) << kind << ": actual = " << actual << ", expected = " << expected
        << ", error = " << error << ' ' << unit << ", tolerance = " << tolerance << ' ' << unit;
    return out.str();
}

}  // namespace pme::test

// Each macro evaluates its arguments once and passes when the error is <= tolerance (false for NaN).
// clang-format off
#define PME_CHECK_REL(actual, expected, tolerance)                                             \
    do {                                                                                       \
        const double pme_a = (actual);                                                         \
        const double pme_e = (expected);                                                       \
        const double pme_t = (tolerance);                                                      \
        const double pme_err = ::pme::test::relative_error(pme_a, pme_e);                      \
        INFO(::pme::test::describe("relative error", pme_a, pme_e, pme_err, pme_t, "(rel)"));  \
        CHECK(pme_err <= pme_t);                                                               \
    } while (false)

#define PME_CHECK_CENTS(actual, expected, tolerance)                                           \
    do {                                                                                       \
        const double pme_a = (actual);                                                         \
        const double pme_e = (expected);                                                       \
        const double pme_t = (tolerance);                                                      \
        const double pme_err = std::fabs(::pme::test::cents(pme_a, pme_e));                    \
        INFO(::pme::test::describe("pitch error", pme_a, pme_e, pme_err, pme_t, "cent"));      \
        CHECK(pme_err <= pme_t);                                                               \
    } while (false)

#define PME_CHECK_DB(actual, expected, tolerance)                                              \
    do {                                                                                       \
        const double pme_a = (actual);                                                         \
        const double pme_e = (expected);                                                       \
        const double pme_t = (tolerance);                                                      \
        const double pme_err = std::fabs(::pme::test::decibels(pme_a, pme_e));                 \
        INFO(::pme::test::describe("level error", pme_a, pme_e, pme_err, pme_t, "dB"));        \
        CHECK(pme_err <= pme_t);                                                               \
    } while (false)

// Values already in dB (for example spectral envelopes): the error is the plain difference.
#define PME_CHECK_DB_VALUE(actual_db, expected_db, tolerance)                                  \
    do {                                                                                       \
        const double pme_a = (actual_db);                                                      \
        const double pme_e = (expected_db);                                                    \
        const double pme_t = (tolerance);                                                      \
        const double pme_err = std::fabs(pme_a - pme_e);                                       \
        INFO(::pme::test::describe("level difference", pme_a, pme_e, pme_err, pme_t, "dB"));   \
        CHECK(pme_err <= pme_t);                                                               \
    } while (false)

// Durations in seconds, compared in samples at sample_rate_hz.
#define PME_CHECK_SAMPLES(actual_seconds, expected_seconds, sample_rate_hz, tolerance_samples) \
    do {                                                                                       \
        const double pme_a = (actual_seconds);                                                 \
        const double pme_e = (expected_seconds);                                               \
        const double pme_t = (tolerance_samples);                                              \
        const double pme_err =                                                                 \
            std::fabs(::pme::test::samples_between(pme_a, pme_e, (sample_rate_hz)));           \
        INFO(::pme::test::describe("time error", pme_a, pme_e, pme_err, pme_t, "sample"));     \
        CHECK(pme_err <= pme_t);                                                               \
    } while (false)
// clang-format on
