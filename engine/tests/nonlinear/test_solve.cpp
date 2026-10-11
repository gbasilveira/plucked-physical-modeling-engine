// SPDX-License-Identifier: MIT
// Covers: A3-R3, A3-T1
// The Woodbury solve against a dense LU solve on random systems of the SPEC-NL structure.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "test_support.hpp"

namespace {

// Dense solve of A x = b by LU with partial pivoting (written independently of the engine).
std::vector<double> denseSolve(std::vector<double> A, std::vector<double> b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t piv = col;
        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(A[row * n + col]) > std::abs(A[piv * n + col])) {
                piv = row;
            }
        }
        if (piv != col) {
            for (std::size_t k = 0; k < n; ++k) {
                std::swap(A[piv * n + k], A[col * n + k]);
            }
            std::swap(b[piv], b[col]);
        }
        for (std::size_t row = col + 1; row < n; ++row) {
            const double f = A[row * n + col] / A[col * n + col];
            for (std::size_t k = col; k < n; ++k) {
                A[row * n + k] -= f * A[col * n + k];
            }
            b[row] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            s -= A[i * n + k] * x[k];
        }
        x[i] = s / A[i * n + i];
    }
    return x;
}

}  // namespace

TEST_CASE("A3-T1 Woodbury result equals a dense solve within 1e-12 on random systems") {
    pme::nl::test::Rng rng(20261011U);
    double worst = 0.0;
    for (int sys = 0; sys < 1000; ++sys) {
        const std::size_t n = 1 + rng.index(256);
        const std::size_t r = 1 + rng.index(4);
        // Columns have the SPEC-NL structure: h_j = diag(gamma/m) d_j * s_j, w_j = d_j * t_j with
        // positive scalings, so that W^T H has non-negative eigenvalues. Scales are chosen so that
        // w^T h is O(1) (hammer-like systems), which keeps I + H W^T well conditioned.
        std::vector<double> gm(n);
        for (double& x : gm) {
            x = rng.uniform(0.1, 10.0);
        }
        std::vector<std::vector<double>> h(r, std::vector<double>(n));
        std::vector<std::vector<double>> w(r, std::vector<double>(n));
        for (std::size_t j = 0; j < r; ++j) {
            std::vector<double> d(n);
            double norm2 = 0.0;
            for (double& x : d) {
                x = rng.uniform(-1.0, 1.0);
                norm2 += x * x;
            }
            const double scale = rng.uniform(0.05, 5.0) / (norm2 + 1e-3);
            for (std::size_t i = 0; i < n; ++i) {
                h[j][i] = gm[i] * d[i];
                w[j][i] = d[i] * scale;
            }
        }
        std::vector<double> a(n);
        for (double& x : a) {
            x = rng.uniform(-3.0, 3.0);
        }
        std::vector<const double*> hp(r);
        std::vector<const double*> wp(r);
        for (std::size_t j = 0; j < r; ++j) {
            hp[j] = h[j].data();
            wp[j] = w[j].data();
        }
        std::vector<double> zW(n);
        pme::nl::lowRankSolve(zW.data(), a.data(), n, r, hp.data(), wp.data());

        // Dense: (I + sum_j h_j w_j^T) z = a.
        std::vector<double> A(n * n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            A[i * n + i] = 1.0;
            for (std::size_t k = 0; k < n; ++k) {
                for (std::size_t j = 0; j < r; ++j) {
                    A[i * n + k] += h[j][i] * w[j][k];
                }
            }
        }
        const std::vector<double> zD = denseSolve(A, a, n);
        double num = 0.0;
        double den = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            num += (zW[i] - zD[i]) * (zW[i] - zD[i]);
            den += zD[i] * zD[i];
        }
        const double rel = std::sqrt(num / den);
        worst = std::max(worst, rel);
        REQUIRE(rel <= 1e-12);
    }
    std::printf("A3-T1 worst relative error over 1000 systems: %.3e\n", worst);
}

TEST_CASE("A3-R3 r = 0 copies and aliasing z with a is allowed") {
    std::vector<double> a{1.0, 2.0, 3.0};
    std::vector<double> z(3);
    pme::nl::lowRankSolve(z.data(), a.data(), 3, 0, nullptr, nullptr);
    CHECK(z == a);
    const std::vector<double> h{0.5, 0.5, 0.5};
    const std::vector<double> w{1.0, 1.0, 1.0};
    const double* hp[1] = {h.data()};
    const double* wp[1] = {w.data()};
    std::vector<double> alias = a;
    pme::nl::lowRankSolve(alias.data(), alias.data(), 3, 1, hp, wp);
    pme::nl::lowRankSolve(z.data(), a.data(), 3, 1, hp, wp);
    CHECK(alias == z);
    // r = 1 closed form: a - h (w.a) / (1 + w.h) with w.a = 6, w.h = 1.5.
    CHECK(z[0] == doctest::Approx(1.0 - 0.5 * 6.0 / 2.5));
}
