// SPDX-License-Identifier: MIT
// Banded linear algebra for the reference solver (float64): a square banded matrix, its banded
// Cholesky factorisation and the solves. Every system the schemes solve is symmetric positive
// definite, so no pivoting is needed. Fixed operation order, no threads.
#pragma once

#include <cstddef>
#include <vector>

namespace pme::ref {

// Square matrix with half-bandwidth `bw` (entries (i, j) with |i - j| <= bw), stored by rows.
class BandMatrix {
public:
    BandMatrix() = default;
    BandMatrix(std::size_t n, std::size_t bw) : n_(n), bw_(bw), data_(n * (2 * bw + 1), 0.0) {}

    [[nodiscard]] std::size_t size() const { return n_; }
    [[nodiscard]] std::size_t bandwidth() const { return bw_; }

    // Entry (i, j); |i - j| must not exceed the bandwidth.
    double& at(std::size_t i, std::size_t j) { return data_[i * (2 * bw_ + 1) + (j + bw_ - i)]; }
    [[nodiscard]] double get(std::size_t i, std::size_t j) const {
        if ((i > j ? i - j : j - i) > bw_) {
            return 0.0;
        }
        return data_[i * (2 * bw_ + 1) + (j + bw_ - i)];
    }

    // this += scale * other (same size; other's bandwidth must not exceed this one's).
    void add_scaled(const BandMatrix& other, double scale);
    // y = this * x.
    void multiply(const std::vector<double>& x, std::vector<double>& y) const;
    // x^T * this * y.
    [[nodiscard]] double quadratic(const std::vector<double>& x,
                                   const std::vector<double>& y) const;
    // Leading principal sub-matrix of order m.
    [[nodiscard]] BandMatrix leading(std::size_t m) const;

private:
    std::size_t n_ = 0;
    std::size_t bw_ = 0;
    std::vector<double> data_;
};

// A = L L^T for a symmetric positive definite BandMatrix. Throws RefError if A is not positive
// definite.
class BandCholesky {
public:
    BandCholesky() = default;
    explicit BandCholesky(const BandMatrix& a);

    [[nodiscard]] std::size_t size() const { return n_; }
    // Solves A x = b in place.
    void solve(std::vector<double>& b) const;

private:
    std::size_t n_ = 0;
    std::size_t bw_ = 0;
    std::vector<double> l_;  // row i holds L(i, i - bw .. i), stored at i * (bw + 1) + (j + bw - i)
};

// Dense LU with partial pivoting for the small Newton systems. a is n x n row-major; b is
// overwritten with the solution. Throws RefError for a singular matrix.
void solve_dense(std::vector<double>& a, std::vector<double>& b, std::size_t n);

}  // namespace pme::ref
