// SPDX-License-Identifier: MIT
#include "pme/analysis/partials.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "pme/analysis/fft.hpp"
#include "pme/analysis/linalg.hpp"

namespace pme::analysis {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
const Complex kI{0.0, 1.0};

// ---- Closed-form spectrum of a Hann-windowed damped exponential --------------------------------
//
// For z = exp(u), the window w[n] = 1/2 - 1/2 cos(theta n), theta = 2 pi / N, and the DFT bin
// nu = 2 pi m / M (zero-padded to M), the sum over n = 0 .. N-1 of w[n] z^n exp(-i nu n) is
//     S(u) = (1 - Q) [ 1/2 A(u_nu) - 1/4 A(u_nu + i theta) - 1/4 A(u_nu - i theta) ],
//     A(v) = 1 / (1 - exp(v)),   u_nu = u - i nu,   Q = exp(N u_nu)
// (a geometric series; exp(N (u_nu +- i theta)) = Q because N theta = 2 pi). A real component
// x[n] = Re(c z^n) transforms to (c S(u) + conj(c) S(conj u)) / 2.

struct Ratio {
    Complex g;   // (1 - Q) / (1 - exp(v))
    Complex dg;  // dg / dv
};

Ratio ratio(double a, double b, Complex one_minus_q, Complex n_q, double n_samples) {
    const double ea = std::exp(a);
    const double half_sin = std::sin(0.5 * b);
    // 1 - exp(a + i b) without cancellation near v = 0.
    const Complex den{-std::expm1(a) + 2.0 * ea * half_sin * half_sin, -ea * std::sin(b)};
    if (std::abs(den) < 1e-10) {
        // Series of sum_{n<N} exp(n v) about v = 0.
        const Complex v{a, b};
        const double n = n_samples;
        return {n + v * (n * (n - 1.0) / 2.0) + v * v * ((n - 1.0) * n * (2.0 * n - 1.0) / 6.0),
                n * (n - 1.0) / 2.0 + v * ((n - 1.0) * n * (2.0 * n - 1.0) / 3.0)};
    }
    const Complex e_v = 1.0 - den;
    return {one_minus_q / den, -n_q / den + one_minus_q * e_v / (den * den)};
}

struct Basis {
    Complex s;   // S(u)
    Complex ds;  // dS/du
};

struct Window {
    double n_samples;  // N
    double theta;      // 2 pi / N
};

Basis hann_dtft(Complex u, double nu, const Window& win) {
    const double n_samples = win.n_samples;
    const double theta = win.theta;
    const double a = u.real();
    const double b = u.imag() - nu;
    const double n_a = n_samples * a;
    const double n_b = n_samples * b;
    const double e_na = std::exp(n_a);
    const double half_sin = std::sin(0.5 * n_b);
    const Complex one_minus_q{-std::expm1(n_a) + 2.0 * e_na * half_sin * half_sin,
                              -e_na * std::sin(n_b)};
    const Complex n_q = n_samples * e_na * Complex{std::cos(n_b), std::sin(n_b)};
    const Ratio r0 = ratio(a, b, one_minus_q, n_q, n_samples);
    const Ratio rp = ratio(a, b + theta, one_minus_q, n_q, n_samples);
    const Ratio rm = ratio(a, b - theta, one_minus_q, n_q, n_samples);
    return {0.5 * r0.g - 0.25 * (rp.g + rm.g), 0.5 * r0.dg - 0.25 * (rp.dg + rm.dg)};
}

// ---- Spectrum and fit --------------------------------------------------------------------------

struct Spectrum {
    double fs = 0.0;
    double n = 0.0;          // signal length N
    std::size_t m = 0;       // transform size M
    double theta = 0.0;      // 2 pi / N
    std::vector<Complex> x;  // bins 0 .. M/2 of the Hann-windowed, zero-padded signal
    [[nodiscard]] double bin_hz() const { return fs / static_cast<double>(m); }
    [[nodiscard]] double nu(std::size_t bin) const {
        return kTwoPi * static_cast<double>(bin) / static_cast<double>(m);
    }
    [[nodiscard]] double duration() const { return n / fs; }
    [[nodiscard]] Window window() const { return {n, theta}; }
};

struct Param {
    double f = 0.0;      // Hz
    double sigma = 0.0;  // 1/s
    double alpha = 0.0;  // Re c
    double beta = 0.0;   // Im c
};

constexpr std::size_t kParamsPerPartial = 4;

Spectrum compute_spectrum(std::span<const double> signal, double fs) {
    Spectrum sp;
    sp.fs = fs;
    sp.n = static_cast<double>(signal.size());
    sp.m = next_pow2(signal.size());
    sp.theta = kTwoPi / sp.n;
    std::vector<double> windowed(signal.size());
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(sp.theta * static_cast<double>(i));
        windowed[i] = signal[i] * w;
    }
    std::vector<Complex> full = fft_real(windowed, sp.m);
    sp.x.assign(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(sp.m / 2 + 1));
    return sp;
}

