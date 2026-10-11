// SPDX-License-Identifier: MIT
// Scalar measurements used by validation tests and golden features (P0.3-R3): decay rate and T60,
// double-decay fit, inharmonicity, contact time, spectral envelope and energy accounting. float64
// throughout; methods in docs/dev/signal-analysis.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pme::analysis {

// ---- Decay (ORC-01, ORC-16)
// ----------------------------------------------------------------------

// T60 = 3 ln(10) / sigma for amplitude proportional to exp(-sigma t); infinity when sigma <= 0.
double t60_from_sigma(double sigma_per_s);

struct DecayFit {
    double sigma_per_s = 0.0;      // amplitude proportional to exp(-sigma t)
    double log_amplitude_0 = 0.0;  // ln of the amplitude at t = 0
    double t60_s = 0.0;
    double rms_log_residual = 0.0;  // rms of the fit in ln(amplitude)
};

// Least-squares line through ln(amplitude) against time. Amplitudes must be positive.
DecayFit fit_decay(std::span<const double> times_s, std::span<const double> amplitudes);

struct DoubleDecayFit {
    double sigma1_per_s = 0.0;  // rate before the knee
    double sigma2_per_s = 0.0;  // rate after the knee
    double knee_s = 0.0;
    double log_amplitude_0 = 0.0;
    double rms_log_residual = 0.0;
};

// Continuous two-slope fit of ln(amplitude): slope -sigma1 before the knee, -sigma2 after it. The
// knee is found by a grid search over [t[1], t[n-2]] refined by golden-section search.
DoubleDecayFit fit_double_decay(std::span<const double> times_s,
                                std::span<const double> amplitudes);

// ---- Inharmonicity (SPEC-STR, ORC-03) -----------------------------------------------------------

struct InharmonicityFit {
    double f0_hz = 0.0;
    double b = 0.0;
    double rms_cents = 0.0;
    double max_abs_cents = 0.0;
};

// Fits f_n = n f0 sqrt(1 + B n^2) to measured partial frequencies by minimising the error in cents
// (linear start on (f_n / n)^2, then Gauss-Newton). Needs at least 3 partials.
InharmonicityFit fit_inharmonicity(std::span<const int> partial_numbers,
                                   std::span<const double> frequencies_hz);

// ---- Contact time (ORC-08) ----------------------------------------------------------------------

struct ContactInterval {
    bool found = false;
    double start_s = 0.0;  // first upward crossing of the threshold
    double end_s = 0.0;    // last downward crossing
    double duration_s = 0.0;
};

// Interval where `force` exceeds `threshold`: from the first upward crossing to the last downward
// crossing, each located by linear interpolation between samples (sub-sample). A signal that starts
// or ends above the threshold reports that sample time as the crossing.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): rate in Hz then threshold in force units.
ContactInterval measure_contact(std::span<const double> force, double sample_rate_hz,
                                double threshold);

// ---- Spectral envelope (ORC-17, golden features)
// -------------------------------------------------

enum class SpectrumWindow : std::uint8_t { hann, rectangular };
enum class SpectrumNormalisation : std::uint8_t {
    raw,       // |X[k]|
    coherent,  // |X[k]| / sum(w): a sinusoid of amplitude A peaks at A / 2
};

struct SpectralEnvelopeOptions {
    double f_min_hz = 20.0;
    double f_max_hz = 20000.0;             // clamped to the Nyquist frequency
    int points_per_octave = 12;            // spacing of the log-frequency grid
    double smoothing_octaves = 1.0 / 3.0;  // width of the power-averaging band
    SpectrumWindow window = SpectrumWindow::hann;
    SpectrumNormalisation normalisation = SpectrumNormalisation::coherent;
};

struct SpectralEnvelope {
    std::vector<double> frequencies_hz;  // fixed log grid f_min * 2^(i / points_per_octave)
    std::vector<double> magnitude_db;    // 10 log10 of the band-averaged power
};

SpectralEnvelope spectral_envelope(std::span<const double> signal, double sample_rate_hz,
                                   const SpectralEnvelopeOptions& options = {});

// ---- Energy accounting (ORC-02, ORC-09, ORC-15)
// --------------------------------------------------

struct EnergyReport {
    double max_relative_drift = 0.0;        // max |H[n] - H[0]| / |H[0]|
    double max_relative_step_change = 0.0;  // max |H[n+1] - H[n]| / |H[n]|
    double max_relative_increase = 0.0;     // max (H[n+1] - H[n]) / |H[n]|, never negative
    bool non_increasing = true;             // max_relative_increase <= tolerance
    std::size_t first_increase_index = 0;   // index n of the first step exceeding the tolerance
};

// `energy` is the per-step energy series (float64). `non_increasing_tolerance` is the relative
// per-step increase allowed before a step counts as an increase (ORC-15: 1e-9 in float64).
EnergyReport account_energy(std::span<const double> energy, double non_increasing_tolerance = 0.0);

}  // namespace pme::analysis
