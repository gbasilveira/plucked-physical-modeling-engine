// SPDX-License-Identifier: MIT
// P0.3-T1: the analysis tools recover frequencies within 0.01 cent and decay rates within 0.5% from
// synthetic sums of damped sinusoids (cases A, B, C of the brief, shared with the Python tests),
// and the Case B inharmonicity fit (P0.3-R3).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <pme/analysis/measures.hpp>
#include <pme/analysis/partials.hpp>
#include <pme/test/tolerance.hpp>

#include "testdata.hpp"

using pme::analysis::PartialTrackingOptions;
using pme::analysis::PartialTrackingResult;
using pme::analysis::testing::SignalCase;

namespace {

struct Worst {
    double cents = 0.0;
    double decay_percent = 0.0;
};

// Checks every partial against the definition, returns the worst errors (for the report).
Worst check_against(const SignalCase& c, const PartialTrackingResult& result) {
    Worst worst;
    if (result.partials.size() != c.partials.size()) {
        for (const auto& p : result.partials) {
            std::printf("  found %.4f Hz sigma %.4f\n", p.frequency_hz, p.sigma_per_s);
        }
    }
    REQUIRE(result.partials.size() == c.partials.size());
    for (std::size_t k = 0; k < c.partials.size(); ++k) {
        INFO("case " << c.name << " partial " << k + 1);
        const auto& est = result.partials[k];
        const auto& truth = c.partials[k];
        PME_CHECK_CENTS(est.frequency_hz, truth.frequency_hz, 0.01);
        PME_CHECK_REL(est.sigma_per_s, truth.sigma_per_s, 0.005);
        worst.cents = std::max(worst.cents, std::fabs(pme::test::cents(est.frequency_hz, truth.frequency_hz)));
        worst.decay_percent = std::max(
            worst.decay_percent, 100.0 * pme::test::relative_error(est.sigma_per_s, truth.sigma_per_s));
    }
    return worst;
}

PartialTrackingOptions options_with_offset(const SignalCase& c, double relative_offset) {
    PartialTrackingOptions opt;
    for (const auto& p : c.partials) {
        opt.approx_frequencies_hz.push_back(p.frequency_hz * (1.0 + relative_offset));
    }
    opt.envelope_frame_s = 0.0;
    return opt;
}

void report(const char* what, const SignalCase& c, double fs, const Worst& w) {
    std::printf("[P0.3-T1] %s %s fs=%.0f: worst %.3g cent, %.3g %% decay\n", what, c.name.c_str(), fs,
                w.cents, w.decay_percent);
}

}  // namespace

TEST_CASE("P0.3-T1 case A recovers a single partial at 27.5, 261.6 and 4186 Hz") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "A") {
            continue;
        }
        for (const double fs : c.sample_rates_hz) {
            const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
            const auto result = pme::analysis::track_partials(x, fs, options_with_offset(c, 3e-4));
            report("A", c, fs, check_against(c, result));
            CHECK(result.converged);
        }
    }
}

TEST_CASE("P0.3-T1 case A by peak picking needs no frequency hint") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "A") {
            continue;
        }
        const double fs = c.sample_rates_hz.front();
        const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        PartialTrackingOptions opt;
        opt.envelope_frame_s = 0.0;
        const auto result = pme::analysis::track_partials(x, fs, opt);
        report("A/peaks", c, fs, check_against(c, result));
    }
}

TEST_CASE("P0.3-T1 case B recovers 30 stiff-string partials at 44.1, 48 and 96 kHz") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "B") {
            continue;
        }
        for (const double fs : c.sample_rates_hz) {
            const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
            const auto result = pme::analysis::track_partials(x, fs, options_with_offset(c, 2e-4));
            report("B", c, fs, check_against(c, result));
            CHECK(result.relative_residual < 1e-8);
        }
    }
}