struct Evaluation {
    std::vector<double> residual;  // data - model, (Re, Im) per bin
    Matrix jacobian;               // d model / d params, empty when not requested
    double cost = 0.0;             // squared norm of the residual
};

// SPEC-ORC support (ORC-01, ORC-16): model of K damped sinusoids over the selected bins.
Evaluation evaluate(const Spectrum& sp, const std::vector<Param>& params,
                    const std::vector<std::size_t>& bins, bool with_jacobian) {
    const std::size_t rows = 2 * bins.size();
    Evaluation ev;
    ev.residual.assign(rows, 0.0);
    if (with_jacobian) {
        ev.jacobian = Matrix(rows, kParamsPerPartial * params.size());
    }
    std::vector<Complex> model(bins.size(), Complex{0.0, 0.0});
    const double two_pi_over_fs = kTwoPi / sp.fs;
    for (std::size_t k = 0; k < params.size(); ++k) {
        const Param& p = params[k];
        const Complex c{p.alpha, p.beta};
        const Complex up{-p.sigma / sp.fs, two_pi_over_fs * p.f};
        const Complex um{-p.sigma / sp.fs, -two_pi_over_fs * p.f};
        for (std::size_t j = 0; j < bins.size(); ++j) {
            const double nu = sp.nu(bins[j]);
            const Basis bp = hann_dtft(up, nu, sp.window());
            const Basis bm = hann_dtft(um, nu, sp.window());
            model[j] += 0.5 * (c * bp.s + std::conj(c) * bm.s);
            if (with_jacobian) {
                const Complex d_f = 0.5 * (c * bp.ds * (kI * two_pi_over_fs) +
                                           std::conj(c) * bm.ds * (-kI * two_pi_over_fs));
                const Complex d_sigma = 0.5 * (c * bp.ds + std::conj(c) * bm.ds) * (-1.0 / sp.fs);
                const Complex d_alpha = 0.5 * (bp.s + bm.s);
                const Complex d_beta = 0.5 * kI * (bp.s - bm.s);
                const std::size_t col = kParamsPerPartial * k;
                const std::array<Complex, kParamsPerPartial> derivs = {d_f, d_sigma, d_alpha,
                                                                       d_beta};
                for (std::size_t q = 0; q < kParamsPerPartial; ++q) {
                    ev.jacobian(2 * j, col + q) = derivs[q].real();
                    ev.jacobian(2 * j + 1, col + q) = derivs[q].imag();
                }
            }
        }
    }
    for (std::size_t j = 0; j < bins.size(); ++j) {
        const Complex r = sp.x[bins[j]] - model[j];
        ev.residual[2 * j] = r.real();
        ev.residual[2 * j + 1] = r.imag();
        ev.cost += r.real() * r.real() + r.imag() * r.imag();
    }
    return ev;
}

