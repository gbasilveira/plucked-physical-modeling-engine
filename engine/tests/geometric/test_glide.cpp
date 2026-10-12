// SPDX-License-Identifier: MIT
// Covers: A7-T1
// Kirchhoff-Carrier pitch glide (SPEC-GEO "Tension modulation"): exciting mode 1 alone makes the
// segment exactly Duffing's equation, q'' + w1^2 q + beta q^3 = 0 with
// beta = (EA - T) k1^4 / (2 L rhoA); first-order harmonic balance gives
// w(a)^2 = w1^2 + (3/4) beta a^2 (derivation in docs/dev/geometric.md). The instantaneous
// frequency is measured from zero-crossing intervals over windows of whole cycles.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::Status;
using pme::string::GeometricConfig;

constexpr int kCyclesPerWindow = 20;

struct Window {
    double tMid{0.0};  ///< Window centre (s).
    double f{0.0};     ///< Mean frequency over the window (Hz).
    double a2{0.0};    ///< Mean-square amplitude: 2 mean(q^2) over the window.
};

/// Windows of `kCyclesPerWindow` cycles between upward zero crossings (linear interpolation).
std::vector<Window> measure(const std::vector<double>& x, double fs) {
    std::vector<double> cross;
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0 && x[i] >= 0.0) {
            cross.push_back((static_cast<double>(i - 1) + (-x[i - 1]) / (x[i] - x[i - 1])) / fs);
        }
    }
    std::vector<Window> out;
    const auto k = static_cast<std::size_t>(kCyclesPerWindow);
    for (std::size_t j = 0; j + k < cross.size(); j += k) {
        Window w;
        w.f = static_cast<double>(k) / (cross[j + k] - cross[j]);
        w.tMid = 0.5 * (cross[j] + cross[j + k]);
        const auto i0 = static_cast<std::size_t>(std::ceil(cross[j] * fs));
        const auto i1 = static_cast<std::size_t>(std::floor(cross[j + k] * fs));
        double sum = 0.0;
        for (std::size_t i = i0; i <= i1; ++i) {
            sum += x[i] * x[i];
        }
        w.a2 = 2.0 * sum / static_cast<double>(i1 - i0 + 1);
        out.push_back(w);
    }
    return out;
}

/// Run the segment and return the trace of the vertical mode `mode`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test knobs
std::vector<double> trace(const pme::StringParams& params, bool tension, double a, int samples,
                          const pme::StringModes** modesOut = nullptr) {
    static Built<double> keep;
    GeometricConfig cfg;
    cfg.tensionModulation = tension;
    cfg.sav.driftRelaxation = false;
    keep = Built<double>{};
    build(keep, params, cfg);
    REQUIRE(keep.status == Status::Ok);
    keep.seg.qV()[0] = a;  // mode 1 only, released at rest (u = 0)
    keep.seg.initialisePsi();
    std::vector<double> x(static_cast<std::size_t>(samples));
    for (int n = 0; n < samples; ++n) {
        keep.seg.advance(0.0, 0.0);
        x[static_cast<std::size_t>(n)] = keep.seg.qV()[0];
    }
    if (modesOut != nullptr) {
        *modesOut = &keep.modes;
    }
    return x;
}

}  // namespace

TEST_CASE(
    "A7-T1 a large-amplitude pluck's instantaneous frequency follows the Kirchhoff-Carrier "
    "prediction within 2 percent") {
    struct Case {
        const char* name;
        double b1;
        double cents;  // predicted initial shift
        int samples;
    };
    for (const Case& c :
         {Case{"lossless", 0.0, 40.0, 96000}, Case{"b1 = 0.5 1/s", 0.5, 50.0, 192000}}) {
        CAPTURE(c.name);
        const pme::StringParams params = plainSteel(0.65, 262.0, 1.0e-3, c.b1);
        // Small-amplitude frequency of the discrete system (the shift is measured against it).
        const pme::StringModes* mp = nullptr;
        const std::vector<double> small = trace(params, true, 1.0e-7, 48000, &mp);
        const std::vector<Window> sw = measure(small, kFs);
        REQUIRE_FALSE(sw.empty());
        const double f1 = sw.front().f;
        const double w1 = 2.0 * kPi * f1;
        const pme::StringModes modes = *mp;
        const double ea = 200.0e9 * kPi * 1.0e-6 / 4.0;
        const double beta = (ea - modes.tension) * std::pow(modes.k[0], 4) /
                            (2.0 * modes.L * modes.rhoA);  // SPEC-GEO via Duffing
        // Amplitude giving the requested initial shift: sqrt(1 + 3/4 eps) = 2^(cents/1200).
        const double eps = (std::pow(2.0, 2.0 * c.cents / 1200.0) - 1.0) / 0.75;
        const double amp = std::sqrt(eps * w1 * w1 / beta);

        const std::vector<double> x = trace(params, true, amp, c.samples);
        const std::vector<Window> ws = measure(x, kFs);
        REQUIRE(ws.size() > 5);
        double worst = 0.0;
        int used = 0;
        double lo = 1e9;
        double hi = -1e9;
        for (const Window& w : ws) {
            const double fPred = f1 * std::sqrt(1.0 + 0.75 * beta * w.a2 / (w1 * w1));
            const double dfPred = fPred - f1;
            const double dfMeas = w.f - f1;
            // Estimator resolution: one sample of timing over the window, f^2 dt / cycles.
            const double resolution = f1 * f1 / kFs / kCyclesPerWindow;
            if (dfPred <= 20.0 * resolution) {
                continue;
            }
            const double ratio = dfMeas / dfPred;
            worst = std::max(worst, std::abs(ratio - 1.0));
            lo = std::min(lo, ratio);
            hi = std::max(hi, ratio);
            ++used;
            CHECK(std::abs(ratio - 1.0) <= 0.02);
        }
        std::cout << "A7-T1 " << c.name << ": f1 = " << fix(f1, 4) << " Hz, A = " << sci(amp)
                  << ", windows used " << used << " of " << ws.size() << ", first window shift "
                  << fix(1200.0 * std::log2(ws.front().f / f1), 2)
                  << " cents, ratio measured/predicted in [" << fix(lo, 5) << ", " << fix(hi, 5)
                  << "], worst deviation " << sci(worst) << "\n";
        CHECK(used >= 5);
    }
}

