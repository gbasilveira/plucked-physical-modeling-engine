// SPDX-License-Identifier: MIT
#include "pme/analysis/measures.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "pme/analysis/fft.hpp"
#include "pme/analysis/linalg.hpp"

namespace pme::analysis {

// ---- Decay -------------------------------------------------------------------------------------

double t60_from_sigma(double sigma_per_s) {
    return sigma_per_s > 0.0 ? 3.0 * std::numbers::ln10 / sigma_per_s
                             : std::numeric_limits<double>::infinity();
}

namespace {

std::vector<double> log_amplitudes(std::span<const double> times_s,
                                   std::span<const double> amplitudes, std::size_t minimum) {
    if (times_s.size() != amplitudes.size() || times_s.size() < minimum) {
        throw std::invalid_argument("decay fit: need matching arrays with enough points");
    }
    std::vector<double> y(amplitudes.size());
    for (std::size_t i = 0; i < amplitudes.size(); ++i) {
        if (!(amplitudes[i] > 0.0)) {
            throw std::invalid_argument("decay fit: amplitudes must be positive");
        }
        y[i] = std::log(amplitudes[i]);
    }
    return y;
}

}  // namespace

DecayFit fit_decay(std::span<const double> times_s, std::span<const double> amplitudes) {
    const std::vector<double> y = log_amplitudes(times_s, amplitudes, 2);
    Matrix a(y.size(), 2);
    for (std::size_t i = 0; i < y.size(); ++i) {
        a(i, 0) = 1.0;
        a(i, 1) = times_s[i];
    }
    const std::vector<double> c = solve_least_squares(a, y);
    DecayFit fit;
    fit.log_amplitude_0 = c[0];
    fit.sigma_per_s = -c[1];
    fit.t60_s = t60_from_sigma(fit.sigma_per_s);
    double sum = 0.0;
    for (std::size_t i = 0; i < y.size(); ++i) {
        const double r = y[i] - (c[0] + c[1] * times_s[i]);
        sum += r * r;
    }
    fit.rms_log_residual = std::sqrt(sum / static_cast<double>(y.size()));
    return fit;
}

namespace {

struct KneeSolution {
    double sse = 0.0;
    std::vector<double> coeffs;
};

KneeSolution solve_knee(std::span<const double> t, const std::vector<double>& y, double knee) {
    Matrix a(y.size(), 3);
    for (std::size_t i = 0; i < y.size(); ++i) {
        a(i, 0) = 1.0;
        a(i, 1) = std::min(t[i], knee);
        a(i, 2) = std::max(t[i] - knee, 0.0);
    }
    KneeSolution sol;
    try {
        sol.coeffs = solve_least_squares(a, y);
    } catch (const std::runtime_error&) {
        sol.sse = std::numeric_limits<double>::infinity();
        return sol;
    }
    for (std::size_t i = 0; i < y.size(); ++i) {
        const double r = y[i] - (sol.coeffs[0] + sol.coeffs[1] * a(i, 1) + sol.coeffs[2] * a(i, 2));
        sol.sse += r * r;
    }
    return sol;
}

}  // namespace

DoubleDecayFit fit_double_decay(std::span<const double> times_s,
                                std::span<const double> amplitudes) {
    const std::vector<double> y = log_amplitudes(times_s, amplitudes, 5);
    const std::size_t n = y.size();
    const double lo = times_s[1];
    const double hi = times_s[n - 2];
    constexpr int kGrid = 200;
    double best_knee = lo;
    double best_sse = std::numeric_limits<double>::infinity();
    std::vector<double> grid(kGrid + 1);
    for (int i = 0; i <= kGrid; ++i) {
        grid[static_cast<std::size_t>(i)] = lo + (hi - lo) * i / kGrid;
        const double sse = solve_knee(times_s, y, grid[static_cast<std::size_t>(i)]).sse;
        if (sse < best_sse) {
            best_sse = sse;
            best_knee = grid[static_cast<std::size_t>(i)];
        }
    }
    // Golden-section refinement between the neighbouring grid points.
    const double step = (hi - lo) / kGrid;
    double a = std::max(lo, best_knee - step);
    double b = std::min(hi, best_knee + step);
    const double inv_phi = (std::sqrt(5.0) - 1.0) / 2.0;
    double c = b - inv_phi * (b - a);
    double d = a + inv_phi * (b - a);
    double fc = solve_knee(times_s, y, c).sse;
    double fd = solve_knee(times_s, y, d).sse;
    for (int i = 0; i < 80; ++i) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - inv_phi * (b - a);
            fc = solve_knee(times_s, y, c).sse;
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + inv_phi * (b - a);
            fd = solve_knee(times_s, y, d).sse;
        }
    }
    const double knee = 0.5 * (a + b);
    const KneeSolution sol = solve_knee(times_s, y, knee);
    DoubleDecayFit fit;
    fit.knee_s = knee;
    fit.log_amplitude_0 = sol.coeffs[0];
    fit.sigma1_per_s = -sol.coeffs[1];
    fit.sigma2_per_s = -sol.coeffs[2];
    fit.rms_log_residual = std::sqrt(sol.sse / static_cast<double>(n));
    return fit;
}