// Linear least squares for the complex amplitudes alone, other parameters held fixed.
void solve_amplitudes(const Spectrum& sp, std::vector<Param>& params,
                      const std::vector<std::size_t>& bins) {
    const Evaluation ev = evaluate(sp, params, bins, true);
    // Residual at the current amplitudes; solve J_ab * d = r for the amplitude columns only.
    const std::size_t k_count = params.size();
    Matrix a(ev.residual.size(), 2 * k_count);
    for (std::size_t r = 0; r < ev.residual.size(); ++r) {
        for (std::size_t k = 0; k < k_count; ++k) {
            a(r, 2 * k) = ev.jacobian(r, kParamsPerPartial * k + 2);
            a(r, 2 * k + 1) = ev.jacobian(r, kParamsPerPartial * k + 3);
        }
    }
    const std::vector<double> delta = solve_least_squares(std::move(a), ev.residual);
    for (std::size_t k = 0; k < k_count; ++k) {
        params[k].alpha += delta[2 * k];
        params[k].beta += delta[2 * k + 1];
    }
}

struct FitOutcome {
    int iterations = 0;
    bool converged = false;
    double relative_residual = 0.0;
};

// Levenberg-Marquardt on all parameters (frequency, decay, complex amplitude) of every partial.
FitOutcome levenberg_marquardt(const Spectrum& sp, std::vector<Param>& params,
                               const std::vector<std::size_t>& bins, int max_iterations) {
    FitOutcome outcome;
    double data_norm2 = 0.0;
    for (const std::size_t bin : bins) {
        data_norm2 += std::norm(sp.x[bin]);
    }
    solve_amplitudes(sp, params, bins);
    Evaluation current = evaluate(sp, params, bins, true);
    double lambda = 1e-9;
    const std::size_t count = kParamsPerPartial * params.size();
    const double duration = sp.duration();

    for (int iter = 0; iter < max_iterations; ++iter) {
        outcome.iterations = iter + 1;
        // Column scaling so that damping is relative to each parameter's influence.
        std::vector<double> scale(count, 1.0);
        for (std::size_t c = 0; c < count; ++c) {
            double norm = 0.0;
            for (std::size_t r = 0; r < current.residual.size(); ++r) {
                norm = std::hypot(norm, current.jacobian(r, c));
            }
            scale[c] = norm > 0.0 ? norm : 1.0;
        }
        bool accepted = false;
        double step_measure = std::numeric_limits<double>::infinity();
        for (int attempt = 0; attempt < 30 && !accepted; ++attempt) {
            Matrix aug(current.residual.size() + count, count);
            for (std::size_t r = 0; r < current.residual.size(); ++r) {
                for (std::size_t c = 0; c < count; ++c) {
                    aug(r, c) = current.jacobian(r, c) / scale[c];
                }
            }
            for (std::size_t c = 0; c < count; ++c) {
                aug(current.residual.size() + c, c) = std::sqrt(lambda);
            }
            std::vector<double> rhs(current.residual.size() + count, 0.0);
            std::copy(current.residual.begin(), current.residual.end(), rhs.begin());
            std::vector<double> delta;
            try {
                delta = solve_least_squares(std::move(aug), rhs);
            } catch (const std::runtime_error&) {
                lambda *= 10.0;
                continue;
            }
            std::vector<Param> trial = params;
            step_measure = 0.0;
            for (std::size_t k = 0; k < params.size(); ++k) {
                const std::size_t col = kParamsPerPartial * k;
                const double d_f = delta[col] / scale[col];
                const double d_sigma = delta[col + 1] / scale[col + 1];
                trial[k].f += d_f;
                trial[k].sigma += d_sigma;
                trial[k].alpha += delta[col + 2] / scale[col + 2];
                trial[k].beta += delta[col + 3] / scale[col + 3];
                step_measure = std::max(
                    {step_measure, std::fabs(d_f) * duration, std::fabs(d_sigma) * duration});
            }
            Evaluation next = evaluate(sp, trial, bins, true);
            if (std::isfinite(next.cost) && next.cost <= current.cost) {
                params = std::move(trial);
                current = std::move(next);
                lambda = std::max(lambda * 0.1, 1e-15);
                accepted = true;
            } else {
                lambda *= 10.0;
            }
        }
        if (!accepted) {
            // No step lowers the cost: at the limit of what the data and rounding allow.
            outcome.converged = true;
            break;
        }
        if (step_measure < 1e-10) {
            outcome.converged = true;
            break;
        }
    }
    outcome.relative_residual = data_norm2 > 0.0 ? std::sqrt(current.cost / data_norm2) : 0.0;
    return outcome;
}

