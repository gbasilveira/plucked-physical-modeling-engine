// SPDX-License-Identifier: MIT
#include "pme/ref/plate.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "pme/ref/common.hpp"

namespace pme::ref {

PlateConstants derive_plate(const PlateParams& p) {
    if (!p.outline.empty()) {
        throw RefError("PlateParams.outline: polygon outlines are not modelled (rectangles only)");
    }
    if (!p.thicknessGrid.empty() || p.thicknessNx != 0 || p.thicknessNy != 0) {
        throw RefError("PlateParams.thicknessGrid: variable thickness is not modelled");
    }
    if (!p.ribs.empty()) {
        throw RefError("PlateParams.ribs: ribs are not modelled");
    }
    if (!p.bridges.empty()) {
        throw RefError("PlateParams.bridges: bridges as beams are not modelled");
    }
    if (p.rimStiffness != 0.0) {
        throw RefError("PlateParams.rimStiffness: elastic rim support is not modelled");
    }
    if (p.rimRotationalStiffness != 0.0) {
        throw RefError("PlateParams.rimRotationalStiffness: elastic rim support is not modelled");
    }
    if (p.grainAngle != 0.0) {
        throw RefError("PlateParams.grainAngle: only a grain along x (grainAngle = 0) is modelled");
    }
    const double e_l = p.youngsL + p.dEdMcL * p.moisture;
    const double e_r = p.youngsR + p.dEdMcR * p.moisture;
    const double g_lr = p.shearLR + p.dGdMc * p.moisture;
    const double rho = p.density + p.dRhodMc * p.moisture;
    if (!(p.lengthX > 0.0) || !(p.lengthY > 0.0) || !(p.thickness > 0.0) || !(e_l > 0.0) ||
        !(e_r > 0.0) || !(g_lr > 0.0) || !(rho > 0.0)) {
        throw RefError(
            "PlateParams: lengthX, lengthY, thickness, youngsL, youngsR, shearLR and density "
            "must be positive");
    }
    const double denom = 1.0 - p.poissonLR * p.poissonRL;
    if (!(denom > 0.0)) {
        throw RefError("PlateParams: 1 - poissonLR poissonRL must be positive");
    }
    PlateConstants c;
    c.lx = p.lengthX;
    c.ly = p.lengthY;
    c.h = p.thickness;
    c.rho = rho;
    const double h3 = p.thickness * p.thickness * p.thickness;
    c.d11 = e_l * h3 / (12.0 * denom);  // SPEC-BODY
    c.d22 = e_r * h3 / (12.0 * denom);
    c.d12 = p.poissonRL * c.d11;
    c.d66 = g_lr * h3 / 12.0;
    return c;
}

double plate_frequency_hz(const PlateConstants& c, int m, int n) {
    const double a = m * kPi / c.lx;
    const double b = n * kPi / c.ly;
    const double w2 = (c.d11 * a * a * a * a + 2.0 * (c.d12 + 2.0 * c.d66) * a * a * b * b +
                       c.d22 * b * b * b * b) /
                      c.rho_h();
    return std::sqrt(w2) / (2.0 * kPi);
}

namespace {
double discrete_lambda(const PlateConstants& c, int nx, int ny, int m, int n) {
    const double hx = c.lx / nx;
    const double hy = c.ly / ny;
    const double sx = std::sin(m * kPi / (2.0 * nx));
    const double sy = std::sin(n * kPi / (2.0 * ny));
    const double ax = 4.0 / (hx * hx) * sx * sx;
    const double ay = 4.0 / (hy * hy) * sy * sy;
    return (c.d11 * ax * ax + 2.0 * (c.d12 + 2.0 * c.d66) * ax * ay + c.d22 * ay * ay) / c.rho_h();
}
}  // namespace

double plate_discrete_frequency_hz(const PlateConstants& c, int nx, int ny, int m, int n) {
    return std::sqrt(discrete_lambda(c, nx, ny, m, n)) / (2.0 * kPi);
}

double plate_scheme_frequency_hz(const PlateConstants& c, int nx, int ny, int m, int n, double k,
                                 double theta) {
    const double lambda = discrete_lambda(c, nx, ny, m, n);
    const double s = k * k * lambda / 4.0;
    const double x = std::sqrt(s / (1.0 + 4.0 * theta * s));
    return (2.0 / k) * std::asin(x) / (2.0 * kPi);
}

namespace {
// 1D second-difference matrices of the Dirichlet problem on n interior nodes: tri[i] = {sub, diag,
// super}; sq[i][d + 2] = (D^2)(i, i + d).
struct Op1d {
    int n = 0;
    double h = 0.0;
    [[nodiscard]] double d1(int i, int j) const {  // (Dxx)(i, j), zero-based
        if (i == j) {
            return -2.0 / (h * h);
        }
        if (i - j == 1 || j - i == 1) {
            return 1.0 / (h * h);
        }
        return 0.0;
    }
    [[nodiscard]] double d2(int i, int j) const {  // (Dxx Dxx)(i, j)
        double s = 0.0;
        for (int k = std::max(0, std::max(i, j) - 1); k <= std::min(n - 1, std::min(i, j) + 1);
             ++k) {
            s += d1(i, k) * d1(k, j);
        }
        return s;
    }
};
}  // namespace

PlateOperator build_plate_operator(const PlateConstants& c, int nx, int ny) {
    if (nx < 2 || ny < 2) {
        throw RefError("plate grid: nx and ny must be at least 2");
    }
    PlateOperator op;
    PlateGrid& g = op.grid;
    g.nx = nx;
    g.ny = ny;
    g.hx = c.lx / nx;
    g.hy = c.ly / ny;
    g.fast_x = (nx - 1) <= (ny - 1);
    g.stride = g.fast_x ? nx - 1 : ny - 1;
    g.count = static_cast<std::size_t>((nx - 1)) * static_cast<std::size_t>((ny - 1));
    op.mass = c.rho_h() * g.hx * g.hy;
    const std::size_t bw = static_cast<std::size_t>(std::max({2 * g.stride, g.stride + 1, 2}));
    op.stiffness = BandMatrix(g.count, bw);
    const Op1d ox{nx - 1, g.hx};
    const Op1d oy{ny - 1, g.hy};
    const double cross = 2.0 * (c.d12 + 2.0 * c.d66);
    const double area = g.hx * g.hy;
    for (int j = 1; j <= ny - 1; ++j) {
        for (int i = 1; i <= nx - 1; ++i) {
            const std::size_t row = g.index(i, j);
            // D11 Dxx^2 (same j).
            for (int di = -2; di <= 2; ++di) {
                const int ii = i + di;
                if (ii < 1 || ii > nx - 1) {
                    continue;
                }
                op.stiffness.at(row, g.index(ii, j)) += area * c.d11 * ox.d2(i - 1, ii - 1);
            }
            // D22 Dyy^2 (same i).
            for (int dj = -2; dj <= 2; ++dj) {
                const int jj = j + dj;
                if (jj < 1 || jj > ny - 1) {
                    continue;
                }
                op.stiffness.at(row, g.index(i, jj)) += area * c.d22 * oy.d2(j - 1, jj - 1);
            }
            // 2 (D12 + 2 D66) Dxx Dyy.
            for (int di = -1; di <= 1; ++di) {
                for (int dj = -1; dj <= 1; ++dj) {
                    const int ii = i + di;
                    const int jj = j + dj;
                    if (ii < 1 || ii > nx - 1 || jj < 1 || jj > ny - 1) {
                        continue;
                    }
                    op.stiffness.at(row, g.index(ii, jj)) +=
                        area * cross * ox.d1(i - 1, ii - 1) * oy.d1(j - 1, jj - 1);
                }
            }
        }
    }
    return op;
}

std::vector<std::pair<std::size_t, double>> bilinear_weights(const PlateGrid& grid, double x,
                                                             double y) {
    const double fx = x / grid.hx;
    const double fy = y / grid.hy;
    if (!(fx >= 0.0) || !(fy >= 0.0) || fx > grid.nx || fy > grid.ny) {
        throw RefError("bridge point: position lies outside the plate rectangle");
    }
    int i0 = static_cast<int>(std::floor(fx));
    int j0 = static_cast<int>(std::floor(fy));
    i0 = std::min(i0, grid.nx - 1);
    j0 = std::min(j0, grid.ny - 1);
    const double a = fx - i0;
    const double b = fy - j0;
    std::vector<std::pair<std::size_t, double>> out;
    const std::array<std::array<double, 3>, 4> corners = {
        {{0, 0, (1 - a) * (1 - b)}, {1, 0, a * (1 - b)}, {0, 1, (1 - a) * b}, {1, 1, a * b}}};
    for (const auto& c : corners) {
        const int i = i0 + static_cast<int>(c[0]);
        const int j = j0 + static_cast<int>(c[1]);
        if (i < 1 || i > grid.nx - 1 || j < 1 || j > grid.ny - 1 || c[2] == 0.0) {
            continue;
        }
        out.emplace_back(grid.index(i, j), c[2]);
    }
    return out;
}

}  // namespace pme::ref
