// SPDX-License-Identifier: MIT
// Rendering of a scenario to features, WAV and energy traces (P0.5-R7). The golden-feature file
// is written through pme_analysis, so the P0.3 comparator reads it unchanged.
#pragma once

#include <string>

#include "pme/analysis/golden.hpp"
#include "pme/analysis/wav.hpp"
#include "pme/ref/scenario.hpp"
#include "pme/ref/simulation.hpp"

namespace pme::ref {

inline constexpr const char* kProducerTool = "pme_reference";
inline constexpr const char* kProducerVersion = "0.5.0";

struct Rendered {
    Results results;
    pme::analysis::GoldenFeatures features;
};

// Simulates the scenario and extracts its golden features: partials of the first string, bridge or
// plate observation by peak picking at the simulation rate (index = round(f / f1) of the first
// string when there is one, else the rank in frequency order), the spectral envelope of the same
// signal, the contact time of the first hammer penetration signal (threshold 0) and the energy
// summary. A partial without decay reports t60 = 1e12 s. Throws RefError.
Rendered render_scenario(const Scenario& scenario);

// Golden features of already simulated results (the part of render_scenario after simulate()).
pme::analysis::GoldenFeatures extract_features(const Scenario& scenario, const Results& results);

// Observation signals as a float32 WAV at the nominal sample rate `fs`: low-pass filtered
// (Hann-windowed sinc, cutoff 0.45 fs) and decimated by the oversampling factor, one channel per
// observation. With `normalise` the whole file is scaled to a peak of 0.9. Throws RefError when
// fs is not an integer number of Hz.
pme::analysis::WavData results_to_wav(const Results& results, bool normalise);

// Energy trace and its summary as JSON text (schema in reference/README.md).
std::string energy_to_json(const Results& results);

}  // namespace pme::ref