// ---- Initial estimates -------------------------------------------------------------------------

// Parabolic interpolation of the log magnitude around bin m; returns the fractional bin.
double refine_peak(const Spectrum& sp, std::size_t m) {
    const double l = std::log(std::abs(sp.x[m - 1]) + 1e-300);
    const double c = std::log(std::abs(sp.x[m]) + 1e-300);
    const double r = std::log(std::abs(sp.x[m + 1]) + 1e-300);
    const double denom = l - 2.0 * c + r;
    const double delta = denom < 0.0 ? 0.5 * (l - r) / denom : 0.0;
    return static_cast<double>(m) + std::clamp(delta, -0.5, 0.5);
}

// Snaps an approximate frequency to the nearest spectral peak when no other approximation is near.
double snap_to_peak(const Spectrum& sp, const std::vector<double>& all, std::size_t index) {
    const double approx = all[index];
    const double natural_hz = 1.0 / sp.duration();
    for (std::size_t j = 0; j < all.size(); ++j) {
        if (j != index && std::fabs(all[j] - approx) < 4.0 * natural_hz) {
            return approx;
        }
    }
    const double centre = approx / sp.bin_hz();
    const auto half_width = static_cast<std::size_t>(1.5 * static_cast<double>(sp.m) / sp.n) + 1;
    const auto lo = static_cast<std::ptrdiff_t>(centre) - static_cast<std::ptrdiff_t>(half_width);
    const auto hi = static_cast<std::ptrdiff_t>(centre) + static_cast<std::ptrdiff_t>(half_width);
    const auto limit = static_cast<std::ptrdiff_t>(sp.m / 2) - 1;
    if (lo < 1 || hi > limit) {
        return approx;
    }
    std::ptrdiff_t best = lo;
    for (std::ptrdiff_t m = lo; m <= hi; ++m) {
        if (std::abs(sp.x[static_cast<std::size_t>(m)]) >
            std::abs(sp.x[static_cast<std::size_t>(best)])) {
            best = m;
        }
    }
    if (best == lo || best == hi) {
        return approx;
    }
    return refine_peak(sp, static_cast<std::size_t>(best)) * sp.bin_hz();
}

double initial_sigma(std::span<const double> signal, double fs) {
    const std::size_t half = signal.size() / 2;
    double e1 = 0.0;
    double e2 = 0.0;
    for (std::size_t i = 0; i < half; ++i) {
        e1 += signal[i] * signal[i];
        e2 += signal[half + i] * signal[half + i];
    }
    const double duration = static_cast<double>(signal.size()) / fs;
    double sigma = 1.0 / duration;
    if (e1 > 0.0 && e2 > 0.0 && e1 > e2) {
        sigma = std::log(e1 / e2) / duration;
    }
    return std::clamp(sigma, 0.05 / duration, 100.0 / duration);
}

std::vector<std::size_t> bins_near(const Spectrum& sp, const std::vector<double>& freqs,
                                   std::size_t radius) {
    const std::size_t top = sp.m / 2;
    std::vector<bool> used(top + 1, false);
    for (const double f : freqs) {
        const auto centre = static_cast<std::ptrdiff_t>(std::llround(f / sp.bin_hz()));
        const auto r = static_cast<std::ptrdiff_t>(radius);
        for (std::ptrdiff_t m = centre - r; m <= centre + r; ++m) {
            if (m >= 0 && m <= static_cast<std::ptrdiff_t>(top)) {
                used[static_cast<std::size_t>(m)] = true;
            }
        }
    }
    std::vector<std::size_t> bins;
    for (std::size_t m = 0; m <= top; ++m) {
        if (used[m]) {
            bins.push_back(m);
        }
    }
    return bins;
}

