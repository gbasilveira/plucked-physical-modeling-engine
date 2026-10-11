// SPDX-License-Identifier: MIT
#include "pme/analysis/linalg.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pme::analysis {

std::vector<double> solve_least_squares(Matrix a, std::span<const double> b) {
    const std::size_t m = a.rows();
    const std::size_t n = a.cols();
    if (b.size() != m || m < n || n == 0) {
        throw std::invalid_argument("solve_least_squares: shape mismatch");
    }
    std::vector<double> rhs(b.begin(), b.end());

    // Householder reflections applied to A and b in place; R ends up in the upper triangle.
    double max_diag = 0.0;
    std::vector<double> diag(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        double norm = 0.0;
        for (std::size_t i = k; i < m; ++i) {
            norm = std::hypot(norm, a(i, k));
        }
        if (norm == 0.0) {
            throw std::runtime_error("solve_least_squares: zero column (rank deficient)");
        }
        const double alpha = a(k, k) > 0.0 ? -norm : norm;
        // v = x - alpha e1, stored in column k from row k downwards.
        a(k, k) -= alpha;
        double vnorm2 = 0.0;
        for (std::size_t i = k; i < m; ++i) {
            vnorm2 += a(i, k) * a(i, k);
        }
        if (vnorm2 > 0.0) {
            for (std::size_t j = k + 1; j < n; ++j) {
                double dot = 0.0;
                for (std::size_t i = k; i < m; ++i) {
                    dot += a(i, k) * a(i, j);
                }
                const double f = 2.0 * dot / vnorm2;
                for (std::size_t i = k; i < m; ++i) {
                    a(i, j) -= f * a(i, k);
                }
            }
            double dot = 0.0;
            for (std::size_t i = k; i < m; ++i) {
                dot += a(i, k) * rhs[i];
            }
            const double f = 2.0 * dot / vnorm2;
            for (std::size_t i = k; i < m; ++i) {
                rhs[i] -= f * a(i, k);
            }
        }
        diag[k] = alpha;
        max_diag = std::max(max_diag, std::fabs(alpha));
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (std::fabs(diag[k]) <= max_diag * 1e-14) {
            throw std::runtime_error("solve_least_squares: rank deficient");
        }
    }

    // Back substitution with R: diagonal in diag[], strict upper triangle in a.
    std::vector<double> x(n, 0.0);
    for (std::size_t kk = n; kk-- > 0;) {
        double sum = rhs[kk];
        for (std::size_t j = kk + 1; j < n; ++j) {
            sum -= a(kk, j) * x[j];
        }
        x[kk] = sum / diag[kk];
    }
    return x;
}

std::vector<double> solve_square(Matrix a, std::span<const double> b) {
    const std::size_t n = a.rows();
    if (a.cols() != n || b.size() != n || n == 0) {
        throw std::invalid_argument("solve_square: shape mismatch");
    }
    std::vector<double> rhs(b.begin(), b.end());
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t pivot = k;
        for (std::size_t i = k + 1; i < n; ++i) {
            if (std::fabs(a(i, k)) > std::fabs(a(pivot, k))) {
                pivot = i;
            }
        }
        if (std::fabs(a(pivot, k)) < std::numeric_limits<double>::min()) {
            throw std::runtime_error("solve_square: singular matrix");
        }
        if (pivot != k) {
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(a(k, j), a(pivot, j));
            }
            std::swap(rhs[k], rhs[pivot]);
        }
        for (std::size_t i = k + 1; i < n; ++i) {
            const double f = a(i, k) / a(k, k);
            for (std::size_t j = k; j < n; ++j) {
                a(i, j) -= f * a(k, j);
            }
            rhs[i] -= f * rhs[k];
        }
    }
    std::vector<double> x(n, 0.0);
    for (std::size_t kk = n; kk-- > 0;) {
        double sum = rhs[kk];
        for (std::size_t j = kk + 1; j < n; ++j) {
            sum -= a(kk, j) * x[j];
        }
        x[kk] = sum / a(kk, kk);
    }
    return x;
}

}  // namespace pme::analysis
