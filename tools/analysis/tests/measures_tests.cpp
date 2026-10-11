// SPDX-License-Identifier: MIT
// P0.3-R3: decay and T60 fits, double decay, inharmonicity, contact time, spectral envelope and
// energy accounting.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <pme/analysis/measures.hpp>
#include <pme/test/tolerance.hpp>
#include <vector>

using namespace pme::analysis;

TEST_CASE("P0.3-R3 T60 follows 3 ln 10 / sigma") {
    CHECK(t60_from_sigma(3.0 * std::numbers::ln10 / 20.0) == doctest::Approx(20.0).epsilon(1e-12));
    CHECK(std::isinf(t60_from_sigma(0.0)));
    CHECK(std::isinf(t60_from_sigma(-1.0)));
}

TEST_CASE("P0.3-R3 decay fit recovers sigma and amplitude from an exponential envelope") {
    std::vector<double> t;
    std::vector<double> a;
    for (int i = 0; i < 40; ++i) {
        t.push_back(0.1 * i);
        a.push_back(0.7 * std::exp(-1.25 * t.back()));
    }
    const DecayFit fit = fit_decay(t, a);
    PME_CHECK_REL(fit.sigma_per_s, 1.25, 1e-12);
    PME_CHECK_REL(std::exp(fit.log_amplitude_0), 0.7, 1e-12);
    PME_CHECK_REL(fit.t60_s, t60_from_sigma(1.25), 1e-12);
    CHECK(fit.rms_log_residual < 1e-12);
    CHECK_THROWS_AS(fit_decay(t, std::vector<double>(40, 0.0)), std::invalid_argument);
}

TEST_CASE("P0.3-R3 double-decay fit returns both rates and the knee time") {
    const double sigma1 = 2.0;
    const double sigma2 = 0.4;
    const double knee = 1.337;
    std::vector<double> t;
    std::vector<double> a;
    for (int i = 0; i < 100; ++i) {
        t.push_back(0.05 * i);
        const double ln =
            t.back() < knee ? -sigma1 * t.back() : -sigma1 * knee - sigma2 * (t.back() - knee);
        a.push_back(std::exp(ln));
    }
    const DoubleDecayFit fit = fit_double_decay(t, a);
    PME_CHECK_REL(fit.sigma1_per_s, sigma1, 1e-6);
    PME_CHECK_REL(fit.sigma2_per_s, sigma2, 1e-6);
    PME_CHECK_REL(fit.knee_s, knee, 1e-6);
    CHECK(fit.rms_log_residual < 1e-9);
}

TEST_CASE("P0.3-R3 inharmonicity fit recovers f0 and B from exact partials") {
    const double f0 = 110.3;
    const double b = 2.0e-4;
    std::vector<int> n;
    std::vector<double> f;
    for (int k = 1; k <= 24; ++k) {
        n.push_back(k);
        f.push_back(k * f0 * std::sqrt(1.0 + b * k * k));
    }
    const InharmonicityFit fit = fit_inharmonicity(n, f);
    PME_CHECK_REL(fit.f0_hz, f0, 1e-9);
    PME_CHECK_REL(fit.b, b, 1e-7);
    CHECK(fit.rms_cents < 1e-6);
    CHECK_THROWS_AS(fit_inharmonicity(std::vector<int>{1, 2}, std::vector<double>{100.0, 200.0}),
                    std::invalid_argument);
}

TEST_CASE("P0.3-R3 contact time of a half-sine pulse is sub-sample accurate") {
    const double fs = 48000.0;
    const double width = 0.003;  // seconds, 144 samples
    const double threshold = 0.01;
    // Duration over which sin(pi (t - t0) / width) > threshold.
    const double expected = width * (1.0 - 2.0 * std::asin(threshold) / std::numbers::pi);
    for (const double offset : {0.0, 0.13, 0.5, 0.77, 0.999}) {
        const double t0 = 0.0101 + offset / fs;
        std::vector<double> force(1000, 0.0);
        for (std::size_t i = 0; i < force.size(); ++i) {
            const double t = static_cast<double>(i) / fs;
            if (t >= t0 && t <= t0 + width) {
                force[i] = std::sin(std::numbers::pi * (t - t0) / width);
            }
        }
        const ContactInterval c = measure_contact(force, fs, threshold);
        INFO("offset " << offset);
        REQUIRE(c.found);
        PME_CHECK_SAMPLES(c.duration_s, expected, fs,
                          0.15);  // the pulse kink costs about 0.1 sample
        PME_CHECK_SAMPLES(c.start_s, t0 + width * std::asin(threshold) / std::numbers::pi, fs,
                          0.15);
    }
    CHECK_FALSE(measure_contact(std::vector<double>(10, 0.0), fs, 0.5).found);
}