// Peak picking by successive subtraction ("CLEAN"): fit the strongest spectral peak as a damped
// sinusoid, subtract its closed-form spectrum from a working copy (so its window sidelobes cannot
// be mistaken for partials), and repeat until the strongest remaining peak is more than
// `peak_threshold_db` below the first one.
std::vector<double> pick_peaks(const Spectrum& sp, const PartialTrackingOptions& opt,
                               double sigma0) {
    Spectrum work = sp;
    const std::size_t half = sp.m / 2;
    const double per_natural = static_cast<double>(sp.m) / sp.n;  // bins per 1/T
    const std::size_t first = std::max<std::size_t>(2, static_cast<std::size_t>(1.5 * per_natural));
    const auto fit_radius = static_cast<std::size_t>(std::ceil(8.0 * per_natural));
    const auto clean_radius = static_cast<std::size_t>(std::ceil(64.0 * per_natural));
    const double min_sep_hz =
        opt.min_separation_hz > 0.0 ? opt.min_separation_hz : 3.0 / sp.duration();
    double strongest = 0.0;
    std::vector<double> freqs;
    const std::size_t max_rounds = 2 * opt.max_partials + 8;
    for (std::size_t round = 0; round < max_rounds && freqs.size() < opt.max_partials; ++round) {
        std::size_t best = 0;
        double best_mag = 0.0;
        for (std::size_t m = first; m + 1 < half; ++m) {
            const double mag = std::abs(work.x[m]);
            if (mag > best_mag && mag > std::abs(work.x[m - 1]) && mag >= std::abs(work.x[m + 1])) {
                best = m;
                best_mag = mag;
            }
        }
        if (best == 0) {
            break;
        }
        if (round == 0) {
            strongest = best_mag;
        }
        if (best_mag < strongest * std::pow(10.0, -opt.peak_threshold_db / 20.0)) {
            break;
        }
        double f = refine_peak(work, best) * work.bin_hz();
        std::vector<Param> single{{f, sigma0, 0.0, 0.0}};
        bool fitted = false;
        try {
            levenberg_marquardt(work, single, bins_near(work, {f}, fit_radius), 40);
            fitted = std::isfinite(single[0].f) && std::isfinite(single[0].sigma) &&
                     std::fabs(single[0].f - f) < 2.0 / work.duration();
        } catch (const std::runtime_error&) {
            fitted = false;
        }
        const std::vector<std::size_t> region = bins_near(work, {f}, clean_radius);
        if (fitted) {
            f = single[0].f;
            const Evaluation ev = evaluate(work, single, region, false);
            for (std::size_t j = 0; j < region.size(); ++j) {
                work.x[region[j]] = Complex{ev.residual[2 * j], ev.residual[2 * j + 1]};
            }
        } else {
            for (const std::size_t bin : bins_near(work, {f}, fit_radius)) {
                work.x[bin] = Complex{0.0, 0.0};
            }
        }
        const bool close = std::any_of(freqs.begin(), freqs.end(),
                                       [&](double g) { return std::fabs(g - f) < min_sep_hz; });
        if (!close && fitted) {
            freqs.push_back(f);
        }
    }
    std::sort(freqs.begin(), freqs.end());
    return freqs;
}

// ---- Envelope ----------------------------------------------------------------------------------

