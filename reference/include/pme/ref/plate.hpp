// SPDX-License-Identifier: MIT
// Orthotropic Kirchhoff plate on a rectangle, simply supported, finite differences (P0.5-R4,
// SPEC-BODY "Plate model"). Physical-space operators for the time-domain solver plus the analytic
// and discrete modal frequencies used by the tests.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "pme/body.hpp"
#include "pme/ref/banded.hpp"

namespace pme::ref {

// Plate constants of SPEC-BODY with the grain along x (theta_g = 0):
//   D11 = E_L h^3 / (12 (1 - nu_LR nu_RL)),  D22 = E_R h^3 / (12 (1 - nu_LR nu_RL)),
//   D12 = nu_RL D11,  D66 = G_LR h^3 / 12.
struct PlateConstants {
    double lx = 0.0;
    double ly = 0.0;
    double h = 0.0;
    double rho = 0.0;
    double d11 = 0.0;
    double d22 = 0.0;
    double d12 = 0.0;
    double d66 = 0.0;
    [[nodiscard]] double rho_h() const { return rho * h; }
};

// Throws RefError naming the field for every feature the reference does not model: polygon outline,
// thickness grid, ribs, bridges as beams, rim stiffness, a grain angle other than zero. Moisture
// is applied linearly (value = reference + coefficient * moisture, moisture read as the change
// from the reference moisture).
PlateConstants derive_plate(const PlateParams& params);

// Analytic simply supported frequency (Hz) of mode (m, n), m, n >= 1 (SPEC-BODY / P0.5-T4):
//   omega^2 = [D11 (m pi/Lx)^4 + 2 (D12 + 2 D66)(m pi/Lx)^2 (n pi/Ly)^2 + D22 (n pi/Ly)^4] / (rho
//   h).
double plate_frequency_hz(const PlateConstants& c, int m, int n);

// Frequency (Hz) of the same mode under the semi-discrete (space only) operator on an nx x ny grid:
// the discrete sine modes are exact eigenvectors, with -d_xx replaced by (4/hx^2) sin^2(m pi/(2
// nx)).
double plate_discrete_frequency_hz(const PlateConstants& c, int nx, int ny, int m, int n);

// Frequency (Hz) of that mode under the fully discrete theta-scheme with time step k:
// sin^2(omega k / 2) = s / (1 + 4 theta s), s = (k^2 lambda) / 4 -> omega = (2/k) asin(sqrt(...)).
double plate_scheme_frequency_hz(const PlateConstants& c, int nx, int ny, int m, int n, double k,
                                 double theta);

struct PlateGrid {
    int nx = 0;  // intervals along x
    int ny = 0;
    double hx = 0.0;
    double hy = 0.0;
    int stride = 0;         // nodes along the fast axis
    bool fast_x = true;     // true: index = (i - 1) + (j - 1) * (nx - 1)
    std::size_t count = 0;  // interior nodes (nx - 1)(ny - 1)

    // Index of interior node (i, j), 1 <= i <= nx - 1, 1 <= j <= ny - 1.
    [[nodiscard]] std::size_t index(int i, int j) const {
        return fast_x ? static_cast<std::size_t>((i - 1) + (j - 1) * (nx - 1))
                      : static_cast<std::size_t>((j - 1) + (i - 1) * (ny - 1));
    }
};

// Nodal (mass, stiffness) form of the plate: M = rho h hx hy I, K = hx hy [D11 Dxx^2 + D22 Dyy^2 +
// 2 (D12 + 2 D66) Dxx Dyy] (Bilbao 2009, section 12.1; the mixed term in the commuting form so that
// the discrete sine modes diagonalise it).
struct PlateOperator {
    PlateGrid grid;
    double mass = 0.0;  // per node
    BandMatrix stiffness;
};
PlateOperator build_plate_operator(const PlateConstants& c, int nx, int ny);

// Bilinear weights of a point (x, y) on the plate's interior nodes: pairs of (node index, weight).
// Nodes on the simply supported edge carry no degree of freedom and are dropped. The same weights
// spread a point force and interpolate the displacement, so the coupling is energy neutral.
std::vector<std::pair<std::size_t, double>> bilinear_weights(const PlateGrid& grid, double x,
                                                             double y);

}  // namespace pme::ref
