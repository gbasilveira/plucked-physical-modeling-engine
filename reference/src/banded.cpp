// SPDX-License-Identifier: MIT
#include "pme/ref/banded.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "pme/ref/common.hpp"

namespace pme::ref {

void BandMatrix::add_scaled(const BandMatrix& other, double scale) {
    if (other.n_ != n_) {
        throw RefError("BandMatrix::add_scaled: size mismatch");
    }
    for (std::size_t i = 0; i < n_; ++i) {
        const std::size_t lo = i > other.bw_ ? i - other.bw_ : 0;
        const std::size_t hi = std::min(n_ - 1, i + other.bw_);
        for (std::size_t j = lo; j <= hi; ++j) {
            at(i, j) += scale * other.get(i, j);
        }
    }
}

void BandMatrix::multiply(const std::vector<double>& x, std::vector<double>& y) const {
    y.assign(n_, 0.0);
    for (std::size_t i = 0; i < n_; ++i) {
        const std::size_t lo = i > bw_ ? i - bw_ : 0;
        const std::size_t hi = std::min(n_ - 1, i + bw_);
        double s = 0.0;
        for (std::size_t j = lo; j <= hi; ++j) {
            s += data_[i * (2 * bw_ + 1) + (j + bw_ - i)] * x[j];
        }
        y[i] = s;
    }
}

double BandMatrix::quadratic(const std::vector<double>& x, const std::vector<double>& y) const {
    double total = 0.0;
    for (std::size_t i = 0; i < n_; ++i) {
        const std::size_t lo = i > bw_ ? i - bw_ : 0;
        const std::size_t hi = std::min(n_ - 1, i + bw_);
        double s = 0.0;
        for (std::size_t j = lo; j <= hi; ++j) {
            s += data_[i * (2 * bw_ + 1) + (j + bw_ - i)] * y[j];
        }
        total += x[i] * s;
    }
    return total;
}

BandMatrix BandMatrix::leading(std::size_t m) const {
    BandMatrix out(m, bw_);
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t lo = i > bw_ ? i - bw_ : 0;
        const std::size_t hi = std::min(m - 1, i + bw_);
        for (std::size_t j = lo; j <= hi; ++j) {
            out.at(i, j) = get(i, j);
        }
    }
    return out;
}

BandCholesky::BandCholesky(const BandMatrix& a) : n_(a.size()), bw_(a.bandwidth()) {
    l_.assign(n_ * (bw_ + 1), 0.0);
    auto lref = [&](std::size_t i, std::size_t j) -> double& {
        return l_[i * (bw_ + 1) + (j + bw_ - i)];
    };
    for (std::size_t j = 0; j < n_; ++j) {
        const std::size_t klo = j > bw_ ? j - bw_ : 0;
        double d = a.get(j, j);
        for (std::size_t k = klo; k < j; ++k) {
            d -= lref(j, k) * lref(j, k);
        }
        if (!(d > 0.0)) {
            throw RefError("BandCholesky: matrix is not positive definite");
        }
        const double ljj = std::sqrt(d);
        lref(j, j) = ljj;
        const std::size_t ihi = std::min(n_ - 1, j + bw_);
        for (std::size_t i = j + 1; i <= ihi; ++i) {
            double s = a.get(i, j);
            const std::size_t k0 = i > bw_ ? i - bw_ : 0;
            for (std::size_t k = k0; k < j; ++k) {
                s -= lref(i, k) * lref(j, k);
            }
            lref(i, j) = s / ljj;
        }
    }
}

void BandCholesky::solve(std::vector<double>& b) const {
    // Forward substitution L y = b.
    for (std::size_t i = 0; i < n_; ++i) {
        const std::size_t lo = i > bw_ ? i - bw_ : 0;
        double s = b[i];
        for (std::size_t j = lo; j < i; ++j) {
            s -= l_[i * (bw_ + 1) + (j + bw_ - i)] * b[j];
        }
        b[i] = s / l_[i * (bw_ + 1) + bw_];
    }
    // Back substitution L^T x = y.
    for (std::size_t ii = n_; ii-- > 0;) {
        double s = b[ii];
        const std::size_t hi = std::min(n_ - 1, ii + bw_);
        for (std::size_t j = ii + 1; j <= hi; ++j) {
            s -= l_[j * (bw_ + 1) + (ii + bw_ - j)] * b[j];
        }
        b[ii] = s / l_[ii * (bw_ + 1) + bw_];
    }
}

void solve_dense(std::vector<double>& a, std::vector<double>& b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t piv = col;
        double best = std::fabs(a[col * n + col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::fabs(a[r * n + col]) > best) {
                best = std::fabs(a[r * n + col]);
                piv = r;
            }
        }
        if (!(best > 0.0)) {
            throw RefError("solve_dense: singular matrix");
        }
        if (piv != col) {
            for (std::size_t c = 0; c < n; ++c) {
                std::swap(a[piv * n + c], a[col * n + c]);
            }
            std::swap(b[piv], b[col]);
        }
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r * n + col] / a[col * n + col];
            if (f == 0.0) {
                continue;
            }
            for (std::size_t c = col; c < n; ++c) {
                a[r * n + c] -= f * a[col * n + c];
            }
            b[r] -= f * b[col];
        }
    }
    for (std::size_t ii = n; ii-- > 0;) {
        double s = b[ii];
        for (std::size_t c = ii + 1; c < n; ++c) {
            s -= a[ii * n + c] * b[c];
        }
        b[ii] = s / a[ii * n + ii];
    }
}

}  // namespace pme::ref