// Amplitude of each partial in consecutive frames, by linear least squares with the fitted
// frequencies and decay rates held fixed (all partials jointly, so close partials separate).
void compute_envelopes(std::span<const double> signal, double fs,
                       std::vector<PartialEstimate>& partials, double frame_s) {
    const auto frame_len = static_cast<std::size_t>(std::llround(frame_s * fs));
    if (frame_len < 8 || partials.empty()) {
        return;
    }
    const std::size_t frames = signal.size() / frame_len;
    const std::size_t k_count = partials.size();
    if (frame_len < 2 * k_count) {
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t start = frame * frame_len;
        const double centre =
            (static_cast<double>(start) + 0.5 * static_cast<double>(frame_len)) / fs;
        Matrix a(frame_len, 2 * k_count);
        for (std::size_t i = 0; i < frame_len; ++i) {
            const double dt = (static_cast<double>(start + i) / fs) - centre;
            for (std::size_t k = 0; k < k_count; ++k) {
                const double env = std::exp(-partials[k].sigma_per_s * dt);
                const double angle = kTwoPi * partials[k].frequency_hz * dt;
                a(i, 2 * k) = env * std::cos(angle);
                a(i, 2 * k + 1) = -env * std::sin(angle);
            }
        }
        std::vector<double> coeffs;
        bool ok = true;
        try {
            coeffs = solve_least_squares(std::move(a), signal.subspan(start, frame_len));
        } catch (const std::runtime_error&) {
            ok = false;
        }
        for (std::size_t k = 0; k < k_count; ++k) {
            partials[k].envelope_times_s.push_back(centre);
            partials[k].envelope_amplitudes.push_back(
                ok ? std::hypot(coeffs[2 * k], coeffs[2 * k + 1])
                   : std::numeric_limits<double>::quiet_NaN());
        }
    }
}

}  // namespace

PartialTrackingResult track_partials(std::span<const double> signal, double sample_rate_hz,
                                     const PartialTrackingOptions& options) {
    if (signal.size() < 16 || !(sample_rate_hz > 0.0)) {
        throw std::invalid_argument("track_partials: need >= 16 samples and a positive rate");
    }
    const Spectrum sp = compute_spectrum(signal, sample_rate_hz);
    std::vector<double> freqs;
    if (options.approx_frequencies_hz.empty()) {
        freqs = pick_peaks(sp, options, initial_sigma(signal, sample_rate_hz));
    } else {
        for (std::size_t k = 0; k < options.approx_frequencies_hz.size(); ++k) {
            freqs.push_back(snap_to_peak(sp, options.approx_frequencies_hz, k));
        }
    }
    PartialTrackingResult result;
    if (freqs.empty()) {
        return result;
    }

    const double sigma0 = initial_sigma(signal, sample_rate_hz);
    const auto radius = static_cast<std::size_t>(std::ceil(8.0 * static_cast<double>(sp.m) / sp.n));
    std::vector<Param> params(freqs.size());
    for (std::size_t k = 0; k < freqs.size(); ++k) {
        params[k].f = freqs[k];
        params[k].sigma = sigma0;
    }

    // Stage 1: each partial alone on its own bins, for decay and amplitude starting values.
    for (std::size_t k = 0; k < freqs.size(); ++k) {
        std::vector<Param> single{params[k]};
        try {
            levenberg_marquardt(sp, single, bins_near(sp, {freqs[k]}, radius), 40);
            if (std::isfinite(single[0].f) && std::isfinite(single[0].sigma) &&
                std::fabs(single[0].f - freqs[k]) < 2.0 / sp.duration()) {
                params[k] = single[0];
            }
        } catch (const std::runtime_error&) {
            // Keep the starting values; the joint fit below still runs.
            continue;
        }
    }

    // Stage 2: all partials jointly.
    const FitOutcome outcome =
        levenberg_marquardt(sp, params, bins_near(sp, freqs, radius), options.max_iterations);
    result.iterations = outcome.iterations;
    result.converged = outcome.converged;
    result.relative_residual = outcome.relative_residual;

    for (const Param& p : params) {
        PartialEstimate est;
        est.frequency_hz = p.f;
        est.sigma_per_s = p.sigma;
        est.t60_s = p.sigma > 0.0 ? 3.0 * std::numbers::ln10 / p.sigma
                                  : std::numeric_limits<double>::infinity();
        est.amplitude = std::hypot(p.alpha, p.beta);
        est.phase_rad = std::atan2(p.beta, p.alpha);
        result.partials.push_back(std::move(est));
    }
    if (options.envelope_frame_s > 0.0) {
        compute_envelopes(signal, sample_rate_hz, result.partials, options.envelope_frame_s);
    }
    return result;
}

}  // namespace pme::analysis
