// SPDX-License-Identifier: MIT
// Golden-feature files (P0.3-R4): the features of a render (partial frequencies, decay rates,
// spectral envelope, ...) stored as JSON and compared within per-feature tolerances. The format is
// described by tools/analysis/golden-features.schema.json and docs/dev/analysis.md. Serves the
// golden-feature tests of the Testing policy and SPEC-ORC rows ORC-01, ORC-08, ORC-16 and ORC-17.
#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pme::analysis {

class GoldenError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct GoldenDecay2 {
    double sigma_per_s = 0.0;  // rate after the knee
    double knee_s = 0.0;
};

struct GoldenPartial {
    int index = 0;  // partial number (1 = fundamental); the key partials are matched on
    double frequency_hz = 0.0;
    double amplitude = 0.0;
    double sigma_per_s = 0.0;
    double t60_s = 0.0;
    std::optional<GoldenDecay2> second_decay;
};

struct GoldenEnvelope {
    std::vector<double> frequencies_hz;
    std::vector<double> magnitude_db;
};

struct GoldenEnergy {
    double max_relative_drift = 0.0;
    double max_relative_step_change = 0.0;
};

// Per-partial overrides; an unset field uses the default.
struct PartialTolerance {
    std::optional<double> frequency_cents;
    std::optional<double> amplitude_db;
    std::optional<double> decay_rate_relative;
};

// Defaults: SPEC-ORC for frequency (0.01 cent, ORC-01/ORC-16), decay rate (0.5%, ORC-16), spectral
// envelope (1 dB, ORC-17) and contact time (1%, ORC-08); the planner's choice for amplitude (0.1 dB)
// and energy values (1e-9 absolute, ORC-15's per-step float64 figure).
struct GoldenTolerances {
    double frequency_cents = 0.01;
    double amplitude_db = 0.1;
    double decay_rate_relative = 0.005;
    double spectral_envelope_db = 1.0;
    double contact_time_relative = 0.01;
    double energy_absolute = 1e-9;
    std::map<int, PartialTolerance> partials;  // keyed by partial index
};

struct GoldenFeatures {
    int schema_version = 1;
    std::string scenario;
    double sample_rate_hz = 0.0;
    std::string producer_tool;
    std::string producer_version;
    std::vector<GoldenPartial> partials;
    std::optional<GoldenEnvelope> spectral_envelope;
    std::optional<double> contact_time_s;
    std::optional<GoldenEnergy> energy;
    GoldenTolerances tolerances;
};

// Parse and serialise. parse_golden throws GoldenError naming the offending field.
GoldenFeatures parse_golden(std::string_view json_text);
std::string serialize_golden(const GoldenFeatures& features);
GoldenFeatures read_golden_file(const std::string& path);
void write_golden_file(const std::string& path, const GoldenFeatures& features);

struct FeatureResult {
    std::string feature;     // for example "partial[3].decay_rate"
    double difference = 0.0; // measured, in `unit`
    double tolerance = 0.0;
    std::string unit;        // "cent", "dB", "relative", "absolute", "-"
    bool pass = true;
    std::string note;        // set for structural failures (missing partial, grid mismatch)
};

struct ComparisonResult {
    bool pass = true;
    std::vector<FeatureResult> results;
    std::vector<std::string> notes;  // informational (extra partials, scenario names differ)
};

// Compares `candidate` against `reference` using the reference's tolerances. Every feature present
// in the reference must be present in the candidate and within tolerance. Partials are matched by
// index; partials only in the candidate are reported in `notes` and do not fail.
ComparisonResult compare_golden(const GoldenFeatures& reference, const GoldenFeatures& candidate);

// Readable report: one line per failing feature (all features when failures_only is false).
std::string format_comparison(const ComparisonResult& result, bool failures_only = true);

}  // namespace pme::analysis