TEST_CASE("P0.3-T1 case B by peak picking finds all 30 partials") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "B") {
            continue;
        }
        const double fs = 48000.0;
        const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        PartialTrackingOptions opt;
        opt.envelope_frame_s = 0.0;
        // The fastest-decaying partials sit about 90 dB below the strongest in a 4 s Hann window.
        opt.peak_threshold_db = 100.0;
        const auto result = pme::analysis::track_partials(x, fs, opt);
        report("B/peaks", c, fs, check_against(c, result));
    }
}

TEST_CASE("P0.3-T1 case C separates unison partials 1 Hz apart") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "C") {
            continue;
        }
        const double fs = c.sample_rates_hz.front();
        const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        PartialTrackingOptions opt;
        opt.approx_frequencies_hz = {c.partials[0].frequency_hz + 0.05, c.partials[1].frequency_hz - 0.05};
        opt.envelope_frame_s = 0.0;
        const auto result = pme::analysis::track_partials(x, fs, opt);
        report("C", c, fs, check_against(c, result));
    }
}

TEST_CASE("P0.3-R3 case B inharmonicity fit recovers f0 and B") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "B") {
            continue;
        }
        const double fs = 48000.0;
        const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        const auto result = pme::analysis::track_partials(x, fs, options_with_offset(c, 2e-4));
        std::vector<double> freqs;
        for (const auto& p : result.partials) {
            freqs.push_back(p.frequency_hz);
        }
        const auto fit = pme::analysis::fit_inharmonicity(c.partial_numbers, freqs);
        PME_CHECK_REL(fit.b, c.inharmonicity_b, 1e-3);
        PME_CHECK_CENTS(fit.f0_hz, c.f0_hz, 0.01);
        std::printf("[P0.3-R3] inharmonicity: B error %.3g (rel), f0 error %.3g cent, rms %.3g cent\n",
                    pme::test::relative_error(fit.b, c.inharmonicity_b),
                    std::fabs(pme::test::cents(fit.f0_hz, c.f0_hz)), fit.rms_cents);
    }
}

TEST_CASE("P0.3-R3 partial tracking returns amplitude envelope and phase") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "C") {
            continue;
        }
        const double fs = c.sample_rates_hz.front();
        const std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        PartialTrackingOptions opt = options_with_offset(c, 1e-4);
        opt.envelope_frame_s = 0.5;
        const auto result = pme::analysis::track_partials(x, fs, opt);
        REQUIRE(result.partials.size() == 2);
        for (std::size_t k = 0; k < 2; ++k) {
            const auto& est = result.partials[k];
            const auto& truth = c.partials[k];
            PME_CHECK_REL(est.amplitude, truth.amplitude, 1e-6);
            CHECK(std::fabs(est.phase_rad - truth.phase_rad) < 1e-6);
            REQUIRE(est.envelope_times_s.size() == 8);
            REQUIRE(est.envelope_amplitudes.size() == 8);
            for (std::size_t j = 0; j < 8; ++j) {
                const double expected = truth.amplitude * std::exp(-truth.sigma_per_s * est.envelope_times_s[j]);
                PME_CHECK_REL(est.envelope_amplitudes[j], expected, 1e-5);
            }
        }
    }
}

TEST_CASE("P0.3-T1 case A survives rounding the signal to float32") {
    for (const SignalCase& c : pme::analysis::testing::load_t1_cases()) {
        if (c.kind != "A") {
            continue;
        }
        const double fs = c.sample_rates_hz.front();
        std::vector<double> x = pme::analysis::testing::synthesize(c, fs);
        for (double& v : x) {
            v = static_cast<double>(static_cast<float>(v));
        }
        const auto result = pme::analysis::track_partials(x, fs, options_with_offset(c, 3e-4));
        REQUIRE(result.partials.size() == 1);
        const double cents = std::fabs(pme::test::cents(result.partials[0].frequency_hz, c.partials[0].frequency_hz));
        const double decay = 100.0 * pme::test::relative_error(result.partials[0].sigma_per_s, c.partials[0].sigma_per_s);
        std::printf("[P0.3-T1] float32 input %s: %.3g cent, %.3g %% decay\n", c.name.c_str(), cents, decay);
        CHECK(cents < 0.01);
        CHECK(decay < 0.5);
    }
}