TEST_CASE(
    "A7-T1 a triangle pluck at 0.12 L glides by the multi-mode first-order prediction (reported)") {
    const pme::StringParams params = plainSteel(0.65, 262.0);
    GeometricConfig cfg;
    cfg.tensionModulation = true;
    cfg.sav.driftRelaxation = false;
    Built<double> b;
    build(b, params, cfg);
    REQUIRE(b.status == Status::Ok);
    const pme::StringModes& m = b.modes;
    const double ea = 200.0e9 * kPi * 1.0e-6 / 4.0;
    const double c = (ea - m.tension) / (2.0 * m.L);
    // Scale the pluck so that the single-mode formula's initial shift of mode 1 would be 40
    // cents: the pluck's S0 gives the multi-mode prediction below, reported as it comes.
    const double xp = 0.12 * m.L;
    trianglePluck<double>(m, 1.0, xp, 0.0, b.seg.qV(), b.seg.qH());
    const double s1 = slopeIntegral<double>(m, b.seg.qV(), b.seg.qH());
    const double target =
        2.0 * (std::pow(2.0, 40.0 / 1200.0) - 1.0) * m.tension / c;  // 2 dT/T... first order
    const double amp = std::sqrt(target / s1);
    trianglePluck<double>(m, amp, xp, 0.0, b.seg.qV(), b.seg.qH());
    // Multi-mode first-order shift of mode 1 (see docs/dev/geometric.md):
    // dw1 = k1^2 c / (2 rhoA w1) [ sum_{m != 1} k_m^2 a_m^2 / 2 + 3/4 k1^2 a1^2 ].
    double others = 0.0;
    for (std::size_t i = 1; i < m.k.size(); ++i) {
        const double q = b.seg.qV()[i];
        others += m.k[i] * m.k[i] * q * q / 2.0;
    }
    const double a1 = b.seg.qV()[0];
    const double w1 = m.omega[0];
    const double dw =
        m.k[0] * m.k[0] * c / (2.0 * m.rhoA * w1) * (others + 0.75 * m.k[0] * m.k[0] * a1 * a1);
    const double predictedCents = 1200.0 * std::log2(1.0 + dw / w1);
    b.seg.initialisePsi();
    const int samples = 96000;
    std::vector<double> x(static_cast<std::size_t>(samples));
    for (int n = 0; n < samples; ++n) {
        b.seg.advance(0.0, 0.0);
        x[static_cast<std::size_t>(n)] = b.seg.qV()[0];
    }
    const std::vector<Window> ws = measure(x, kFs);
    REQUIRE(ws.size() > 3);
    const double f1 = m.omega[0] / (2.0 * kPi);
    double meanCents = 0.0;
    for (const Window& w : ws) {
        meanCents += 1200.0 * std::log2(w.f / f1);
    }
    meanCents /= static_cast<double>(ws.size());
    std::cout << "A7-T1 triangle pluck: peak y_x-based S0 = " << sci(s1 * amp * amp)
              << ", predicted mode-1 glide " << fix(predictedCents, 2)
              << " cents, measured mean over " << ws.size() << " windows " << fix(meanCents, 2)
              << " cents (first window " << fix(1200.0 * std::log2(ws.front().f / f1), 2) << ")\n";
    CHECK(std::isfinite(meanCents));
    CHECK(meanCents > 0.0);
}
