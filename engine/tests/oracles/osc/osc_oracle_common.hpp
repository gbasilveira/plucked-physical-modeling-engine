// SPDX-License-Identifier: MIT
// Shared helpers of the A1 oracle tests ORC-01 and ORC-02. Everything here is derived from SPEC-OSC
// ("The modal oscillator") and SPEC-CONV, not from the engine sources: float64 coefficients from
// the SI-0008 cancellation-free formula, the difference-form recursion as a template, the discrete
// energy, and a damped-cosine least-squares fit.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace pme::oracle {

inline constexpr double kPi = std::numbers::pi;

/// 3 ln(10): the decay rate times T60 for a 60 dB amplitude drop (sigma = kSixtyDb / T60).
inline constexpr double kSixtyDb = 3.0 * std::numbers::ln10;

/// Formats a value with six significant digits in scientific notation for the report lines.
inline std::string sci(double v) {
    std::ostringstream out;
    out << std::scientific << std::setprecision(3) << v;
    return out.str();
}

/// Writes one report line (achieved errors) to stderr; ctest shows it with --output-on-failure -V.
inline void report(const std::string& line) {
    std::cerr << line << std::endl;  // NOLINT(performance-avoid-endl)
}

/// float64 coefficients of one oscillator, SPEC-OSC "Discrete scheme" and "Difference form".
struct Coeffs64 {
    double beta{};
    double kappa{};
    double gamma{};
    double omegaHat2{};  ///< rad^2/s^2
    double sigmaHat{};   ///< 1/s
    double omegaD{};     ///< rad/s
};

/// SPEC-OSC: sigmaHat dt = tanh(sigma dt); omegaHat^2 dt^2 = 4 [sinh^2(sigma dt / 2) +
/// sin^2(omega_d dt / 2)] / cosh(sigma dt); omega_d dt = dt sqrt((omega - sigma)(omega + sigma)).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Coeffs64 coefficients64(double omega, double sigma, double fs) {
    const double dt = 1.0 / fs;
    const double x = sigma * dt;
    const double wdDt = dt * std::sqrt((omega - sigma) * (omega + sigma));
    const double sh = std::sinh(0.5 * x);
    const double sn = std::sin(0.5 * wdDt);
    const double w2dt2 = 4.0 * (sh * sh + sn * sn) / std::cosh(x);
    const double sigHatDt = std::tanh(x);
    Coeffs64 c;
    c.beta = (1.0 - sigHatDt) / (1.0 + sigHatDt);
    c.kappa = w2dt2 / (1.0 + sigHatDt);
    c.gamma = dt * dt / (1.0 + sigHatDt);
    c.omegaHat2 = w2dt2 * fs * fs;
    c.sigmaHat = sigHatDt * fs;
    c.omegaD = wdDt * fs;
    return c;
}

/// One oscillator in difference form (SPEC-OSC): stores q^n and u^n = q^n - q^(n-1).
/// u^(n+1) = beta u^n - kappa q^n + gamma f^n; q^(n+1) = q^n + u^(n+1), in this operation order.
template <typename T>
struct Oscillator {
    T beta{};
    T kappa{};
    T gamma{};
    T q{};
    T u{};

    /// Advances one step with force f^n and returns u^n (the value before the step).
    T step(T f) {
        const T uOld = u;
        u = beta * u - kappa * q + gamma * f;
        q = q + u;
        return uOld;
    }
};

