// SPDX-License-Identifier: MIT
// P0.3-R3: the library's own FFT and dense solves, each against a direct implementation.

#include <algorithm>
#include <cmath>
#include <vector>

#include <pme/analysis/fft.hpp>
#include <pme/analysis/linalg.hpp>
#include <pme/test/tolerance.hpp>

#include "testdata.hpp"

using pme::analysis::Complex;
using pme::analysis::testing::Uniform;

namespace {

std::vector<Complex> random_signal(std::size_t n, std::uint64_t seed) {
    Uniform rng(seed);
    std::vector<Complex> x(n);
    for (Complex& v : x) {
        const double re = rng.symmetric();
        const double im = rng.symmetric();
        v = {re, im};
    }
    return x;
}

double max_abs_diff(const std::vector<Complex>& a, const std::vector<Complex>& b) {
    double worst = 0.0;
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

}  // namespace

TEST_CASE("P0.3-R3 fft matches the direct DFT for power-of-two and arbitrary lengths") {
    for (const std::size_t n : {1U, 2U, 3U, 8U, 12U, 17U, 64U, 100U, 255U, 256U, 1000U}) {
        const std::vector<Complex> x = random_signal(n, 100 + n);
        const std::vector<Complex> fast = pme::analysis::fft(x);
        const std::vector<Complex> slow = pme::analysis::dft_direct(x);
        INFO("length " << n);
        CHECK(max_abs_diff(fast, slow) < 1e-12 * static_cast<double>(n));
    }
}

TEST_CASE("P0.3-R3 ifft inverts fft") {
    for (const std::size_t n : {16U, 45U, 128U, 997U}) {
        const std::vector<Complex> x = random_signal(n, 7 + n);
        const std::vector<Complex> back = pme::analysis::ifft(pme::analysis::fft(x));
        INFO("length " << n);
        CHECK(max_abs_diff(x, back) < 1e-13);
    }
}

TEST_CASE("P0.3-R3 fft_real zero-pads and matches the complex transform") {
    Uniform rng(5);
    std::vector<double> x(37);
    for (double& v : x) {
        v = rng.symmetric();
    }
    for (const std::size_t size : {37U, 64U, 100U}) {
        std::vector<Complex> padded(size, Complex{0.0, 0.0});
        for (std::size_t i = 0; i < x.size(); ++i) {
            padded[i] = x[i];
        }
        CHECK(max_abs_diff(pme::analysis::fft_real(x, size), pme::analysis::dft_direct(padded)) < 1e-11);
    }
    CHECK_THROWS_AS(pme::analysis::fft_real(x, 10), std::invalid_argument);
}

TEST_CASE("P0.3-R3 least squares recovers the exact solution of a consistent system") {
    Uniform rng(11);
    const std::size_t rows = 40;
    const std::size_t cols = 6;
    pme::analysis::Matrix a(rows, cols);
    std::vector<double> truth(cols);
    for (double& v : truth) {
        v = rng.symmetric();
    }
    std::vector<double> b(rows, 0.0);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            a(r, c) = rng.symmetric();
            b[r] += a(r, c) * truth[c];
        }
    }
    const std::vector<double> x = pme::analysis::solve_least_squares(a, b);
    for (std::size_t c = 0; c < cols; ++c) {
        CHECK(std::fabs(x[c] - truth[c]) < 1e-13);
    }
}

TEST_CASE("P0.3-R3 least squares residual is orthogonal to the columns") {
    Uniform rng(13);
    const std::size_t rows = 30;
    const std::size_t cols = 4;
    pme::analysis::Matrix a(rows, cols);
    std::vector<double> b(rows);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            a(r, c) = rng.symmetric();
        }
        b[r] = rng.symmetric();
    }
    const std::vector<double> x = pme::analysis::solve_least_squares(a, b);
    for (std::size_t c = 0; c < cols; ++c) {
        double dot = 0.0;
        for (std::size_t r = 0; r < rows; ++r) {
            double fit = 0.0;
            for (std::size_t k = 0; k < cols; ++k) {
                fit += a(r, k) * x[k];
            }
            dot += a(r, c) * (b[r] - fit);
        }
        CHECK(std::fabs(dot) < 1e-12);
    }
}

TEST_CASE("P0.3-R3 least squares reports rank deficiency") {
    pme::analysis::Matrix a(5, 2);
    for (std::size_t r = 0; r < 5; ++r) {
        a(r, 0) = static_cast<double>(r + 1);
        a(r, 1) = 2.0 * static_cast<double>(r + 1);
    }
    const std::vector<double> b(5, 1.0);
    CHECK_THROWS_AS(pme::analysis::solve_least_squares(a, b), std::runtime_error);
}

TEST_CASE("P0.3-R3 square solve matches a hand-solved system") {
    pme::analysis::Matrix a(3, 3);
    const double values[3][3] = {{0.0, 2.0, 1.0}, {1.0, 1.0, 1.0}, {2.0, 0.0, 3.0}};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            a(r, c) = values[r][c];
        }
    }
    // x = (1, 2, 3): b = A x.
    const std::vector<double> b = {7.0, 6.0, 11.0};
    const std::vector<double> x = pme::analysis::solve_square(a, b);
    CHECK(std::fabs(x[0] - 1.0) < 1e-14);
    CHECK(std::fabs(x[1] - 2.0) < 1e-14);
    CHECK(std::fabs(x[2] - 3.0) < 1e-14);
    pme::analysis::Matrix singular(2, 2);
    singular(0, 0) = 1.0;
    singular(0, 1) = 2.0;
    singular(1, 0) = 2.0;
    singular(1, 1) = 4.0;
    CHECK_THROWS_AS(pme::analysis::solve_square(singular, std::vector<double>{1.0, 2.0}),
                    std::runtime_error);
}
