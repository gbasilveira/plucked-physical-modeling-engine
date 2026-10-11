// SPDX-License-Identifier: MIT
// Partial tracking (P0.3-R3): frequency, decay rate, amplitude and phase of the damped sinusoids in
// a signal, plus their amplitude envelopes over time. Serves SPEC-ORC rows ORC-01 (oscillator
// accuracy), ORC-16 (sample-rate independence) and ORC-07 (unison coupling) and the golden-feature
// files (P0.3-R4). float64 throughout; method in docs/dev/analysis.md.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace pme::analysis {

struct PartialTrackingOptions {
    // Approximate partial frequencies in Hz. When empty, partials are found by peak picking.
    std::vector<double> approx_frequencies_hz;
    // Peak picking only: keep spectral peaks within this many dB of the strongest one.
    double peak_threshold_db = 60.0;
    // Peak picking only: at most this many partials (strongest first, returned in frequency order).
    std::size_t max_partials = 64;
    // Peak picking only: peaks closer than this are merged (<= 0 selects 3 / duration).
    double min_separation_hz = 0.0;
    // Frame length of the amplitude envelope in seconds; <= 0 disables the envelope.
    double envelope_frame_s = 0.25;
    // Levenberg-Marquardt iteration limit.
    int max_iterations = 100;
};

// x(t) ~ sum_k amplitude_k exp(-sigma_k t) cos(2 pi frequency_k t + phase_k), t = n / sample_rate.
struct PartialEstimate {
    double frequency_hz = 0.0;
    double sigma_per_s = 0.0;                 // amplitude proportional to exp(-sigma t)
    double t60_s = 0.0;                       // 3 ln(10) / sigma; infinity when sigma <= 0
    double amplitude = 0.0;                   // at t = 0
    double phase_rad = 0.0;                   // at t = 0
    std::vector<double> envelope_times_s;     // frame centres
    std::vector<double> envelope_amplitudes;  // amplitude of this partial at each frame centre
};

struct PartialTrackingResult {
    std::vector<PartialEstimate> partials;  // in frequency order for peak picking, else input order
    double relative_residual = 0.0;         // ||data - model|| / ||data|| over the fitted bins
    int iterations = 0;
    bool converged = false;
};

// Fits all partials jointly in the frequency domain (Hann-windowed spectrum, closed-form model of
// damped sinusoids, Levenberg-Marquardt), so partials about 1 Hz apart are separated. Throws
// std::invalid_argument for a signal shorter than 16 samples or a non-positive sample rate.
PartialTrackingResult track_partials(std::span<const double> signal, double sample_rate_hz,
                                     const PartialTrackingOptions& options);

}  // namespace pme::analysis
