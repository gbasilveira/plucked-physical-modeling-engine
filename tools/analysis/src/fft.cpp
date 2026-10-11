// SPDX-License-Identifier: MIT
#include "pme/analysis/fft.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace pme::analysis {

namespace {

// exp(-2 pi i k / n) computed directly (not by recurrence) so twiddle error stays at rounding level.
Complex twiddle(std::size_t k, std::size_t n) {
    const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
    return {std::cos(angle), std::sin(angle)};
}

// In-place iterative radix-2 transform; data.size() must be a power of two. `inverse` flips the sign
// of the exponent and does not scale.
void fft_pow2_in_place(std::vector<Complex>& data, bool inverse) {
    const std::size_t n = data.size();
    if (n <= 1) {
        return;
    }
    const unsigned bits = static_cast<unsigned>(std::countr_zero(n));
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t j = 0;
        for (unsigned b = 0; b < bits; ++b) {
            j |= ((i >> b) & 1U) << (bits - 1U - b);
        }
        if (j > i) {
            std::swap(data[i], data[j]);
        }
    }
    std::vector<Complex> roots(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        roots[k] = twiddle(k, n);
        if (inverse) {
            roots[k] = std::conj(roots[k]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1U) {
        const std::size_t half = len / 2;
        const std::size_t stride = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const Complex t = roots[k * stride] * data[start + k + half];
                const Complex u = data[start + k];
                data[start + k] = u + t;
                data[start + k + half] = u - t;
            }
        }
    }
}

// Bluestein: X[k] = conj-chirp[k] * sum_n (x[n] chirp[n]) * conj-chirp[k - n], with
// chirp[n] = exp(-i pi n^2 / N), evaluated as a circular convolution of power-of-two length.
std::vector<Complex> bluestein(std::span<const Complex> x, bool inverse) {
    const std::size_t n = x.size();
    const std::size_t m = next_pow2(2 * n - 1);
    std::vector<Complex> chirp(n);
    for (std::size_t i = 0; i < n; ++i) {
        // n^2 mod 2N keeps the angle small and exact for large n.
        const std::uint64_t sq = (static_cast<std::uint64_t>(i) * i) % (2ULL * n);
        const double angle = -std::numbers::pi * static_cast<double>(sq) / static_cast<double>(n);
        chirp[i] = {std::cos(angle), inverse ? -std::sin(angle) : std::sin(angle)};
    }
    std::vector<Complex> a(m, Complex{0.0, 0.0});
    std::vector<Complex> b(m, Complex{0.0, 0.0});
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = x[i] * chirp[i];
    }
    b[0] = std::conj(chirp[0]);
    for (std::size_t i = 1; i < n; ++i) {
        b[i] = std::conj(chirp[i]);
        b[m - i] = std::conj(chirp[i]);
    }
    fft_pow2_in_place(a, false);
    fft_pow2_in_place(b, false);
    for (std::size_t i = 0; i < m; ++i) {
        a[i] *= b[i];
    }
    fft_pow2_in_place(a, true);
    std::vector<Complex> out(n);
    const double scale = 1.0 / static_cast<double>(m);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a[i] * scale * chirp[i];
    }
    return out;
}

std::vector<Complex> transform(std::span<const Complex> x, bool inverse) {
    const std::size_t n = x.size();
    if (n == 0) {
        throw std::invalid_argument("fft: empty input");
    }
    if (std::has_single_bit(n)) {
        std::vector<Complex> data(x.begin(), x.end());
        fft_pow2_in_place(data, inverse);
        return data;
    }
    return bluestein(x, inverse);
}

}  // namespace

std::size_t next_pow2(std::size_t n) {
    return n <= 1 ? 1 : std::bit_ceil(n);
}

std::vector<Complex> fft(std::span<const Complex> x) {
    return transform(x, false);
}

std::vector<Complex> ifft(std::span<const Complex> x) {
    std::vector<Complex> out = transform(x, true);
    const double scale = 1.0 / static_cast<double>(x.size());
    for (Complex& v : out) {
        v *= scale;
    }
    return out;
}

std::vector<Complex> fft_real(std::span<const double> x, std::size_t size) {
    if (size < x.size() || size == 0) {
        throw std::invalid_argument("fft_real: size must be >= signal length and >= 1");
    }
    std::vector<Complex> data(size, Complex{0.0, 0.0});
    for (std::size_t i = 0; i < x.size(); ++i) {
        data[i] = Complex{x[i], 0.0};
    }
    if (std::has_single_bit(size)) {
        fft_pow2_in_place(data, false);
        return data;
    }
    return bluestein(data, false);
}

std::vector<Complex> dft_direct(std::span<const Complex> x) {
    const std::size_t n = x.size();
    std::vector<Complex> out(n, Complex{0.0, 0.0});
    for (std::size_t k = 0; k < n; ++k) {
        Complex sum{0.0, 0.0};
        for (std::size_t j = 0; j < n; ++j) {
            sum += x[j] * twiddle((k * j) % n, n);
        }
        out[k] = sum;
    }
    return out;
}

}  // namespace pme::analysis