// ---- Inharmonicity -----------------------------------------------------------------------------

InharmonicityFit fit_inharmonicity(std::span<const int> partial_numbers,
                                   std::span<const double> frequencies_hz) {
    const std::size_t count = partial_numbers.size();
    if (count != frequencies_hz.size() || count < 3) {
        throw std::invalid_argument("fit_inharmonicity: need >= 3 matching partials");
    }
    // Start: (f_n / n)^2 = f0^2 + f0^2 B n^2 is linear in n^2.
    Matrix lin(count, 2);
    std::vector<double> y(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double n = partial_numbers[i];
        lin(i, 0) = 1.0;
        lin(i, 1) = n * n;
        y[i] = (frequencies_hz[i] / n) * (frequencies_hz[i] / n);
    }
    const std::vector<double> ab = solve_least_squares(lin, y);
    double f0 = std::sqrt(std::max(ab[0], std::numeric_limits<double>::min()));
    double b = ab[1] / ab[0];

    // Gauss-Newton on the residual in cents: r_n = 1200 log2(model_n / f_n).
    const double to_cents = 1200.0 / std::numbers::ln2;
    auto residuals = [&](double f0v, double bv, std::vector<double>& r) {
        for (std::size_t i = 0; i < count; ++i) {
            const double n = partial_numbers[i];
            const double model = n * f0v * std::sqrt(1.0 + bv * n * n);
            r[i] = to_cents * std::log(model / frequencies_hz[i]);
        }
    };
    std::vector<double> r(count);
    for (int iter = 0; iter < 30; ++iter) {
        residuals(f0, b, r);
        Matrix j(count, 2);
        for (std::size_t i = 0; i < count; ++i) {
            const double n = partial_numbers[i];
            j(i, 0) = to_cents;  // d r / d ln f0, so the step is relative in f0
            j(i, 1) = to_cents * 0.5 * n * n / (1.0 + b * n * n);
        }
        // Scale the B column so that both columns are comparable.
        double scale_b = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            scale_b = std::max(scale_b, std::fabs(j(i, 1)));
        }
        for (std::size_t i = 0; i < count; ++i) {
            j(i, 1) /= scale_b;
        }
        std::vector<double> neg_r(count);
        for (std::size_t i = 0; i < count; ++i) {
            neg_r[i] = -r[i];
        }
        const std::vector<double> delta = solve_least_squares(j, neg_r);
        f0 *= std::exp(delta[0]);
        b += delta[1] / scale_b;
        if (std::fabs(delta[0]) < 1e-15 &&
            std::fabs(delta[1] / scale_b) < 1e-15 * (1.0 + std::fabs(b))) {
            break;
        }
    }
    residuals(f0, b, r);
    InharmonicityFit fit;
    fit.f0_hz = f0;
    fit.b = b;
    double sum = 0.0;
    for (const double v : r) {
        sum += v * v;
        fit.max_abs_cents = std::max(fit.max_abs_cents, std::fabs(v));
    }
    fit.rms_cents = std::sqrt(sum / static_cast<double>(count));
    return fit;
}

// ---- Contact time ------------------------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): rate in Hz then threshold in force units.
ContactInterval measure_contact(std::span<const double> force, double sample_rate_hz,
                                double threshold) {
    ContactInterval out;
    const std::size_t n = force.size();
    std::size_t first = n;
    std::size_t last = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (force[i] > threshold) {
            if (first == n) {
                first = i;
            }
            last = i;
        }
    }
    if (first == n) {
        return out;
    }
    const double dt = 1.0 / sample_rate_hz;
    out.found = true;
    if (first == 0) {
        out.start_s = 0.0;
    } else {
        const double f0 = force[first - 1];
        const double f1 = force[first];
        out.start_s = (static_cast<double>(first - 1) + (threshold - f0) / (f1 - f0)) * dt;
    }
    if (last + 1 >= n) {
        out.end_s = static_cast<double>(last) * dt;
    } else {
        const double f0 = force[last];
        const double f1 = force[last + 1];
        out.end_s = (static_cast<double>(last) + (f0 - threshold) / (f0 - f1)) * dt;
    }
    out.duration_s = out.end_s - out.start_s;
    return out;
}

