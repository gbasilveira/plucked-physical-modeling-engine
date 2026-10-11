// SPDX-License-Identifier: MIT
#include "pme/ref/string_ops.hpp"

#include <array>

namespace pme::ref {

// Assembles the unit-parameter string forms (Bilbao 2009, sections 7.1-7.2). Node indexing: nodes
// 1 .. N - 1 are interior (index 0 .. N - 2); node N (index N - 1) is the moving bridge end when
// `coupled`. kt = (1/h) sum_cells g g^T (tension), kb = (1/h^3) sum_rows w c c^T (bending).
void assemble_string(int n_int, double h, bool coupled, bool left_clamped, bool right_clamped,
                     BandMatrix& kt, BandMatrix& kb) {
    const std::size_t nloc = static_cast<std::size_t>(n_int) + (coupled ? 1 : 0);
    kt = BandMatrix(nloc, 2);
    kb = BandMatrix(nloc, 2);
    const int nn = n_int + 1;  // N intervals; nodes 0 .. N
    auto node_index = [&](int node) -> long {
        // node 0 and (uncoupled) node N carry no degree of freedom
        if (node <= 0) {
            return -1;
        }
        if (node >= nn) {
            return coupled ? static_cast<long>(nn - 1) : -1;
        }
        return node - 1;
    };
    auto add_outer = [&](BandMatrix& m, const std::array<long, 3>& idx,
                         const std::array<double, 3>& coef, double weight) {
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) {
                if (idx[static_cast<std::size_t>(a)] < 0 || idx[static_cast<std::size_t>(b)] < 0) {
                    continue;
                }
                m.at(static_cast<std::size_t>(idx[static_cast<std::size_t>(a)]),
                     static_cast<std::size_t>(idx[static_cast<std::size_t>(b)])) +=
                    weight * coef[static_cast<std::size_t>(a)] * coef[static_cast<std::size_t>(b)];
            }
        }
    };
    // Tension form: first differences over the cells i = 0 .. N - 1.
    for (int i = 0; i < nn; ++i) {
        add_outer(kt, {node_index(i), node_index(i + 1), -1}, {-1.0, 1.0, 0.0}, 1.0 / h);
    }
    // Bending form: curvature at the interior nodes (the curvature at a moving bridge end is zero
    // by the choice of the ghost point, SPEC-STR "y = d_xx y = 0").
    for (int i = 1; i <= nn - 1; ++i) {
        add_outer(kb, {node_index(i - 1), node_index(i), node_index(i + 1)}, {1.0, -2.0, 1.0},
                  1.0 / (h * h * h));
    }
    if (left_clamped) {  // curvature at node 0 with the ghost point u_-1 = u_1, half weight
        add_outer(kb, {node_index(1), -1, -1}, {2.0, 0.0, 0.0}, 0.5 / (h * h * h));
    }
    if (right_clamped && !coupled) {  // node N fixed to zero, ghost u_N+1 = u_N-1
        add_outer(kb, {node_index(nn - 1), -1, -1}, {2.0, 0.0, 0.0}, 0.5 / (h * h * h));
    }
}

// out = K x for a string block, evaluated through first differences and differences of
// differences: x_{i+1} - x_i is exact for smooth data (Sterbenz) and the curvature is the
// difference of two such values, so the round-off is eps |slope| instead of eps |x|. This keeps the
// energy monitor at round-off level on fine grids. Mathematically identical to stiff * x.
void apply_string_stiffness(const StringStencil& b, const std::vector<double>& x,
                            std::vector<double>& out) {
    const std::size_t nn = b.n + 1;  // N intervals: nodes 0 .. N
    const double kt = b.tension / b.h;
    const double kb = b.ei / (b.h * b.h * b.h);
    auto node = [&](std::size_t i) -> double {  // x_0 = 0; x_N = bridge (coupled) or 0
        if (i == 0) {
            return 0.0;
        }
        if (i < nn) {
            return x[i - 1];
        }
        return b.coupled ? x[b.n] : 0.0;
    };
    std::vector<double> g(nn);  // first differences g_i = x_{i+1} - x_i, i = 0 .. N-1
    for (std::size_t i = 0; i < nn; ++i) {
        g[i] = node(i + 1) - node(i);
    }
    // Bending rows: curvature c_i = g_i - g_{i-1}, i = 1 .. N-1 (cw_0 = cw_N = 0).
    std::vector<double> cw(nn + 1, 0.0);
    for (std::size_t i = 1; i < nn; ++i) {
        cw[i] = g[i] - g[i - 1];
    }
    out.assign(b.size(), 0.0);
    for (std::size_t j = 1; j < nn + (b.coupled ? 1 : 0); ++j) {
        const double gl = g[j - 1];
        const double gr = j < nn ? g[j] : 0.0;
        double f = kt * (gl - gr);
        const double cl = cw[j - 1];
        const double cc = cw[j];
        const double cr = j + 1 <= nn ? cw[j + 1] : 0.0;
        f += kb * ((cr - cc) - (cc - cl));
        if (j == 1 && b.left_clamped) {
            f += kb * 2.0 * node(1);  // half-weight curvature row at node 0, ghost u_-1 = u_1
        }
        if (j == nn - 1 && b.right_clamped) {
            f += kb * 2.0 * node(nn - 1);  // same at node N (fixed to zero)
        }
        out[j - 1] = f;
    }
}

void apply_string_tension_form(const StringStencil& st, const std::vector<double>& x,
                               std::vector<double>& out) {
    const std::size_t nn = st.n + 1;
    const double kt = 1.0 / st.h;
    auto node = [&](std::size_t i) -> double {
        if (i == 0) {
            return 0.0;
        }
        if (i < nn) {
            return x[i - 1];
        }
        return st.coupled ? x[st.n] : 0.0;
    };
    out.assign(st.size(), 0.0);
    for (std::size_t j = 1; j < nn + (st.coupled ? 1 : 0); ++j) {
        const double gl = node(j) - node(j - 1);
        const double gr = j < nn ? node(j + 1) - node(j) : 0.0;
        out[j - 1] = kt * (gl - gr);
    }
}

double string_slope_integral(const StringStencil& st, const std::vector<double>& x) {
    const std::size_t nn = st.n + 1;
    auto node = [&](std::size_t i) -> double {
        if (i == 0) {
            return 0.0;
        }
        if (i < nn) {
            return x[i - 1];
        }
        return st.coupled ? x[st.n] : 0.0;
    };
    double s = 0.0;
    for (std::size_t i = 0; i < nn; ++i) {
        const double g = node(i + 1) - node(i);
        s += g * g;
    }
    return s / st.h;
}

}  // namespace pme::ref