/// SPEC-OSC "Discrete energy": H^(n+1/2) = 1/2 (u^(n+1)/dt)^2 + 1/2 omegaHat^2 q^n q^(n+1).
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
inline double halfStepEnergy(double uNext, double qNow, double qNext, double omegaHat2, double dt) {
    const double v = uNext / dt;
    return 0.5 * v * v + 0.5 * omegaHat2 * qNow * qNext;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

/// Result of fitting y(t) = A exp(-s t) cos(w t + phi) to samples y[k] at t = k dt.
struct DampedCosineFit {
    double amplitude{};
    double sigma{};  ///< 1/s
    double omega{};  ///< rad/s
    double phase{};
    double rms{};  ///< RMS residual
    bool converged{};
};

namespace detail {

// Solves the 4x4 system a x = b in place (Gaussian elimination, partial pivoting).
inline bool solve4(std::array<std::array<double, 4>, 4>& a, std::array<double, 4>& b) {
    for (std::size_t col = 0; col < 4; ++col) {
        std::size_t piv = col;
        for (std::size_t r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) {
                piv = r;
            }
        }
        if (a[piv][col] == 0.0) {
            return false;
        }
        std::swap(a[piv], a[col]);
        std::swap(b[piv], b[col]);
        for (std::size_t r = col + 1; r < 4; ++r) {
            const double m = a[r][col] / a[col][col];
            for (std::size_t c = col; c < 4; ++c) {
                a[r][c] -= m * a[col][c];
            }
            b[r] -= m * b[col];
        }
    }
    for (std::size_t i = 4; i-- > 0;) {
        double s = b[i];
        for (std::size_t c = i + 1; c < 4; ++c) {
            s -= a[i][c] * b[c];
        }
        b[i] = s / a[i][i];
    }
    return true;
}

}  // namespace detail

/// Gauss-Newton least-squares fit of a damped cosine to the first `count` samples, starting from
/// `start`. Independent of pme_analysis.
inline DampedCosineFit fitDampedCosine(const std::vector<double>& y, std::size_t count, double dt,
                                       DampedCosineFit start) {
    DampedCosineFit p = start;
    std::array<double, 4> par{p.amplitude, p.sigma, p.omega, p.phase};
    std::vector<double> jac(count * 4U);
    std::vector<double> res(count);
    p.converged = false;
    for (int iter = 0; iter < 100; ++iter) {
        double sse = 0.0;
        for (std::size_t k = 0; k < count; ++k) {
            const double t = static_cast<double>(k) * dt;
            const double e = std::exp(-par[1] * t);
            const double th = par[2] * t + par[3];
            const double c = std::cos(th);
            const double s = std::sin(th);
            res[k] = y[k] - par[0] * e * c;
            sse += res[k] * res[k];
            jac[k * 4U + 0U] = e * c;
            jac[k * 4U + 1U] = -t * par[0] * e * c;
            jac[k * 4U + 2U] = -t * par[0] * e * s;
            jac[k * 4U + 3U] = -par[0] * e * s;
        }
        p.rms = std::sqrt(sse / static_cast<double>(count));
        // Column scaling keeps the normal equations well conditioned (t spans seconds).
        std::array<double, 4> scale{};
        for (std::size_t j = 0; j < 4; ++j) {
            double n2 = 0.0;
            for (std::size_t k = 0; k < count; ++k) {
                n2 += jac[k * 4U + j] * jac[k * 4U + j];
            }
            scale[j] = n2 > 0.0 ? 1.0 / std::sqrt(n2) : 1.0;
        }
        std::array<std::array<double, 4>, 4> a{};
        std::array<double, 4> b{};
        for (std::size_t k = 0; k < count; ++k) {
            for (std::size_t i = 0; i < 4; ++i) {
                const double ji = jac[k * 4U + i] * scale[i];
                b[i] += ji * res[k];
                for (std::size_t j = 0; j < 4; ++j) {
                    a[i][j] += ji * jac[k * 4U + j] * scale[j];
                }
            }
        }
        if (!detail::solve4(a, b)) {
            return p;
        }
        // Convergence: every step changes the model by less than 1e-10 over the window (the rates
        // sigma and omega are weighted by the window length, the phase and amplitude are absolute).
        const double span = static_cast<double>(count) * dt;
        double maxStep = 0.0;
        for (std::size_t j = 0; j < 4; ++j) {
            const double d = b[j] * scale[j];
            par[j] += d;
            const double weight = (j == 1U || j == 2U) ? span : 1.0;
            maxStep = std::max(maxStep, std::fabs(d) * weight);
        }
        if (maxStep < 1e-10) {
            p.converged = true;
            break;
        }
    }
    p.amplitude = par[0];
    p.sigma = par[1];
    p.omega = par[2];
    p.phase = par[3];
    return p;
}

}  // namespace pme::oracle