// ---- Spectral envelope -------------------------------------------------------------------------

SpectralEnvelope spectral_envelope(std::span<const double> signal, double sample_rate_hz,
                                   const SpectralEnvelopeOptions& options) {
    if (signal.size() < 8 || !(sample_rate_hz > 0.0) || options.points_per_octave < 1 ||
        !(options.smoothing_octaves > 0.0) || !(options.f_min_hz > 0.0)) {
        throw std::invalid_argument("spectral_envelope: invalid arguments");
    }
    const std::size_t n = signal.size();
    const std::size_t m = 2 * next_pow2(n);
    std::vector<double> windowed(n);
    double window_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = options.window == SpectrumWindow::hann
                             ? 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi *
                                                    static_cast<double>(i) / static_cast<double>(n))
                             : 1.0;
        windowed[i] = signal[i] * w;
        window_sum += w;
    }
    const std::vector<Complex> spec = fft_real(windowed, m);
    const double norm = options.normalisation == SpectrumNormalisation::coherent ? window_sum : 1.0;
    const std::size_t half = m / 2;
    std::vector<double> power(half + 1);
    for (std::size_t k = 0; k <= half; ++k) {
        power[k] = std::norm(spec[k]) / (norm * norm);
    }
    // Cumulative power at bin boundaries: cum[k] = sum of power[j] for j < k (boundary k - 1/2).
    std::vector<double> cum(half + 2, 0.0);
    for (std::size_t k = 0; k <= half; ++k) {
        cum[k + 1] = cum[k] + power[k];
    }
    auto cumulative = [&](double bin) {
        const double t = std::clamp(bin + 0.5, 0.0, static_cast<double>(half + 1));
        const auto k = static_cast<std::size_t>(std::floor(t));
        if (k >= half + 1) {
            return cum[half + 1];
        }
        return cum[k] + (t - static_cast<double>(k)) * power[k];
    };

    const double bin_hz = sample_rate_hz / static_cast<double>(m);
    const double f_max = std::min(options.f_max_hz, 0.5 * sample_rate_hz);
    SpectralEnvelope env;
    for (int i = 0;; ++i) {
        const double fc =
            options.f_min_hz * std::exp2(static_cast<double>(i) / options.points_per_octave);
        if (fc > f_max) {
            break;
        }
        const double lo = fc * std::exp2(-0.5 * options.smoothing_octaves) / bin_hz;
        const double hi = std::min(fc * std::exp2(0.5 * options.smoothing_octaves) / bin_hz,
                                   static_cast<double>(half));
        double mean = 0.0;
        if (hi > lo) {
            mean = (cumulative(hi) - cumulative(lo)) / (hi - lo);
        } else {
            mean = power[std::min(static_cast<std::size_t>(std::llround(fc / bin_hz)), half)];
        }
        env.frequencies_hz.push_back(fc);
        env.magnitude_db.push_back(10.0 * std::log10(std::max(mean, 1e-300)));
    }
    return env;
}

// ---- Energy accounting -------------------------------------------------------------------------

EnergyReport account_energy(std::span<const double> energy, double non_increasing_tolerance) {
    EnergyReport report;
    if (energy.empty()) {
        return report;
    }
    const double h0 = std::fabs(energy[0]);
    bool found_increase = false;
    for (std::size_t i = 0; i < energy.size(); ++i) {
        const double drift = std::fabs(energy[i] - energy[0]);
        report.max_relative_drift =
            std::max(report.max_relative_drift, h0 > 0.0 ? drift / h0 : drift);
        if (i + 1 < energy.size()) {
            const double hi = std::fabs(energy[i]);
            const double diff = energy[i + 1] - energy[i];
            const double rel = hi > 0.0 ? std::fabs(diff) / hi : std::fabs(diff);
            const double rel_up = hi > 0.0 ? std::max(diff, 0.0) / hi : std::max(diff, 0.0);
            report.max_relative_step_change = std::max(report.max_relative_step_change, rel);
            report.max_relative_increase = std::max(report.max_relative_increase, rel_up);
            if (!found_increase && rel_up > non_increasing_tolerance) {
                found_increase = true;
                report.first_increase_index = i;
            }
        }
    }
    report.non_increasing = !found_increase;
    return report;
}

}  // namespace pme::analysis
