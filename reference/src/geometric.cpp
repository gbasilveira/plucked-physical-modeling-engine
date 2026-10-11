// SPDX-License-Identifier: MIT
// Geometric nonlinearity of a single fixed-end string (SPEC-GEO).
//
// Tension modulation. Kirchhoff-Carrier potential V = (EA - T) S^2 / (8 L), S = integral of
// y_x^2 + z_x^2 over both polarisations (SPEC-GEO "Tension modulation"). The discrete gradient
// (V(S+) - V(S-)) / (S+ - S-) times the discrete gradient of S makes the force
//     a * Kt (u+ + u-) / 2,   a = (EA - T) (S+ + S-) / (4 L),
// with Kt the tension form (x^T Kt x = S for one polarisation). The scheme is the theta-scheme
// plus this term; the scalar `a` is found by Newton iteration (Bilbao 2009, section 8.1), each
// evaluation being a banded solve. The discrete energy includes (V(S+) + V(S)) / 2.
//
// Longitudinal waves. rhoA xi_tt = EA xi_xx + (EA - T)/2 d_x(y_x^2 + z_x^2), fixed ends (SPEC-GEO
// "Longitudinal modes"), the same theta-scheme with the forcing taken explicitly at step n, as in
// SPEC-GEO ("modal oscillator with this explicit force at step n"). One-way coupling: there is no
// back-coupling from xi to y in SPEC-GEO's default tier, so the energy trace covers the
// transverse subsystem only.
#include "pme/ref/geometric.hpp"

#include <algorithm>
#include <cmath>

#include "pme/ref/banded.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/string_ops.hpp"

namespace pme::ref {

namespace {

struct Pol {
    std::vector<double> cur, prev, next;
};

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        s += a[i] * b[i];
    }
    return s;
}

double interp_node(const std::vector<double>& u, double h, double x) {
    const int nint = static_cast<int>(u.size());
    const double fx = x / h;
    int i0 = static_cast<int>(std::floor(fx));
    i0 = std::max(0, std::min(i0, nint));
    const double a = fx - i0;
    auto node = [&](int i) {
        return (i >= 1 && i <= nint) ? u[static_cast<std::size_t>(i - 1)] : 0.0;
    };
    return (1.0 - a) * node(i0) + a * node(i0 + 1);
}

}  // namespace

