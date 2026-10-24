// SPDX-License-Identifier: MIT
// Shared helpers of the A8 tests: aligned buffers, a fixed-seed generator, realistic mode-bank
// coefficients, target iteration and error norms.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/simd/dispatch.hpp"
#include "pme/simd/kernels.hpp"

namespace simdtest {

/// 64-byte-aligned buffer of at least `n` elements (padded to a multiple of 16), zero-filled.
template <class T>
class Buf {
public:
    explicit Buf(std::size_t n = 0) : n_(n) { mem_.allocate(((n + 15U) / 16U) * 16U + 16U); }
    [[nodiscard]] T* data() noexcept { return mem_.data(); }
    [[nodiscard]] const T* data() const noexcept { return mem_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    T& operator[](std::size_t i) noexcept { return mem_.data()[i]; }
    const T& operator[](std::size_t i) const noexcept { return mem_.data()[i]; }
    void copyFrom(const Buf& o) { std::copy(o.data(), o.data() + o.size(), data()); }

private:
    pme::osc::AlignedArray<T> mem_;
    std::size_t n_;
};

/// splitmix64: a fixed, portable generator (std distributions differ between standard libraries).
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {
        s_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = s_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }
    /// Uniform in [lo, hi).
    double uniform(double lo, double hi) {
        const double u = static_cast<double>(next() >> 11U) * (1.0 / 9007199254740992.0);
        return lo + (hi - lo) * u;
    }

private:
    std::uint64_t s_;
};

/// The sizes of A8-T1.
inline const std::vector<std::size_t>& sizes() {
    static const std::vector<std::size_t> s{1, 15, 16, 17, 64, 1023, 1024, 4099};
    return s;
}

/// Mode-bank coefficients of modes spread logarithmically from 20 Hz to 20 kHz at 48 kHz, with
/// decay rates 0.5 to 50 1/s (A8-T1: realistic magnitudes).
struct Coefficients {
    Buf<float> beta, kappa, gamma;
    Buf<double> beta64, kappa64, gamma64;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): count then seed
    explicit Coefficients(std::size_t n, std::uint64_t seed = 1)
        : beta(n), kappa(n), gamma(n), beta64(n), kappa64(n), gamma64(n) {
        Rng rng(seed);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = (n > 1) ? static_cast<double>(i) / static_cast<double>(n - 1) : 0.5;
            const double f = 20.0 * std::pow(1000.0, t);
            const double sigma = rng.uniform(0.5, 50.0);
            pme::OscCoeffs c;
            const auto status =
                pme::compileOscillator(2.0 * std::numbers::pi * f, sigma, 48000.0, c);
            if (status != pme::Status::Ok) {
                c = pme::OscCoeffs{0.99F, 0.01F, 1e-9F, 0.0, 0.0};
            }
            beta[i] = c.beta;
            kappa[i] = c.kappa;
            gamma[i] = c.gamma;
            beta64[i] = static_cast<double>(c.beta);
            kappa64[i] = static_cast<double>(c.kappa);
            gamma64[i] = static_cast<double>(c.gamma);
        }
    }
};

template <class T>
void fillRandom(Buf<T>& b, Rng& rng, double lo, double hi) {
    for (std::size_t i = 0; i < b.size(); ++i) {
        b[i] = static_cast<T>(rng.uniform(lo, hi));
    }
}

/// Prints one line to standard output (the test report collects these lines).
template <class... Args>
void log(const Args&... args) {
    std::ostringstream out;
    (out << ... << args);  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    out << '\n';
    std::fputs(out.str().c_str(), stdout);
}

/// Runs `fn(index, name, fused)` once per supported and compiled target, restoring the default
/// dispatch afterwards. Prints the targets that ran (A8-T1 reporting).
template <class F>
void forEachTarget(const char* label, F fn) {
    pme::simd::initialise();
    std::string ran;
    for (std::size_t t = 0; t < pme::simd::testTargetCount(); ++t) {
        pme::simd::testSelectTarget(t);
        fn(t, pme::simd::testTargetName(t), pme::simd::activeTargetIsFused());
        ran += std::string(" ") + pme::simd::testTargetName(t);
    }
    pme::simd::testResetTarget();
    log("[simd] ", label, ": targets that ran:", ran);
}

/// Relative error of a reduction against `ref`: |x - ref| / max(|ref|, sumAbs / 4). The second
/// term only matters when the sum cancels (|ref| < sumAbs / 4): then no float32 result can be
/// better than a few float32 roundings (2^-24) of the partial sums, which are of the order of
/// sumAbs, so the bound 1e-6 * sumAbs / 4 is about four float32 epsilons of sumAbs (A8-T1 norm).
inline double reductionError(double x, double ref, double sumAbs) {
    return std::abs(x - ref) / std::max(std::abs(ref), sumAbs / 4.0);
}

}  // namespace simdtest
