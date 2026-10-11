// SPDX-License-Identifier: MIT
// Shared test helpers: the P0.3-T1 signal definitions (read from the JSON shared with the Python
// tests) and a platform-independent pseudo-random generator.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace pme::analysis::testing {

struct SignalPartial {
    double frequency_hz = 0.0;
    double amplitude = 0.0;
    double sigma_per_s = 0.0;
    double phase_rad = 0.0;
};

struct SignalCase {
    std::string name;
    std::string kind;  // "A", "B" or "C"
    std::vector<double> sample_rates_hz;
    double duration_s = 0.0;
    std::vector<SignalPartial> partials;
    std::vector<int> partial_numbers;  // case B
    double f0_hz = 0.0;                // case B
    double inharmonicity_b = 0.0;      // case B
};

// Reads tools/analysis/testdata/p0_3_t1_signals.json (directory from PME_ANALYSIS_TESTDATA).
std::vector<SignalCase> load_t1_cases();

// x[n] = sum a exp(-sigma t) cos(2 pi f t + phase), t = n / fs, float64.
std::vector<double> synthesize(const SignalCase& c, double sample_rate_hz);

// Uniform [0, 1) from mt19937_64 (the raw engine output is specified by the standard, unlike the
// library distributions).
class Uniform {
public:
    explicit Uniform(std::uint64_t seed) : engine_(seed) {}
    double next() { return static_cast<double>(engine_() >> 11U) * (1.0 / 9007199254740992.0); }
    double symmetric() { return 2.0 * next() - 1.0; }

private:
    std::mt19937_64 engine_;
};

}  // namespace pme::analysis::testing
