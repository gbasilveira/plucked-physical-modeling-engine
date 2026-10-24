// SPDX-License-Identifier: MIT
// Discrete Fourier transform for the analysis library (P0.3-R3). Written here so that the library
// has no third-party dependency beyond JSON (ADR-0005). Radix-2 for powers of two, Bluestein's
// chirp-z algorithm for every other length. float64 throughout; not for the audio thread.
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace pme::analysis {

using Complex = std::complex<double>;

// Smallest power of two that is >= n (and >= 1).
std::size_t next_pow2(std::size_t n);

// Forward transform X[k] = sum_n x[n] exp(-2 pi i k n / N), any length N >= 1.
std::vector<Complex> fft(std::span<const Complex> x);

// Inverse transform x[n] = (1/N) sum_k X[k] exp(+2 pi i k n / N).
std::vector<Complex> ifft(std::span<const Complex> x);

// Forward transform of a real signal zero-padded to `size` (>= x.size()); returns all `size` bins.
std::vector<Complex> fft_real(std::span<const double> x, std::size_t size);

// Direct O(N^2) transform; the reference the fast transform is tested against.
std::vector<Complex> dft_direct(std::span<const Complex> x);

}  // namespace pme::analysis
