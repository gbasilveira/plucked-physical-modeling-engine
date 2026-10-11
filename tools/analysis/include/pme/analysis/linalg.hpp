// SPDX-License-Identifier: MIT
// Small dense linear algebra for the analysis library (P0.3-R3): Householder QR least squares and
// Gaussian elimination with partial pivoting. float64, row-major, not for the audio thread.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace pme::analysis {

// Dense row-major matrix.
class Matrix {
public:
    Matrix() = default;
    Matrix(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

    [[nodiscard]] std::size_t rows() const { return rows_; }
    [[nodiscard]] std::size_t cols() const { return cols_; }
    double& operator()(std::size_t r, std::size_t c) { return data_[r * cols_ + c]; }
    double operator()(std::size_t r, std::size_t c) const { return data_[r * cols_ + c]; }

private:
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    std::vector<double> data_;
};

// Minimises ||A x - b||_2 for a full-column-rank A with rows >= cols, by Householder QR (A is
// taken by value and overwritten). Throws std::invalid_argument on a shape mismatch and
// std::runtime_error when A is numerically rank deficient.
std::vector<double> solve_least_squares(Matrix a, std::span<const double> b);

// Solves the square system A x = b by Gaussian elimination with partial pivoting. Throws
// std::runtime_error when A is singular.
std::vector<double> solve_square(Matrix a, std::span<const double> b);

}  // namespace pme::analysis
