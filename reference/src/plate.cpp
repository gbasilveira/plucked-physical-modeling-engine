// SPDX-License-Identifier: MIT
#include "pme/ref/plate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "pme/ref/common.hpp"

namespace pme::ref {

PlateConstants derive_plate(const PlateParams& p) {
    if (!p.outline.empty()) {
        throw RefError("PlateParams.outline: polygon outlines are not modelled (rectangles only)");
    }
    if (!p.thicknessGrid.empty() || p.thicknessNx != 0 || p.thicknessNy != 0) {
        throw RefError("PlateParams.thicknessGrid: variable thickness is not modelled");
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
    validate_ribs(c, p.ribs);
    return c;
}

namespace {
constexpr double kRibAxisTol = 1e-9;  // m: how far a rib may stray from its axis
constexpr double kRibGridTol = 1e-9;  // in grid spacings

std::string rib_name(std::size_t i) { return "ribs[" + std::to_string(i) + "]"; }
}  // namespace

// SPEC-BODY "Geometry inputs": ribs are straight beams of rectangular section.
void validate_ribs(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs) {
    for (std::size_t i = 0; i < ribs.size(); ++i) {
        const pme::RibDesc& r = ribs[i];
        const std::string n = "PlateParams." + rib_name(i);
        const double vals[] = {r.start.x, r.start.y, r.start.z, r.end.x, r.end.y, r.end.z,
                               r.width,   r.height,  r.youngs,  r.density};
        for (const double v : vals) {
            if (!std::isfinite(v)) {
                throw RefError(n + ": all values must be finite");
            }
        }
        if (!(r.width > 0.0)) {
            throw RefError(n + ".width: must be positive");
        }
        if (!(r.height > 0.0)) {
            throw RefError(n + ".height: must be positive");
        }
        if (!(r.youngs > 0.0)) {
            throw RefError(n + ".youngs: must be positive");
        }
        if (!(r.density > 0.0)) {
            throw RefError(n + ".density: must be positive");
        }
        if (r.start.z != 0.0 || r.end.z != 0.0) {
            throw RefError(n + ".start.z/end.z: ribs lie on the mid-surface (z = 0)");
        }
        const double dx = std::fabs(r.end.x - r.start.x);
        const double dy = std::fabs(r.end.y - r.start.y);
        if (dx > kRibAxisTol && dy > kRibAxisTol) {
            throw RefError(rib_name(i) + ": only axis-parallel ribs are modelled");
        }
        if (dx <= kRibAxisTol && dy <= kRibAxisTol) {
            throw RefError(rib_name(i) + ": start and end coincide (zero length)");
        }
        const double xs[] = {r.start.x, r.end.x};
        const double ys[] = {r.start.y, r.end.y};
        for (int e = 0; e < 2; ++e) {
            if (xs[e] < -kRibAxisTol || xs[e] > c.lx + kRibAxisTol || ys[e] < -kRibAxisTol ||
                ys[e] > c.ly + kRibAxisTol) {
                throw RefError(rib_name(i) + ": end point lies outside the plate rectangle");
            }
        }
    }
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
    return build_plate_operator(c, {}, nx, ny);
}

namespace {
// A rib resolved on the grid: its line is index `fixed` of the axis normal to it, and it spans
// nodes s0..s1 (s0 < s1) of the axis along it.
struct RibLine {
    bool along_y = false;
    int fixed = 0;
    int s0 = 0;
    int s1 = 0;
};

int node_on_grid(double coordinate, double spacing, int count, bool& ok) {
    const double f = coordinate / spacing;
    const double r = std::round(f);
    if (std::fabs(coordinate - r * spacing) > kRibGridTol * spacing || r < 0.0 || r > count) {
        ok = false;
    }
    return static_cast<int>(r);
}

RibLine resolve_rib(const pme::RibDesc& r, std::size_t index, const PlateGrid& g) {
    bool ok = true;
    RibLine line;
    line.along_y = std::fabs(r.end.x - r.start.x) <= kRibAxisTol;
    const int xa = node_on_grid(r.start.x, g.hx, g.nx, ok);
    const int xb = node_on_grid(r.end.x, g.hx, g.nx, ok);
    const int ya = node_on_grid(r.start.y, g.hy, g.ny, ok);
    const int yb = node_on_grid(r.end.y, g.hy, g.ny, ok);
    if (!ok) {
        throw RefError(rib_name(index) + ": line and end points must lie on grid nodes (nx = " +
                       std::to_string(g.nx) + ", ny = " + std::to_string(g.ny) + ")");
    }
    if (line.along_y) {
        line.fixed = xa;
        line.s0 = std::min(ya, yb);
        line.s1 = std::max(ya, yb);
    } else {
        line.fixed = ya;
        line.s0 = std::min(xa, xb);
        line.s1 = std::max(xa, xb);
    }
    return line;
}

// SPEC-BODY "Geometry inputs" (ribs as straight beams), Euler-Bernoulli: the rib adds
//   1/2 E I (w_ss)^2 + 1/2 rhoA (w_t)^2 per unit length along its axis s,
// discretised by the 3-point second difference along the grid line (stiffness, interior line
// nodes only: a rib end is a free end) and the trapezoid rule (lumped mass).
void add_ribs(const std::vector<pme::RibDesc>& ribs, PlateOperator& op) {
    const PlateGrid& g = op.grid;
    const std::size_t bw = op.stiffness.bandwidth();
    for (std::size_t ri = 0; ri < ribs.size(); ++ri) {
        const pme::RibDesc& r = ribs[ri];
        const RibLine line = resolve_rib(r, ri, g);
        const double ei = r.youngs * r.width * r.height * r.height * r.height / 12.0;
        const double rho_a = r.density * r.width * r.height;
        const double hs = line.along_y ? g.hy : g.hx;
        const int n_along = line.along_y ? g.ny : g.nx;
        const int n_fixed = line.along_y ? g.nx : g.ny;
        // Degree of freedom of line node p, or -1 on the simply supported edge (w = 0).
        auto dof = [&](int p) -> long {
            if (p < 1 || p > n_along - 1 || line.fixed < 1 || line.fixed > n_fixed - 1) {
                return -1;
            }
            return static_cast<long>(line.along_y ? g.index(line.fixed, p) : g.index(p, line.fixed));
        };
        for (int p = line.s0; p <= line.s1; ++p) {
            const long d = dof(p);
            if (d >= 0) {
                const double w = (p == line.s0 || p == line.s1) ? 0.5 : 1.0;
                op.node_mass[static_cast<std::size_t>(d)] += rho_a * hs * w;
            }
        }
        for (int p = line.s0 + 1; p <= line.s1 - 1; ++p) {
            const long ids[3] = {dof(p - 1), dof(p), dof(p + 1)};
            const double coef[3] = {1.0 / (hs * hs), -2.0 / (hs * hs), 1.0 / (hs * hs)};
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    if (ids[a] < 0 || ids[b] < 0) {
                        continue;
                    }
                    const auto ia = static_cast<std::size_t>(ids[a]);
                    const auto ib = static_cast<std::size_t>(ids[b]);
                    if ((ia > ib ? ia - ib : ib - ia) > bw) {
                        throw RefError(rib_name(ri) + ": rib stencil exceeds the matrix bandwidth");
                    }
                    op.stiffness.at(ia, ib) += ei * hs * coef[a] * coef[b];
                }
            }
        }
    }
}
}  // namespace

