// SPDX-License-Identifier: MIT
// Covers: A7-T2
// Phantom partials (SPEC-GEO "Longitudinal force at the bridge", "Transmission to the body"): the
// vertical bridge force of a large pluck contains lines at the sums f_i + f_j of transverse
// partials, because F_L carries (EA - T)/2 y_x(L)^2 and the longitudinal modes respond to the
// pair sums. The control run has the longitudinal modes off and kappa_L = 0.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <vector>

#include "pme/analysis/fft.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::Status;
using pme::string::GeometricConfig;

constexpr std::size_t kSamples = 96000;  // 2 s
constexpr std::size_t kFft = 131072;
constexpr double kBinHz = kFs / static_cast<double>(kFft);

struct Result {
    std::vector<double> mag;     ///< |X| of the Hann-windowed bridge force, bins 0..kFft/2.
    std::vector<double> modeHz;  ///< Linear partial frequencies of the string (Hz).
    std::size_t keptL{0};
    double inharmonicity{0.0};
};

Result run(bool longitudinal, double amp) {
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.longitudinalModes = longitudinal ? 8 : 0;
    cfg.kappaL = longitudinal ? 0.026 : 0.0;
    cfg.kappaT = 0.0;
    cfg.fMax = 20000.0;
    cfg.sav.driftRelaxation = false;
    Built<double> b;
    // Stiffer string: d = 2.2 mm gives B about 1.5e-3, so f_i + f_j sits away from every partial.
    build(b, plainSteel(0.65, 262.0, 2.2e-3), cfg);
    REQUIRE(b.status == Status::Ok);
    pme::bridge::BridgePoint<double> point;
    pme::bridge::ParallelModel model;
    const double omega = 2.0 * kPi * 300.0;
    REQUIRE(pme::bridge::massSpringDamperModel(0.3, 0.3 * omega * omega, 0.0, model) == Status::Ok);
    REQUIRE(point.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    trianglePluck<double>(b.modes, amp, 0.12 * b.modes.L, 0.0, b.seg.qV(), b.seg.qH());
    b.seg.initialisePsi();
    std::vector<double> fb(kSamples);
    for (std::size_t n = 0; n < kSamples; ++n) {
        fb[n] = b.seg.addBridgeForces(point);  // F_b,v including kappa_L F_L
        point.advance();
        b.seg.advance(point.accelV(), point.accelH());
    }
    // Hann window (periodic), zero-padded transform.
    std::vector<double> windowed(kSamples);
    for (std::size_t n = 0; n < kSamples; ++n) {
        windowed[n] = fb[n] * (0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) /
                                                    static_cast<double>(kSamples)));
    }
    const auto spec = pme::analysis::fft_real(windowed, kFft);
    Result r;
    r.mag.resize(kFft / 2 + 1);
    for (std::size_t i = 0; i < r.mag.size(); ++i) {
        r.mag[i] = std::abs(spec[i]);
    }
    for (const double w : b.modes.omega) {
        r.modeHz.push_back(w / (2.0 * kPi));
    }
    r.keptL = b.seg.longitudinalCount();
    r.inharmonicity = b.modes.inharmonicity;
    return r;
}

/// Frequency of the strongest bin within +-`halfWidth` Hz of `centre`, refined by parabolic
/// interpolation of the log magnitude.
double peakFrequency(const std::vector<double>& mag, double centre, double halfWidth) {
    const auto lo = static_cast<std::size_t>((centre - halfWidth) / kBinHz);
    const auto hi = static_cast<std::size_t>((centre + halfWidth) / kBinHz);
    std::size_t best = lo;
    for (std::size_t i = lo; i <= hi; ++i) {
        if (mag[i] > mag[best]) {
            best = i;
        }
    }
    const double a = std::log(mag[best - 1]);
    const double bb = std::log(mag[best]);
    const double c = std::log(mag[best + 1]);
    const double delta = 0.5 * (a - c) / (a - 2.0 * bb + c);
    return (static_cast<double>(best) + delta) * kBinHz;
}

double db(double v) { return 20.0 * std::log10(v); }

/// Largest magnitude within +-2 bins of `f`.
double peakNear(const std::vector<double>& mag, double f) {
    const auto centre = static_cast<std::size_t>(std::llround(f / kBinHz));
    double best = 0.0;
    for (std::size_t i = centre - 2; i <= centre + 2; ++i) {
        best = std::max(best, mag[i]);
    }
    return best;
}

/// Median magnitude over +-20 bins.
double localFloor(const std::vector<double>& mag, double f) {
    const auto centre = static_cast<std::size_t>(std::llround(f / kBinHz));
    std::vector<double> window(mag.begin() + static_cast<std::ptrdiff_t>(centre - 20),
                               mag.begin() + static_cast<std::ptrdiff_t>(centre + 21));
    std::nth_element(window.begin(), window.begin() + 20, window.end());
    return window[20];
}

}  // namespace

TEST_CASE("A7-T2 the bridge-force spectrum shows phantom partials at the sums f_i + f_j") {
    const Result on = run(true, 5.0e-3);
    const Result off = run(false, 5.0e-3);
    REQUIRE(on.keptL >= 1);
    // Measured transverse partials 1..12 from the spectrum with longitudinal modes on.
    std::vector<double> measured;
    for (std::size_t n = 0; n < 12; ++n) {
        measured.push_back(peakFrequency(on.mag, on.modeHz[n], 0.03 * on.modeHz[n]));
    }
    std::cout << "A7-T2 string B = " << sci(on.inharmonicity) << ", kept " << on.keptL
              << " longitudinal modes, measured f1 = " << fix(measured[0], 3) << " Hz (linear "
              << fix(on.modeHz[0], 3) << "), bin = " << fix(kBinHz, 4) << " Hz\n";
    int checked = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            const double target = measured[i] + measured[j];
            CAPTURE(i + 1);
            CAPTURE(j + 1);
            // The sum differs from every partial by more than 5 bins (precondition of the test).
            for (const double fk : measured) {
                CHECK(std::abs(target - fk) > 5.0 * kBinHz);
            }
            const double peak = peakNear(on.mag, target);
            const double floorLevel = localFloor(on.mag, target);
            const double control = peakNear(off.mag, target);
            std::cout << "A7-T2 pair (" << i + 1 << "," << j + 1 << ") at " << fix(target, 2)
                      << " Hz: peak " << fix(db(peak), 1) << " dB, floor " << fix(db(floorLevel), 1)
                      << " dB, above floor " << fix(db(peak / floorLevel), 1)
                      << " dB; control peak " << fix(db(control), 1) << " dB, weaker by "
                      << fix(db(peak / control), 1) << " dB\n";
            CHECK(db(peak / floorLevel) >= 20.0);
            CHECK(db(peak / control) >= 20.0);
            ++checked;
        }
    }
    CHECK(checked == 6);
}