TEST_CASE("P0.3-R3 spectral envelope of a one-pole low-pass matches its response") {
    const double fs = 48000.0;
    const double pole = std::exp(-2.0 * std::numbers::pi * 1000.0 / fs);
    std::vector<double> h(65536);
    double v = 1.0 - pole;
    for (double& s : h) {
        s = v;
        v *= pole;
    }
    SpectralEnvelopeOptions opt;
    opt.window = SpectrumWindow::rectangular;
    opt.normalisation = SpectrumNormalisation::raw;
    const SpectralEnvelope env = spectral_envelope(h, fs, opt);
    REQUIRE(env.frequencies_hz.size() == env.magnitude_db.size());
    REQUIRE(env.frequencies_hz.size() > 100);
    CHECK(env.frequencies_hz.front() == 20.0);
    CHECK(env.frequencies_hz.back() <= 20000.0);

    auto power = [&](double f) {
        const double w = 2.0 * std::numbers::pi * f / fs;
        return (1.0 - pole) * (1.0 - pole) / (1.0 - 2.0 * pole * std::cos(w) + pole * pole);
    };
    double worst = 0.0;
    for (std::size_t i = 0; i < env.frequencies_hz.size(); ++i) {
        const double fc = env.frequencies_hz[i];
        const double lo = fc * std::exp2(-1.0 / 6.0);
        const double hi = fc * std::exp2(1.0 / 6.0);
        double mean = 0.0;
        constexpr int kSteps = 2000;
        for (int k = 0; k < kSteps; ++k) {
            mean += power(lo + (hi - lo) * (k + 0.5) / kSteps);
        }
        mean /= kSteps;
        const double expected_db = 10.0 * std::log10(mean);
        worst = std::max(worst, std::fabs(env.magnitude_db[i] - expected_db));
    }
    CHECK(worst < 0.05);
    // Sanity on the shape: about -26 dB at 20 kHz relative to the passband.
    CHECK(env.magnitude_db.front() - env.magnitude_db.back() > 20.0);
}

TEST_CASE("P0.3-R3 spectral envelope of a sinusoid peaks at half its amplitude when coherent") {
    const double fs = 48000.0;
    std::vector<double> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / fs);
    }
    SpectralEnvelopeOptions opt;
    opt.smoothing_octaves = 0.02;
    opt.points_per_octave = 48;
    const SpectralEnvelope env = spectral_envelope(x, fs, opt);
    double peak = -1e9;
    for (const double db : env.magnitude_db) {
        peak = std::max(peak, db);
    }
    // |X| = A / 2 = 0.25 -> power 0.0625 -> -12.04 dB, band-averaged power is lower by the peak
    // width.
    CHECK(peak < -12.0);
    CHECK(peak > -22.0);
}

TEST_CASE("P0.3-R3 energy accounting reports drift and step change and monotonicity") {
    std::vector<double> h(1000, 2.0);
    EnergyReport flat = account_energy(h);
    CHECK(flat.max_relative_drift == 0.0);
    CHECK(flat.max_relative_step_change == 0.0);
    CHECK(flat.non_increasing);

    h[500] = 2.0 * (1.0 + 3e-10);
    const EnergyReport blip = account_energy(h, 1e-9);
    PME_CHECK_REL(blip.max_relative_drift, 3e-10, 1e-6);
    PME_CHECK_REL(blip.max_relative_step_change, 3e-10, 1e-6);
    CHECK(blip.non_increasing);  // 3e-10 < 1e-9
    const EnergyReport strict = account_energy(h, 1e-11);
    CHECK_FALSE(strict.non_increasing);
    CHECK(strict.first_increase_index == 499);

    std::vector<double> decaying;
    decaying.reserve(100);
    for (int i = 0; i < 100; ++i) {
        decaying.push_back(std::exp(-0.01 * i));
    }
    const EnergyReport d = account_energy(decaying);
    CHECK(d.non_increasing);
    CHECK(d.max_relative_increase == 0.0);
    PME_CHECK_REL(d.max_relative_step_change, 1.0 - std::exp(-0.01), 1e-12);
    CHECK(account_energy(std::vector<double>{}).non_increasing);
}
