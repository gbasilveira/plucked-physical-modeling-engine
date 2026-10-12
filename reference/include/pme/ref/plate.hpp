// SPDX-License-Identifier: MIT
// Orthotropic Kirchhoff plate on a rectangle, simply supported, finite differences (P0.5-R4,
// SPEC-BODY "Plate model"). Physical-space operators for the time-domain solver plus the analytic
// and discrete modal frequencies used by the tests.
#pragma once

#include <cstddef>
#include <string>
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
// thickness grid, bridges as beams, rim stiffness, a grain angle other than zero, and ribs that are
// not axis-parallel Euler-Bernoulli beams inside the rectangle (validate_ribs). Moisture
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

// Checks the ribs of a plate (P0.5-R4 follow-up P0.5-ribs): each rib is an Euler-Bernoulli beam
// parallel to x or to y (|dy| or |dx| at most 1e-9 m) with z = 0 at both ends, positive width,
// height, youngs and density, lying inside the rectangle. Throws RefError naming `ribs[i]` and the
// field; diagonal ribs give "ribs[i]: only axis-parallel ribs are modelled".
void validate_ribs(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs);

// Nodal (mass, stiffness) form of the plate: M = rho h hx hy I, K = hx hy [D11 Dxx^2 + D22 Dyy^2 +
// 2 (D12 + 2 D66) Dxx Dyy] (Bilbao 2009, section 12.1; the mixed term in the commuting form so that
// the discrete sine modes diagonalise it). Ribs add to both: E I hs (delta_ss w)^2 per interior
// line node to K, and the trapezoid-lumped rho A hs per line node to M
// (docs/dev/reference-solver.md, "Ribs").
struct PlateOperator {
    PlateGrid grid;
    double mass = 0.0;              // the plate's own mass per node (rho h hx hy)
    std::vector<double> node_mass;  // mass per interior node, ribs included (= mass without ribs)
    BandMatrix stiffness;           // ribs included
};
PlateOperator build_plate_operator(const PlateConstants& c, int nx, int ny);
// With ribs: every rib's line and end points must fall on grid nodes within 1e-9 h, else RefError
// naming `ribs[i]` and the grid.
PlateOperator build_plate_operator(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs,
                                   int nx, int ny);

// out = K w for a vector w over the interior nodes, evaluated in difference form (first the second
// differences Dxx w, Dyy w, then their differences) so that the round-off for smooth w is
// eps/(k h)^2 instead of the eps/(k h)^4 of the expanded banded product. Equals
// build_plate_operator(c, ribs, nx, ny).stiffness.multiply(w, out) up to round-off. Used for the
// residuals and Rayleigh quotients of plate_modes().
void plate_stiffness_apply(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs,
                           const PlateGrid& grid, const std::vector<double>& w,
                           std::vector<double>& out);

// The `count` lowest eigenpairs of K phi = omega^2 M phi of the discrete (ribbed) plate operator.
struct PlateModeOptions {
    int max_iterations = 500;  // RefError when the subspace iteration needs more
};
struct PlateModes {
    std::vector<double> frequencies_hz;       // ascending
    std::vector<std::vector<double>> values;  // values[mode][point]: M-normalised, bilinear
    int iterations = 0;                       // subspace iterations used
    double max_residual = 0.0;                // max ||K phi - w^2 M phi|| / (w^2 ||M phi||)
};
// Block inverse (subspace) iteration with Rayleigh-Ritz on A = M^(-1/2) K M^(-1/2), factored once
// with BandCholesky; cyclic Jacobi for the projected problem. Mode shapes are evaluated at
// `points` (x, y) by bilinear_weights and normalised so that phi^T M phi = 1 (the sign makes the
// largest nodal value positive). Throws RefError when the iteration does not converge or a
// residual exceeds 1e-9.
PlateModes plate_modes(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs, int nx,
                       int ny, int count, const std::vector<std::pair<double, double>>& points = {},
                       const PlateModeOptions& options = {});
// JSON text of a PlateModes result: count, nx, ny, frequencies_hz, iterations, max_residual and,
// when points were given, points and values.
std::string plate_modes_to_json(const PlateModes& modes, int nx, int ny,
                                const std::vector<std::pair<double, double>>& points);

// Bilinear weights of a point (x, y) on the plate's interior nodes: pairs of (node index, weight).
// Nodes on the simply supported edge carry no degree of freedom and are dropped. The same weights
// spread a point force and interpolate the displacement, so the coupling is energy neutral.
std::vector<std::pair<std::size_t, double>> bilinear_weights(const PlateGrid& grid, double x,
                                                             double y);

}  // namespace pme::ref
