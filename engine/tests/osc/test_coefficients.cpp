// SPDX-License-Identifier: MIT
// Covers: A1-R1, A1-R2, A1-T1, A1-T2
// A1 coefficient tests: the 60-digit reference table (A1-T1), the printed-formula comparison
// (A1-R1), invalid arguments, and the bandwidth rule at 44.1, 48 and 96 kHz (A1-T2, A1-R2).
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "pme/osc.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/test/tolerance.hpp"

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double hexToDouble(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

double relErr(double actual, double expected) {
    return std::fabs(actual - expected) / std::fabs(expected);
}

nlohmann::json loadReference() {
    std::ifstream in(PME_A1_T1_REFERENCE);
    REQUIRE_MESSAGE(in.good(), "cannot open " PME_A1_T1_REFERENCE);
    nlohmann::json doc;
    in >> doc;
    return doc;
}

// The former SPEC-OSC expression (SI-0008), float64, with omega_d dt in the product form.
double printedOmegaHat2Dt2(double omega, double sigma, double fs) {
    const double dt = 1.0 / fs;
    const double x = sigma * dt;
    const double y = dt * std::sqrt((omega - sigma) * (omega + sigma));
    const double s = std::sin(0.5 * y);
    const double tanhX = std::tanh(x);
    return 2.0 * (-std::expm1(-x) + 2.0 * std::exp(-x) * s * s) -
           2.0 * tanhX * std::exp(-x) * std::cos(y);
}

}  // namespace

TEST_CASE("A1-T1 coefficients agree with the 60-digit reference") {
    const nlohmann::json doc = loadReference();
    const auto& entries = doc.at("entries");
    REQUIRE(entries.size() >= 60U * 40U);

    double worst[5] = {0, 0, 0, 0, 0};
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (const auto& e : entries) {
        const double omega = hexToDouble(e.at("omega").get<std::string>());
        const double sigma = hexToDouble(e.at("sigma").get<std::string>());
        const double fs = hexToDouble(e.at("fs").get<std::string>());

        pme::OscCoeffs out;
        out.beta = 7.0F;
        out.kappa = 8.0F;
        out.gamma = 9.0F;
        out.omegaHat2 = 10.0;
        out.sigmaHat = 11.0;

        if (e.at("rejected").get<bool>()) {
            ++rejected;
            CHECK(pme::compileOscillator(omega, sigma, fs, out) == pme::Status::Rejected);
            CHECK(out.beta == 7.0F);
            CHECK(out.kappa == 8.0F);
            CHECK(out.gamma == 9.0F);
            CHECK(out.omegaHat2 == 10.0);
            CHECK(out.sigmaHat == 11.0);
            continue;
        }
        ++accepted;
        const double rSigmaHatDt = hexToDouble(e.at("sigmaHatDt").get<std::string>());
        const double rOmegaHat2Dt2 = hexToDouble(e.at("omegaHat2Dt2").get<std::string>());
        const double rBeta = hexToDouble(e.at("beta").get<std::string>());
        const double rKappa = hexToDouble(e.at("kappa").get<std::string>());
        const double rGamma = hexToDouble(e.at("gamma").get<std::string>());

        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(omega, sigma, fs, c) == pme::Status::Ok);
        const double errs[5] = {relErr(c.sigmaHatDt, rSigmaHatDt), relErr(c.omegaHat2Dt2, rOmegaHat2Dt2),
                                relErr(c.beta, rBeta), relErr(c.kappa, rKappa),
                                relErr(c.gamma, rGamma)};
        for (int k = 0; k < 5; ++k) {
            worst[k] = std::fmax(worst[k], errs[k]);
        }
        // sigmaHat = 0 gives a reference of exactly 0 for sigmaHatDt: relative error is undefined.
        if (sigma == 0.0) {
            CHECK(c.sigmaHatDt == 0.0);
        } else {
            CHECK_MESSAGE(errs[0] <= 1e-14, "sigmaHatDt omega=" << omega << " sigma=" << sigma);
        }
        CHECK_MESSAGE(errs[1] <= 1e-14, "omegaHat2Dt2 omega=" << omega << " sigma=" << sigma);
        CHECK_MESSAGE(errs[2] <= 1e-14, "beta omega=" << omega << " sigma=" << sigma);
        CHECK_MESSAGE(errs[3] <= 1e-14, "kappa omega=" << omega << " sigma=" << sigma);
        CHECK_MESSAGE(errs[4] <= 1e-14, "gamma omega=" << omega << " sigma=" << sigma);

        // omegaHat2 and sigmaHat of the public struct, within 1e-14 of the reference scaled by fs.
        REQUIRE(pme::compileOscillator(omega, sigma, fs, out) == pme::Status::Ok);
        CHECK_MESSAGE(relErr(out.omegaHat2, rOmegaHat2Dt2 * fs * fs) <= 1e-14,
                      "omegaHat2 omega=" << omega << " sigma=" << sigma);
        if (sigma != 0.0) {
            CHECK_MESSAGE(relErr(out.sigmaHat, rSigmaHatDt * fs) <= 1e-14,
                          "sigmaHat omega=" << omega << " sigma=" << sigma);
        }
        // float32 fields are the correctly rounded float64 values, exactly.
        CHECK(out.beta == static_cast<float>(c.beta));
        CHECK(out.kappa == static_cast<float>(c.kappa));
        CHECK(out.gamma == static_cast<float>(c.gamma));
    }
    CHECK(accepted > 1000);
    CHECK(rejected > 100);
    MESSAGE("worst relative error sigmaHatDt " << worst[0] << ", omegaHat2Dt2 " << worst[1]
                                               << ", beta " << worst[2] << ", kappa " << worst[3]
                                               << ", gamma " << worst[4]);
}

