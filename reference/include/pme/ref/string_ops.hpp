// SPDX-License-Identifier: MIT
// String stencils shared by the simulation and the geometric-nonlinearity solver (P0.5-R1, R6):
// the banded mass-weighted forms of the tension and bending energies and round-off-careful
// evaluation of K x. Internal to the reference solver.
#pragma once

#include <cstddef>
#include <vector>

#include "pme/ref/banded.hpp"

namespace pme::ref {

// Geometry and constants of one string polarisation on a grid of n + 1 intervals (nodes 0 .. N,
// N = n + 1). Interior nodes 1 .. N - 1 are the unknowns; node N is one more unknown when
// `coupled` (the moving bridge end), else it is fixed to zero.
struct StringStencil {
    std::size_t n = 0;  // interior unknowns N - 1
    double h = 0.0;
    double tension = 0.0;
    double ei = 0.0;
    bool coupled = false;
    bool left_clamped = false;
    bool right_clamped = false;  // only without a bridge

    [[nodiscard]] std::size_t size() const { return n + (coupled ? 1 : 0); }
};

// Unit-parameter forms (Bilbao 2009, sections 7.1-7.2): kt = (1/h) sum_cells g g^T with the first
// differences g (so that x^T kt x = integral of y_x^2), kb = (1/h^3) sum_rows w c c^T with the
// curvature rows c (a ghost-point row at a clamped end, half weight). K = T kt + EI kb.
void assemble_string(int n_int, double h, bool coupled, bool left_clamped, bool right_clamped,
                     BandMatrix& kt, BandMatrix& kb);

// out = K x with K = T kt + EI kb, evaluated through first differences and differences of
// differences (round-off eps |slope| instead of eps |x| on smooth data; see simulation.cpp notes).
void apply_string_stiffness(const StringStencil& st, const std::vector<double>& x,
                            std::vector<double>& out);

// out = kt x (the tension form of unit tension) and S = x^T kt x = integral of y_x^2 dx.
void apply_string_tension_form(const StringStencil& st, const std::vector<double>& x,
                               std::vector<double>& out);
double string_slope_integral(const StringStencil& st, const std::vector<double>& x);

}  // namespace pme::ref