Results simulate_geometric(const Scenario& sc, const SimulationOptions& options) {
    const StringSpec& spec = sc.strings[0];
    const StringPhysics ph = derive_string(spec.params);
    const bool lon = spec.geometric.longitudinal;
    const double gamma = ph.ea - ph.tension;  // EA - T
    if (!(gamma > 0.0)) {
        throw RefError(
            "geometric: EA - T must be positive (the axial stiffness exceeds the tension)");
    }
    const int nn = spec.intervals;
    const std::size_t n = static_cast<std::size_t>(nn - 1);
    const double len = ph.length;
    const double h = len / nn;
    const double k = 1.0 / (sc.fs * sc.oversampling);
    const double theta = sc.theta;
    const std::size_t steps = static_cast<std::size_t>(std::llround(sc.duration / k));
    const bool clamped = spec.params.boundary == Boundary::Clamped;
    const StringStencil st{n, h, ph.tension, ph.ei, false, clamped, clamped};

    BandMatrix kt;
    BandMatrix kb;
    assemble_string(nn - 1, h, false, clamped, clamped, kt, kb);
    BandMatrix mass(n, 2);
    for (std::size_t i = 0; i < n; ++i) {
        mass.at(i, i) = ph.rho_a * h;
    }
    BandMatrix kfull(n, 2);
    kfull.add_scaled(kt, ph.tension);
    kfull.add_scaled(kb, ph.ei);
    BandMatrix damp(n, 2);
    damp.add_scaled(mass, 2.0 * ph.b1);
    damp.add_scaled(kfull, 2.0 * ph.b3);
    BandMatrix pm = mass;
    pm.add_scaled(kfull, theta * k * k);
    BandMatrix a0 = pm;
    a0.add_scaled(damp, 0.5 * k);

    const bool tm = spec.geometric.tension_modulation;
    const double cks = gamma / (4.0 * len);  // a = cks (S+ + S-)

    // Longitudinal subsystem: M_L = rhoA h, K_L = EA kt, C_L = 2 b1 M_L + 2 b3 K_L.
    BandMatrix kl(n, 2);
    BandMatrix ml(n, 2);
    BandMatrix pml;
    BandMatrix cl(n, 2);
    BandCholesky chol_l;
    if (lon) {
        kl.add_scaled(kt, ph.ea);
        ml = mass;
        cl.add_scaled(ml, 2.0 * spec.geometric.longitudinal_b1);
        cl.add_scaled(kl, 2.0 * spec.geometric.longitudinal_b3);
        pml = ml;
        pml.add_scaled(kl, theta * k * k);
        BandMatrix al = pml;
        al.add_scaled(cl, 0.5 * k);
        chol_l = BandCholesky(al);
    }

    Results out;
    out.name = sc.name;
    out.fs = sc.fs;
    out.oversampling = sc.oversampling;
    out.sim_rate = sc.fs * sc.oversampling;
    out.dt = k;
    out.steps = steps;

    // Initial state: triangle released at rest, polarisation split by the angle.
    Pol pol[2];
    const double amp[2] = {spec.initial.amplitude * std::cos(spec.initial.angle),
                           spec.initial.amplitude * std::sin(spec.initial.angle)};
    const double xp = spec.initial.position;
    if (!(xp > 0.0) || !(xp < len)) {
        throw RefError("strings[].initial.position must lie inside the string");
    }
    for (int p = 0; p < 2; ++p) {
        pol[p].cur.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(i + 1) * h;
            pol[p].cur[i] = x <= xp ? amp[p] * x / xp : amp[p] * (len - x) / (len - xp);
        }
        pol[p].next.assign(n, 0.0);
    }
    {
        // Time-symmetric start at rest (as for the linear strings), using the linear operator.
        BandCholesky c0(a0);
        for (int p = 0; p < 2; ++p) {
            std::vector<double> kx;
            apply_string_stiffness(st, pol[p].cur, kx);
            c0.solve(kx);
            pol[p].prev = pol[p].cur;
            for (std::size_t i = 0; i < n; ++i) {
                pol[p].prev[i] -= 0.5 * k * k * kx[i];
            }
        }
    }
    std::vector<double> xi_cur(n, 0.0);
    std::vector<double> xi_prev(n, 0.0);
    std::vector<double> xi_next(n, 0.0);
    if (lon && spec.geometric.has_longitudinal_initial) {
        const double xl = spec.geometric.longitudinal_initial_position;
        if (!(xl > 0.0) || !(xl < len)) {
            throw RefError("geometric.longitudinal_initial.position must lie inside the string");
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(i + 1) * h;
            const double a = spec.geometric.longitudinal_initial_amplitude;
            xi_cur[i] = x <= xl ? a * x / xl : a * (len - x) / (len - xl);
        }
        BandMatrix al = pml;
        al.add_scaled(cl, 0.5 * k);
        const BandCholesky cs(al);
        std::vector<double> kx;
        kl.multiply(xi_cur, kx);
        cs.solve(kx);
        xi_prev = xi_cur;
        for (std::size_t i = 0; i < n; ++i) {
            xi_prev[i] -= 0.5 * k * k * kx[i];
        }
    }

    out.signals.resize(sc.observations.size());
    for (std::size_t i = 0; i < sc.observations.size(); ++i) {
        out.signals[i].name = sc.observations[i].name;
        out.signals[i].data.assign(steps, 0.0);
    }
    EnergyTrace& et = out.energy;
    if (options.record_energy) {
        for (auto* v : {&et.total, &et.string, &et.bridge, &et.plate, &et.hammer, &et.contact}) {
            v->assign(steps + 1, 0.0);
        }
        et.loss.assign(steps, 0.0);
        et.balance.assign(steps, 0.0);
    }

    // Energy of the pair (newer, older): H = sum_p [1/2 |v|_M^2 + 1/2 new.K old + theta k^2/2
    // |v|_K^2]
    // + (V(S_new) + V(S_old)) / 2 (Bilbao 2009, sections 7.2.2 and 8.1).
    auto store_energy = [&](const std::vector<double>* newer[2],
                            const std::vector<double>* older[2], std::size_t idx) {
        double lin = 0.0;
        double s_new = 0.0;
        double s_old = 0.0;
        for (int p = 0; p < 2; ++p) {
            const std::vector<double>& xn = *newer[p];
            const std::vector<double>& xo = *older[p];
            std::vector<double> kxn;
            std::vector<double> kxo;
            apply_string_stiffness(st, xn, kxn);
            apply_string_stiffness(st, xo, kxo);
            for (std::size_t i = 0; i < n; ++i) {
                const double v = (xn[i] - xo[i]) / k;
                lin += 0.5 * mass.get(i, i) * v * v + 0.5 * xn[i] * kxo[i] +
                       0.5 * theta * k * v * (kxn[i] - kxo[i]);
            }
            s_new += string_slope_integral(st, xn);
            s_old += string_slope_integral(st, xo);
        }
        const double v_new = tm ? gamma * s_new * s_new / (8.0 * len) : 0.0;
        const double v_old = tm ? gamma * s_old * s_old / (8.0 * len) : 0.0;
        et.string[idx] = lin;
        et.contact[idx] = 0.5 * (v_new + v_old);  // tension-modulation potential
        et.total[idx] = lin + et.contact[idx];
    };
    if (options.record_energy) {
        const std::vector<double>* nw[2] = {&pol[0].cur, &pol[1].cur};
        const std::vector<double>* ol[2] = {&pol[0].prev, &pol[1].prev};
        store_energy(nw, ol, 0);
    }

    std::vector<double> kx_cur[2];
    std::vector<double> kt_prev[2];
    std::vector<double> tmp(n);
    std::vector<double> r1;
    std::vector<double> r2;
    double a_scalar = 0.0;
    BandCholesky chol0(a0);
    std::vector<double> force_l(n, 0.0);

    for (std::size_t step = 0; step < steps; ++step) {
        for (int p = 0; p < 2; ++p) {
            for (std::size_t i = 0; i < n; ++i) {
                tmp[i] = 2.0 * pol[p].cur[i] - pol[p].prev[i];
            }
            pm.multiply(tmp, r1);
            damp.multiply(pol[p].prev, r2);
            apply_string_stiffness(st, pol[p].cur, kx_cur[p]);
            for (std::size_t i = 0; i < n; ++i) {
                r1[i] += 0.5 * k * r2[i] - k * k * kx_cur[p][i];
            }
            // r1 holds the base right-hand side; keep it in next as scratch.
            pol[p].next = r1;
            apply_string_tension_form(st, pol[p].prev, kt_prev[p]);
        }
        if (!tm) {
            for (int p = 0; p < 2; ++p) {
                chol0.solve(pol[p].next);
            }
        } else {
            const double s_prev =
                string_slope_integral(st, pol[0].prev) + string_slope_integral(st, pol[1].prev);
            const double s_cur =
                string_slope_integral(st, pol[0].cur) + string_slope_integral(st, pol[1].cur);
            (void)s_cur;
            if (step == 0) {
                a_scalar = 2.0 * cks * s_prev;
            }
            std::vector<double> base[2] = {pol[0].next, pol[1].next};
            double residual = 1.0;
            int it = 0;
            std::vector<double> d[2];
            for (; it < 50; ++it) {
                BandMatrix am = a0;
                am.add_scaled(kt, 0.5 * k * k * a_scalar);
                const BandCholesky cm(am);
                double s_plus = 0.0;
                double ds_plus = 0.0;
                for (int p = 0; p < 2; ++p) {
                    d[p] = base[p];
                    for (std::size_t i = 0; i < n; ++i) {
                        d[p][i] -= 0.5 * k * k * a_scalar * kt_prev[p][i];
                    }
                    cm.solve(d[p]);
                    s_plus += string_slope_integral(st, d[p]);
                    // dd/da = -A^-1 (k^2/2) Kt (d + u-)
                    std::vector<double> rhs(n);
                    std::vector<double> ktd;
                    apply_string_tension_form(st, d[p], ktd);
                    for (std::size_t i = 0; i < n; ++i) {
                        rhs[i] = -0.5 * k * k * (ktd[i] + kt_prev[p][i]);
                    }
                    cm.solve(rhs);
                    ds_plus += 2.0 * dot(ktd, rhs);
                }
                const double f = a_scalar - cks * (s_plus + s_prev);
                residual = std::fabs(f);
                if (residual <= 1e-15 * std::fabs(a_scalar) || residual <= 1e-300) {
                    break;
                }
                const double df = 1.0 - cks * ds_plus;
                a_scalar -= f / df;
            }
            if (it >= 50) {
                throw RefError("tension-modulation Newton iteration did not converge at step " +
                               std::to_string(step));
            }
            out.newton.max_iterations = std::max(out.newton.max_iterations, it + 1);
            ++out.newton.contact_steps;
            for (int p = 0; p < 2; ++p) {
                pol[p].next = d[p];
            }
        }
        if (lon) {
            // Forcing F_i = (EA - T)/2 (c_i - c_{i-1}) with the cell values c_i = sum_p (du/h)^2.
            std::vector<double> cell(static_cast<std::size_t>(nn), 0.0);
            for (int p = 0; p < 2; ++p) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(nn); ++i) {
                    const double ui = i == 0 ? 0.0 : pol[p].cur[i - 1];
                    const double uj = i + 1 >= static_cast<std::size_t>(nn) ? 0.0 : pol[p].cur[i];
                    const double g = (uj - ui) / h;
                    cell[i] += g * g;
                }
            }
            for (std::size_t i = 0; i < n; ++i) {
                force_l[i] = 0.5 * gamma * (cell[i + 1] - cell[i]);
            }
            std::vector<double> t2(n);
            for (std::size_t i = 0; i < n; ++i) {
                t2[i] = 2.0 * xi_cur[i] - xi_prev[i];
            }
            pml.multiply(t2, r1);
            cl.multiply(xi_prev, r2);
            std::vector<double> kxi;
            kl.multiply(xi_cur, kxi);
            for (std::size_t i = 0; i < n; ++i) {
                xi_next[i] = r1[i] + 0.5 * k * r2[i] - k * k * kxi[i] + k * k * force_l[i];
            }
            chol_l.solve(xi_next);
        }
        // Observations at step n.
        for (std::size_t oi = 0; oi < sc.observations.size(); ++oi) {
            const ObservationSpec& o = sc.observations[oi];
            double val = 0.0;
            switch (o.kind) {
                case ObservationKind::StringDisplacement:
                    val = interp_node(pol[o.polarisation].cur, h, o.x);
                    break;
                case ObservationKind::StringVelocity: {
                    std::vector<double> v(n);
                    for (std::size_t i = 0; i < n; ++i) {
                        v[i] =
                            (pol[o.polarisation].next[i] - pol[o.polarisation].prev[i]) / (2.0 * k);
                    }
                    val = interp_node(v, h, o.x);
                    break;
                }
                case ObservationKind::LongitudinalDisplacement:
                    if (!lon) {
                        throw RefError("longitudinal observation needs geometric.longitudinal");
                    }
                    val = interp_node(xi_cur, h, o.x);
                    break;
                case ObservationKind::LongitudinalBridgeForce: {
                    if (!lon) {
                        throw RefError("longitudinal observation needs geometric.longitudinal");
                    }
                    // F_L = EA xi_x(L) + (EA - T)/2 y_x(L)^2 with one-sided second-order slopes.
                    auto slope = [&](const std::vector<double>& f) {
                        const double fm1 = f[n - 1];
                        const double fm2 = f[n - 2];
                        return (fm2 - 4.0 * fm1) / (2.0 * h);
                    };
                    double yx2 = 0.0;
                    for (int p = 0; p < 2; ++p) {
                        const double sl = slope(pol[p].cur);
                        yx2 += sl * sl;
                    }
                    val = ph.ea * slope(xi_cur) + 0.5 * gamma * yx2;
                    break;
                }
                default:
                    throw RefError(
                        "geometric scenarios support string and longitudinal observations only");
            }
            out.signals[oi].data[step] = val;
        }
        if (options.record_energy) {
            const std::vector<double>* nw[2] = {&pol[0].next, &pol[1].next};
            const std::vector<double>* ol[2] = {&pol[0].cur, &pol[1].cur};
            store_energy(nw, ol, step + 1);
            // Loss k v^T C v, v = (u+ - u-)/(2k).
            double loss = 0.0;
            for (int p = 0; p < 2; ++p) {
                std::vector<double> vb(n);
                for (std::size_t i = 0; i < n; ++i) {
                    vb[i] = (pol[p].next[i] - pol[p].prev[i]) / (2.0 * k);
                }
                loss += k * damp.quadratic(vb, vb);
            }
            et.loss[step] = loss;
            et.balance[step] = et.total[step + 1] - et.total[step] + et.loss[step];
        }
        for (int p = 0; p < 2; ++p) {
            std::swap(pol[p].prev, pol[p].cur);
            std::swap(pol[p].cur, pol[p].next);
        }
        std::swap(xi_prev, xi_cur);
        std::swap(xi_cur, xi_next);
    }
    return out;
}

}  // namespace pme::ref