namespace {
// Second difference as a difference of first differences: for smooth data both first differences
// are exact (Sterbenz), so the result carries a relative error of eps instead of eps |u| / result.
double d2(double um, double u0, double up) { return (up - u0) - (u0 - um); }
}  // namespace

// SPEC-BODY K = hx hy [D11 Dxx^2 + D22 Dyy^2 + 2 (D12 + 2 D66) Dxx Dyy] + sum of rib terms, as
// products of the interior second-difference operators (zero beyond the simply supported edges).
void plate_stiffness_apply(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs,
                           const PlateGrid& g, const std::vector<double>& w,
                           std::vector<double>& out) {
    const int nx = g.nx;
    const int ny = g.ny;
    const auto stride = static_cast<std::size_t>(ny + 1);
    auto at = [stride](int i, int j) { return static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(j); };
    const std::size_t total = static_cast<std::size_t>(nx + 1) * stride;
    std::vector<double> wp(total, 0.0);
    for (int j = 1; j <= ny - 1; ++j) {
        for (int i = 1; i <= nx - 1; ++i) {
            wp[at(i, j)] = w[g.index(i, j)];
        }
    }
    const double ihx2 = 1.0 / (g.hx * g.hx);
    const double ihy2 = 1.0 / (g.hy * g.hy);
    std::vector<double> a(total, 0.0);  // Dxx w
    std::vector<double> b(total, 0.0);  // Dyy w
    for (int j = 1; j <= ny - 1; ++j) {
        for (int i = 1; i <= nx - 1; ++i) {
            a[at(i, j)] = d2(wp[at(i - 1, j)], wp[at(i, j)], wp[at(i + 1, j)]) * ihx2;
            b[at(i, j)] = d2(wp[at(i, j - 1)], wp[at(i, j)], wp[at(i, j + 1)]) * ihy2;
        }
    }
    const double area = g.hx * g.hy;
    const double cross = 2.0 * (c.d12 + 2.0 * c.d66);
    std::vector<double> kp(total, 0.0);
    for (int j = 1; j <= ny - 1; ++j) {
        for (int i = 1; i <= nx - 1; ++i) {
            const double axx = d2(a[at(i - 1, j)], a[at(i, j)], a[at(i + 1, j)]) * ihx2;
            const double byy = d2(b[at(i, j - 1)], b[at(i, j)], b[at(i, j + 1)]) * ihy2;
            const double bxx = d2(b[at(i - 1, j)], b[at(i, j)], b[at(i + 1, j)]) * ihx2;
            kp[at(i, j)] = area * (c.d11 * axx + c.d22 * byy + cross * bxx);
        }
    }
    for (std::size_t ri = 0; ri < ribs.size(); ++ri) {
        const pme::RibDesc& r = ribs[ri];
        const RibLine line = resolve_rib(r, ri, g);
        const double ei = r.youngs * r.width * r.height * r.height * r.height / 12.0;
        const double hs = line.along_y ? g.hy : g.hx;
        const double ihs2 = 1.0 / (hs * hs);
        for (int p = line.s0 + 1; p <= line.s1 - 1; ++p) {
            auto node = [&](int s) { return line.along_y ? at(line.fixed, s) : at(s, line.fixed); };
            const double q = d2(wp[node(p - 1)], wp[node(p)], wp[node(p + 1)]) * ihs2;
            const double f = ei * hs * q * ihs2;
            kp[node(p - 1)] += f;
            kp[node(p)] += -2.0 * f;
            kp[node(p + 1)] += f;
        }
    }
    out.assign(g.count, 0.0);
    for (int j = 1; j <= ny - 1; ++j) {
        for (int i = 1; i <= nx - 1; ++i) {
            out[g.index(i, j)] = kp[at(i, j)];
        }
    }
}

PlateOperator build_plate_operator(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs,
                                   int nx, int ny) {
    if (nx < 2 || ny < 2) {
        throw RefError("plate grid: nx and ny must be at least 2");
    }
    validate_ribs(c, ribs);
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
    op.node_mass.assign(g.count, op.mass);
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
    add_ribs(ribs, op);
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