TEST_CASE("A1-R1 SPEC printed formula agrees with the SI-0008 form") {
    // Documents the algebraic equivalence; the printed form loses digits when sigma dt is small.
    const nlohmann::json doc = loadReference();
    double worst = 0.0;
    for (const auto& e : doc.at("entries")) {
        if (e.at("rejected").get<bool>()) {
            continue;
        }
        const double omega = hexToDouble(e.at("omega").get<std::string>());
        const double sigma = hexToDouble(e.at("sigma").get<std::string>());
        const double fs = hexToDouble(e.at("fs").get<std::string>());
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(omega, sigma, fs, c) == pme::Status::Ok);
        const double printed = printedOmegaHat2Dt2(omega, sigma, fs);
        const double err = relErr(printed, c.omegaHat2Dt2);
        worst = std::fmax(worst, err);
        CHECK_MESSAGE(err <= 5e-12, "omega=" << omega << " sigma=" << sigma);
    }
    MESSAGE("worst relative difference between printed form and SI-0008 form " << worst);
}

TEST_CASE("A1-R1 invalid arguments return InvalidArgument and leave out unchanged") {
    pme::OscCoeffs out;
    out.beta = 3.0F;
    out.omegaHat2 = 4.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(pme::compileOscillator(100.0, 1.0, 0.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, 1.0, -48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(nan, 1.0, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, nan, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, 1.0, nan, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(inf, 1.0, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, inf, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, 1.0, inf, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(100.0, -1.0, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(pme::compileOscillator(-100.0, 1.0, 48000.0, out) == pme::Status::InvalidArgument);
    CHECK(out.beta == 3.0F);
    CHECK(out.omegaHat2 == 4.0);
    pme::osc::OscCoeffsF64 c;
    c.beta = 5.0;
    CHECK(pme::osc::compileOscillatorF64(100.0, 1.0, 0.0, c) == pme::Status::InvalidArgument);
    CHECK(c.beta == 5.0);
}

TEST_CASE("A1-R1 compileOscillator accepts a typical mode and matches the difference form") {
    pme::OscCoeffs out;
    REQUIRE(pme::compileOscillator(2.0 * kPi * 27.5, 0.5, 48000.0, out) == pme::Status::Ok);
    CHECK(out.beta > 0.0F);
    CHECK(out.beta < 1.0F);
    CHECK(out.kappa > 0.0F);
    CHECK(out.gamma > 0.0F);
    PME_CHECK_REL(out.sigmaHat, 0.5, 1e-9);
    // sigma = 0: the oscillator is undamped, beta = 1 and sigmaHat = 0.
    REQUIRE(pme::compileOscillator(2.0 * kPi * 440.0, 0.0, 48000.0, out) == pme::Status::Ok);
    CHECK(out.beta == 1.0F);
    CHECK(out.sigmaHat == 0.0);
}

namespace {

using pme::osc::BandwidthVerdict;
using pme::osc::passesBandwidthRule;

// Largest omega (sigma = 0) the bandwidth rule accepts, found by bisection on the doubles.
double highestAcceptedOmega(double fs) {
    double lo = 2.0 * kPi * 1000.0;  // accepted
    double hi = kPi * fs;            // rejected (omega_d dt = pi)
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE(passesBandwidthRule(lo, 0.0, fs, inf) == BandwidthVerdict::Accepted);
    REQUIRE(passesBandwidthRule(hi, 0.0, fs, inf) == BandwidthVerdict::AboveNyquistMargin);
    while (std::nextafter(lo, hi) < hi) {
        const double mid = 0.5 * (lo + hi);
        if (mid <= lo || mid >= hi) {
            break;
        }
        if (passesBandwidthRule(mid, 0.0, fs, inf) == BandwidthVerdict::Accepted) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

}  // namespace

TEST_CASE("A1-T2 bandwidth rejections at 44.1, 48 and 96 kHz") {
    const double inf = std::numeric_limits<double>::infinity();
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        CAPTURE(fs);
        // 0.95 pi boundary, in the product form of omega_d, one ulp-scale step each side.
        const double lo = highestAcceptedOmega(fs);
        const double hi = std::nextafter(lo, inf);
        pme::OscCoeffs out;
        out.beta = 2.0F;
        CHECK(pme::compileOscillator(lo, 0.0, fs, out) == pme::Status::Ok);
        CHECK(pme::compileOscillator(hi, 0.0, fs, out) == pme::Status::Rejected);
        CHECK(passesBandwidthRule(lo, 0.0, fs, inf) == BandwidthVerdict::Accepted);
        CHECK(passesBandwidthRule(hi, 0.0, fs, inf) == BandwidthVerdict::AboveNyquistMargin);
        CHECK(pme::osc::dampedFrequencyTimesDt(lo, 0.0, fs) <= 0.95 * kPi);
        CHECK(pme::osc::dampedFrequencyTimesDt(hi, 0.0, fs) > 0.95 * kPi);
        PME_CHECK_REL(lo / (2.0 * kPi), 0.475 * fs, 1e-12);
        MESSAGE("fs " << fs << ": highest accepted frequency " << lo / (2.0 * kPi) << " Hz");

        // Overdamped: sigma = omega and 1.0001 omega rejected, 0.9999 omega accepted.
        const double omega = 2.0 * kPi * 100.0;
        CHECK(pme::compileOscillator(omega, omega, fs, out) == pme::Status::Rejected);
        CHECK(pme::compileOscillator(omega, 1.0001 * omega, fs, out) == pme::Status::Rejected);
        CHECK(pme::compileOscillator(omega, 0.9999 * omega, fs, out) == pme::Status::Ok);
        CHECK(passesBandwidthRule(omega, omega, fs, inf) == BandwidthVerdict::Overdamped);
        CHECK(passesBandwidthRule(omega, 1.0001 * omega, fs, inf) == BandwidthVerdict::Overdamped);
        CHECK(passesBandwidthRule(omega, 0.9999 * omega, fs, inf) == BandwidthVerdict::Accepted);

        // f_max = 16 kHz: frequency is omega_d / 2 pi (sigma = 0 here).
        const double fMax = 16000.0;
        CHECK(passesBandwidthRule(2.0 * kPi * 16000.1, 0.0, fs, fMax) == BandwidthVerdict::AboveFMax);
        CHECK(passesBandwidthRule(2.0 * kPi * 15999.9, 0.0, fs, fMax) == BandwidthVerdict::Accepted);
    }

    // At 44.1 kHz an f_max above the Nyquist margin leaves the 0.95 pi rule binding.
    const double fs = 44100.0;
    const double aboveMargin = 2.0 * kPi * 0.48 * fs;  // 0.96 pi
    CHECK(passesBandwidthRule(aboveMargin, 0.0, fs, 30000.0) == BandwidthVerdict::AboveNyquistMargin);
    CHECK(passesBandwidthRule(2.0 * kPi * 20000.0, 0.0, fs, 30000.0) == BandwidthVerdict::Accepted);
    CHECK(passesBandwidthRule(2.0 * kPi * 20000.0, 0.0, fs, 19999.0) == BandwidthVerdict::AboveFMax);
}

TEST_CASE("A1-R2 compileOscillator agrees with the bandwidth rule at f_max = infinity") {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    pme::OscCoeffs out;
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        for (double f = 20.0; f < 0.5 * fs; f *= 1.07) {
            for (const double ratio : {0.0, 1e-4, 0.1, 0.9, 1.0, 1.2}) {
                const double omega = 2.0 * kPi * f;
                const double sigma = ratio * omega;
                const pme::Status st = pme::compileOscillator(omega, sigma, fs, out);
                const BandwidthVerdict v = passesBandwidthRule(omega, sigma, fs, inf);
                CHECK((st == pme::Status::Ok) == (v == BandwidthVerdict::Accepted));
                CHECK((st == pme::Status::Rejected) == (v != BandwidthVerdict::Accepted));
            }
        }
    }
    CHECK(passesBandwidthRule(100.0, 1.0, 48000.0, nan) == BandwidthVerdict::InvalidArgument);
    CHECK(passesBandwidthRule(100.0, 1.0, -1.0, inf) == BandwidthVerdict::InvalidArgument);
    CHECK(passesBandwidthRule(0.0, 0.0, 48000.0, inf) == BandwidthVerdict::Overdamped);
}
