// SPDX-License-Identifier: MIT
// Reference time-domain solver: strings, hammers, bridges, plate and barriers.
// Scheme sources: Bilbao, "Numerical Sound Synthesis" (2009), sections 7.1-7.2 (stiff string,
// theta-scheme, loss terms), 8.1 (discrete gradients, Newton), 12.1 (plate). All float64.
#include "pme/ref/simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>

#include "pme/ref/banded.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/geometric.hpp"
#include "pme/ref/plate.hpp"
#include "pme/ref/string_ops.hpp"

namespace pme::ref {

const Signal* Results::find(const std::string& signal_name) const {
    for (const auto& s : signals) {
        if (s.name == signal_name) {
            return &s;
        }
    }
    return nullptr;
}

const std::vector<double>& Results::signal(const std::string& signal_name) const {
    const Signal* s = find(signal_name);
    if (s == nullptr) {
        throw RefError("no signal named '" + signal_name + "' in the results");
    }
    return s->data;
}

double max_relative_energy_step(const EnergyTrace& e) {
    if (e.total.size() < 2) {
        return 0.0;
    }
    double worst = 0.0;
    for (std::size_t n = 0; n + 1 < e.total.size(); ++n) {
        worst = std::max(worst, std::fabs(e.total[n + 1] - e.total[n]));
    }
    return worst / std::fabs(e.total[0]);
}

double max_relative_energy_imbalance(const EnergyTrace& e) {
    double worst = 0.0;
    for (const double b : e.balance) {
        worst = std::max(worst, std::fabs(b));
    }
    return e.total.empty() ? 0.0 : worst / std::fabs(e.total[0]);
}

namespace {

// ---- Felt and barrier potential V(g) = wt K [g]_+^(p+1) / (p+1) (SPEC-HAM, SPEC-PLK) -----------
struct Law {
    double wt = 1.0;
    double K = 0.0;
    double p = 2.0;
    double mu = 0.0;  // Hunt-Crossley: R = wt K mu [g]_+^p

    [[nodiscard]] double potential(double g) const {
        return g > 0.0 ? wt * K / (p + 1.0) * std::pow(g, p + 1.0) : 0.0;
    }
    [[nodiscard]] double force(double g) const { return g > 0.0 ? wt * K * std::pow(g, p) : 0.0; }
    [[nodiscard]] double dforce(double g) const {
        return g > 0.0 ? wt * K * p * std::pow(g, p - 1.0) : 0.0;
    }
};

// Two-step discrete gradient (V(a) - V(b)) / (a - b) of Bilbao 2009, section 8.1, evaluated
// without cancellation (expm1/log1p) and with its derivative with respect to a.
double discrete_gradient(const Law& law, double a, double b, double& dphi) {
    dphi = 0.0;
    if (a <= 0.0 && b <= 0.0) {
        return 0.0;
    }
    const double d = a - b;
    const double scale = std::max(std::fabs(a), std::fabs(b));
    if (d == 0.0 || std::fabs(d) < 1e-14 * scale) {
        const double mid = 0.5 * (a + b);
        dphi = 0.5 * law.dforce(mid);
        return law.force(mid);
    }
    double phi = 0.0;
    if (b <= 0.0) {
        phi = law.potential(a) / d;
    } else if (a <= 0.0) {
        phi = law.potential(b) / (b - a);
    } else {
        const double x = d / b;
        phi = law.wt * law.K / (law.p + 1.0) * std::pow(b, law.p + 1.0) *
              std::expm1((law.p + 1.0) * std::log1p(x)) / d;
    }
    if (std::fabs(d) < 1e-7 * scale) {
        dphi = 0.5 * law.dforce(0.5 * (a + b));
    } else {
        dphi = (law.force(a) - phi) / d;
    }
    return phi;
}

// ---- Model structures
// ----------------------------------------------------------------------------

struct SysState {
    std::vector<std::vector<double>> u;  // per block: interior string nodes
    std::vector<double> q;               // interface degrees of freedom
    std::vector<double> y;               // hammer positions
};

struct SysForce {
    std::vector<std::vector<double>> u;  // nodal forces (N) on the interior nodes of each block
    std::vector<double> y;               // forces on the hammers
};

struct Block {
    int string = 0;
    int pol = 0;
    int bridge = -1;
    std::size_t n = 0;     // interior unknowns N - 1
    std::size_t nloc = 0;  // n + 1 when coupled
    double h = 0.0;
    double rho_a = 0.0;
    StringStencil st;
    BandMatrix mass, stiff, damp, pm;
    BandCholesky chol;
    std::vector<double> y;  // A^-1 a
    double schur = 0.0;
    std::size_t row = 0;
    bool coupled = false;
    std::vector<double> z, xl, xm, tmp, r1, r2, r3;  // scratch
    std::vector<double> kx_cache;                    // K x of the older state (energy)
};

struct ContactEntry {
    int block = 0;
    std::size_t node = 0;
    double coef = 0.0;
};

struct Contact {
    int hammer = -1;   // owner hammer (index) or -1
    int barrier = -1;  // owner barrier (index) or -1
    double hcoef = 0.0;
    double g0 = 0.0;
    Law law;
    std::vector<ContactEntry> entries;
};

struct Hammer {
    double mass = 0.0;
    double velocity = 0.0;
    double gap = 0.0;
};

// ---- The simulation
// ------------------------------------------------------------------------------

class Sim {
public:
    Sim(Scenario sc, const SimulationOptions& opt) : sc_(std::move(sc)), opt_(opt) {}
    Results run();

private:
    Scenario sc_;
    SimulationOptions opt_;
    double k_ = 0.0;
    double theta_ = 0.25;
    std::size_t steps_ = 0;

    std::vector<StringPhysics> phys_;
    std::vector<int> grid_n_;
    std::vector<Block> blocks_;
    std::vector<std::vector<int>> string_blocks_;  // per string: block index per polarisation or -1

    // interface (q space)
    std::size_t nq_ = 0;
    bool has_plate_ = false;
    PlateConstants plate_c_;
    PlateOperator plate_op_;
    std::size_t plate_dofs_ = 0;
    std::vector<std::vector<std::pair<std::size_t, double>>> prow_;  // per (2*bridge + pol)
    std::vector<std::vector<std::pair<std::size_t, double>>>
        par_;                              // per bridge: parallel displacement
    std::vector<std::size_t> perp_index_;  // per bridge, or npos
    BandMatrix mq_, kq_, cq_, pq_;
    BandCholesky aq_;
    std::vector<double> kq_cache_;

    // contacts
    std::vector<Hammer> hammers_;
    std::vector<Contact> contacts_;
    std::vector<SysState> resp_;
    std::vector<double> gamma_;

    // scratch
    std::vector<double> ri_, rq_, tq1_, tq2_, tq3_;

    void build();
    void build_blocks();
    void build_interface();
    void build_contacts();
    [[nodiscard]] SysState zero_state() const;
    void initial_states(SysState& cur, SysState& prev);

    void advance(const SysState& cur, const SysState& prev, const SysForce* force, SysState& next);
    [[nodiscard]] double gap(const Contact& c, const SysState& s) const;
    void solve_contacts(const std::vector<double>& gfree, const std::vector<double>& gcur,
                        const std::vector<double>& gprev, std::vector<double>& gnext,
                        std::vector<double>& phi, std::size_t step, NewtonStats& stats) const;

    struct EnergyParts {
        double string = 0.0, bridge = 0.0, plate = 0.0, hammer = 0.0, contact = 0.0;
        [[nodiscard]] double total() const { return string + bridge + plate + hammer + contact; }
    };
    EnergyParts pair_energy(SysState& newer, SysState& older, const std::vector<double>& gnew,
                            const std::vector<double>& gold, bool cache_valid);
    [[nodiscard]] double step_loss(const SysState& next, const SysState& prev, const SysState& cur,
                                   const std::vector<double>& gnext,
                                   const std::vector<double>& gprev,
                                   const std::vector<double>& gcur) const;
    [[nodiscard]] double observe(const ObservationSpec& o, const SysState& cur,
                                 const SysState& prev, const SysState& next,
                                 const std::vector<double>& gcur,
                                 const std::vector<double>& phi) const;
    [[nodiscard]] double bridge_parallel(std::size_t b, const SysState& s) const;
    void block_x(const Block& b, const SysState& s, std::vector<double>& x) const;
    [[nodiscard]] double interface_w(const Block& b, const SysState& s) const;
};

void Sim::build_blocks() {
    const std::size_t ns = sc_.strings.size();
    string_blocks_.assign(ns, std::vector<int>(2, -1));
    for (std::size_t s = 0; s < ns; ++s) {
        const StringSpec& spec = sc_.strings[s];
        const StringPhysics& ph = phys_[s];
        const int nint = grid_n_[s] - 1;
        const double h = ph.length / grid_n_[s];
        // Which polarisations are needed.
        bool need_h = spec.initial.enabled && std::sin(spec.initial.angle) != 0.0;
        if (spec.bridge >= 0 &&
            sc_.bridges[static_cast<std::size_t>(spec.bridge)].desc.thetaB != 0.0) {
            need_h = true;
        }
        for (const auto& o : sc_.observations) {
            const bool string_obs = (o.kind == ObservationKind::StringDisplacement ||
                                     o.kind == ObservationKind::StringVelocity) &&
                                    o.index == static_cast<int>(s);
            if (string_obs && o.polarisation == 1) {
                need_h = true;
            }
            if (o.kind == ObservationKind::BridgeForcePerp && spec.bridge == o.index) {
                need_h = true;
            }
        }
        for (int pol = 0; pol < 2; ++pol) {
            if (pol == 1 && !need_h) {
                continue;
            }
            Block b;
            b.string = static_cast<int>(s);
            b.pol = pol;
            b.bridge = spec.bridge;
            b.coupled = spec.bridge >= 0;
            b.n = static_cast<std::size_t>(nint);
            b.nloc = b.n + (b.coupled ? 1 : 0);
            b.h = h;
            b.rho_a = ph.rho_a;
            b.st = StringStencil{b.n,
                                 h,
                                 ph.tension,
                                 ph.ei,
                                 b.coupled,
                                 spec.params.boundary == Boundary::Clamped,
                                 spec.params.boundary == Boundary::Clamped && !b.coupled};
            BandMatrix kt;
            BandMatrix kb;
            assemble_string(nint, h, b.coupled, spec.params.boundary == Boundary::Clamped,
                            spec.params.boundary == Boundary::Clamped, kt, kb);
            b.mass = BandMatrix(b.nloc, 2);
            for (std::size_t i = 0; i < b.nloc; ++i) {
                b.mass.at(i, i) = ph.rho_a * h * ((b.coupled && i == b.n) ? 0.5 : 1.0);
            }
            b.stiff = BandMatrix(b.nloc, 2);
            b.stiff.add_scaled(kt, ph.tension);
            b.stiff.add_scaled(kb, ph.ei);
            // C = 2 b1 M + 2 b3 K: the finite-difference loss 2 sigma0 d_t - 2 sigma1 d_t d_xx +
            // 2 sigma3 d_t d_xxxx with sigma1 = b3 T / rhoA, sigma3 = b3 EI / rhoA (Bilbao 2009,
            // section 7.1.3), written with the same M and K.
            b.damp = BandMatrix(b.nloc, 2);
            b.damp.add_scaled(b.mass, 2.0 * ph.b1);
            b.damp.add_scaled(b.stiff, 2.0 * ph.b3);
            b.pm = b.mass;
            b.pm.add_scaled(b.stiff, theta_ * k_ * k_);
            BandMatrix a = b.pm;
            a.add_scaled(b.damp, 0.5 * k_);
            if (b.coupled) {
                const BandMatrix interior = a.leading(b.n);
                b.chol = BandCholesky(interior);
                std::vector<double> col(b.n, 0.0);
                for (std::size_t i = (b.n >= 2 ? b.n - 2 : 0); i < b.n; ++i) {
                    col[i] = a.get(i, b.n);
                }
                b.y = col;
                b.chol.solve(b.y);
                double dot = 0.0;
                for (std::size_t i = 0; i < b.n; ++i) {
                    dot += col[i] * b.y[i];
                }
                b.schur = a.get(b.n, b.n) - dot;
                b.row = 2 * static_cast<std::size_t>(spec.bridge) + static_cast<std::size_t>(pol);
            } else {
                b.chol = BandCholesky(a);
            }
            b.z.assign(b.n, 0.0);
            b.xl.assign(b.nloc, 0.0);
            b.xm.assign(b.nloc, 0.0);
            b.tmp.assign(b.nloc, 0.0);
            string_blocks_[s][static_cast<std::size_t>(pol)] = static_cast<int>(blocks_.size());
            blocks_.push_back(std::move(b));
        }
    }
}

void Sim::build_interface() {
    const std::size_t nb = sc_.bridges.size();
    has_plate_ = sc_.plate.has_value();
    prow_.assign(2 * nb, {});
    par_.assign(nb, {});
    perp_index_.assign(nb, std::numeric_limits<std::size_t>::max());
    if (has_plate_) {
        plate_c_ = derive_plate(sc_.plate->params);
        plate_op_ = build_plate_operator(plate_c_, sc_.plate->nx, sc_.plate->ny);
        plate_dofs_ = plate_op_.grid.count;
        nq_ = plate_dofs_ + nb;
    } else {
        nq_ = 2 * nb;
    }
    if (nq_ == 0) {
        return;
    }
    std::size_t bw = 1;
    if (has_plate_) {
        bw = plate_op_.stiffness.bandwidth();
    }
    mq_ = BandMatrix(nq_, bw);
    kq_ = BandMatrix(nq_, bw);
    cq_ = BandMatrix(nq_, bw);
    std::vector<int> attached(nb, 0);
    for (const auto& s : sc_.strings) {
        if (s.bridge >= 0) {
            ++attached[static_cast<std::size_t>(s.bridge)];
        }
    }
    if (has_plate_) {
        for (std::size_t i = 0; i < plate_dofs_; ++i) {
            for (std::size_t j = (i > bw ? i - bw : 0); j <= std::min(plate_dofs_ - 1, i + bw);
                 ++j) {
                kq_.at(i, j) = plate_op_.stiffness.get(i, j);
            }
            mq_.at(i, i) = plate_op_.mass;
        }
        // Rayleigh loss sigma(omega) = b1 + b3 omega^2 -> C = 2 b1 M + 2 b3 K.
        double b1 = sc_.plate->loss_b1;
        double b3 = sc_.plate->loss_b3;
        if (b1 == 0.0 && b3 == 0.0 && sc_.plate->params.lossFactor > 0.0) {
            if (!(sc_.plate->loss_reference_hz > 0.0)) {
                throw RefError(
                    "plate.loss_reference_hz is required when PlateParams.lossFactor is set");
            }
            // sigma = eta omega / 2 at the reference frequency: b3 = eta / (2 omega_ref).
            b3 = sc_.plate->params.lossFactor / (2.0 * 2.0 * kPi * sc_.plate->loss_reference_hz);
        }
        for (std::size_t i = 0; i < plate_dofs_; ++i) {
            for (std::size_t j = (i > bw ? i - bw : 0); j <= std::min(plate_dofs_ - 1, i + bw);
                 ++j) {
                cq_.at(i, j) = 2.0 * b3 * kq_.get(i, j);
            }
            cq_.at(i, i) += 2.0 * b1 * mq_.get(i, i);
        }
    }
    for (std::size_t b = 0; b < nb; ++b) {
        const BridgeSpec& br = sc_.bridges[b];
        const double th = br.desc.thetaB;
        const double c = std::cos(th);
        const double s = std::sin(th);
        std::size_t ipar = 0;
        std::size_t iperp = 0;
        if (has_plate_) {
            if (th != 0.0) {
                throw RefError(
                    "bridges[].theta_b: a rotated bridge axis is not supported together with a "
                    "plate");
            }
            if (br.mass != 0.0 || br.stiffness != 0.0 || br.damping != 0.0 ||
                br.conductance != 0.0) {
                throw RefError(
                    "bridges[]: mass, stiffness, damping and conductance must be zero when the "
                    "bridge point sits on a plate");
            }
            par_[b] = bilinear_weights(plate_op_.grid, br.desc.position.x, br.desc.position.y);
            iperp = plate_dofs_ + b;
        } else {
            ipar = 2 * b;
            iperp = 2 * b + 1;
            par_[b] = {{ipar, 1.0}};
            if (br.kind == BridgeModelKind::Conductance) {
                if (!(br.conductance > 0.0)) {
                    throw RefError("bridges[].conductance must be positive");
                }
                cq_.at(ipar, ipar) = 1.0 / br.conductance;  // SPEC-BRG: velocity = G force
            } else {
                mq_.at(ipar, ipar) = br.mass;
                kq_.at(ipar, ipar) = br.stiffness;
                cq_.at(ipar, ipar) = br.damping;
                if (br.mass < 0.0 || br.stiffness < 0.0 || br.damping < 0.0) {
                    throw RefError("bridges[]: mass, stiffness and damping must not be negative");
                }
            }
            if (attached[b] == 0 && mq_.get(ipar, ipar) == 0.0 && kq_.get(ipar, ipar) == 0.0 &&
                cq_.get(ipar, ipar) == 0.0) {
                kq_.at(ipar, ipar) = 1.0;  // unused bridge point: keep the matrix regular
            }
        }
        // Horizontal axis: SPEC-BRG "Polarisation coupling", a pure conductance G_h (a dashpot
        // 1/G_h); G_h = 0 means a rigid horizontal axis.
        const double gh = br.desc.conductanceH;
        if (gh > 0.0) {
            cq_.at(iperp, iperp) = 1.0 / gh;
            perp_index_[b] = iperp;
        } else {
            kq_.at(iperp, iperp) = 1.0;  // dummy degree of freedom that never moves
        }
        // Rows: [w_v; w_h] = R(theta_b)^T [w_par; w_perp].
        std::vector<std::pair<std::size_t, double>> rv;
        std::vector<std::pair<std::size_t, double>> rh;
        for (const auto& [idx, wgt] : par_[b]) {
            rv.emplace_back(idx, c * wgt);
            if (s != 0.0) {
                rh.emplace_back(idx, -s * wgt);
            }
        }
        if (gh > 0.0) {
            if (s != 0.0) {
                rv.emplace_back(iperp, s);
            }
            rh.emplace_back(iperp, c);
        }
        prow_[2 * b] = rv;
        prow_[2 * b + 1] = rh;
    }
    pq_ = mq_;
    pq_.add_scaled(kq_, theta_ * k_ * k_);
    BandMatrix ahat = pq_;
    ahat.add_scaled(cq_, 0.5 * k_);
    for (const Block& b : blocks_) {
        if (!b.coupled) {
            continue;
        }
        const auto& row = prow_[b.row];
        for (const auto& [ia, ca] : row) {
            for (const auto& [ib, cb] : row) {
                ahat.at(ia, ib) += b.schur * ca * cb;
            }
        }
    }
    try {
        aq_ = BandCholesky(ahat);
    } catch (const RefError&) {
        throw RefError(
            "interface system is singular: a bridge point needs a positive mass, stiffness or "
            "damping (or a conductance)");
    }
    ri_.assign(nq_, 0.0);
    rq_.assign(nq_, 0.0);
    tq1_.assign(nq_, 0.0);
    tq2_.assign(nq_, 0.0);
    tq3_.assign(nq_, 0.0);
}

void Sim::build_contacts() {
    // Hammers (SPEC-HAM): window vector s over the struck string's interior nodes, sum s = 1.
    for (std::size_t hi = 0; hi < sc_.hammers.size(); ++hi) {
        const HammerSpec& hs = sc_.hammers[hi];
        Hammer ham;
        ham.mass = hs.mass;
        ham.velocity = hs.velocity;
        ham.gap = hs.gap > 0.0 ? hs.gap : hs.velocity * k_;
        hammers_.push_back(ham);
        Law law;
        law.K = hs.K;
        law.p = hs.p;
        law.mu = hs.mu;
        if (hs.wall) {
            Contact c;
            c.hammer = static_cast<int>(hi);
            c.hcoef = 1.0;
            c.law = law;
            contacts_.push_back(c);
            continue;
        }
        for (const int si : hs.strings) {
            const auto s = static_cast<std::size_t>(si);
            const int blk = string_blocks_[s][0];
            const double h = phys_[s].length / grid_n_[s];
            const int nint = grid_n_[s] - 1;
            std::vector<std::pair<std::size_t, double>> w;
            if (hs.width >= 2.0 * h) {
                double sum = 0.0;
                for (int i = 1; i <= nint; ++i) {
                    const double d = i * h - hs.strike_x;
                    if (std::fabs(d) < 0.5 * hs.width) {
                        // Raised cosine of full width b (SPEC-STR "Forces and observations").
                        const double wgt =
                            (1.0 + std::cos(2.0 * kPi * d / hs.width)) / hs.width * h;
                        w.emplace_back(static_cast<std::size_t>(i - 1), wgt);
                        sum += wgt;
                    }
                }
                if (w.empty() || !(sum > 0.0)) {
                    throw RefError("hammers[]: the contact window covers no grid node");
                }
                for (auto& e : w) {
                    e.second /= sum;
                }
            } else {
                const double fx = hs.strike_x / h;
                const int i0 = static_cast<int>(std::floor(fx));
                const double a = fx - i0;
                if (i0 < 1 || i0 + 1 > nint + 1) {
                    throw RefError("hammers[].strike_x: outside the string interior");
                }
                if (i0 <= nint) {
                    w.emplace_back(static_cast<std::size_t>(i0 - 1), 1.0 - a);
                }
                if (i0 + 1 <= nint) {
                    w.emplace_back(static_cast<std::size_t>(i0), a);
                }
            }
            Contact c;
            c.hammer = static_cast<int>(hi);
            c.hcoef = 1.0;
            c.law = law;
            for (const auto& [node, wt] : w) {
                c.entries.push_back({blk, node, -wt});
            }
            contacts_.push_back(c);
        }
    }
    // Barriers (SPEC-PLK): eta_j = s_b (b(x_j) - y(x_j)), V = sum_j dx_j K
    // [eta]_+^(alpha+1)/(alpha+1).
    for (std::size_t bi = 0; bi < sc_.barriers.size(); ++bi) {
        const BarrierSpec& bs = sc_.barriers[bi];
        const auto s = static_cast<std::size_t>(bs.string);
        const int blk = string_blocks_[s][0];
        const double h = phys_[s].length / grid_n_[s];
        const int nint = grid_n_[s] - 1;
        const BarrierParams& bp = bs.params;
        for (std::size_t j = 0; j < bp.x.size(); ++j) {
            const double fx = bp.x[j] / h;
            const int i0 = static_cast<int>(std::floor(fx));
            const double a = fx - i0;
            Contact c;
            c.barrier = static_cast<int>(bi);
            c.law.K = bp.K;
            c.law.p = bp.alpha;
            c.law.mu = bp.muB;
            c.law.wt = bp.w[j];
            c.g0 = bp.side * bp.b[j];
            if (i0 >= 1 && i0 <= nint) {
                c.entries.push_back({blk, static_cast<std::size_t>(i0 - 1), -bp.side * (1.0 - a)});
            }
            if (i0 + 1 >= 1 && i0 + 1 <= nint) {
                c.entries.push_back({blk, static_cast<std::size_t>(i0), -bp.side * a});
            }
            contacts_.push_back(c);
        }
    }
}

SysState Sim::zero_state() const {
    SysState s;
    s.u.resize(blocks_.size());
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        s.u[j].assign(blocks_[j].n, 0.0);
    }
    s.q.assign(nq_, 0.0);
    s.y.assign(hammers_.size(), 0.0);
    return s;
}

double Sim::interface_w(const Block& b, const SysState& s) const {
    double w = 0.0;
    for (const auto& [idx, coef] : prow_[b.row]) {
        w += coef * s.q[idx];
    }
    return w;
}

void Sim::block_x(const Block& b, const SysState& s, std::vector<double>& x) const {
    const auto& u = s.u[static_cast<std::size_t>(&b - blocks_.data())];
    std::copy(u.begin(), u.end(), x.begin());
    if (b.coupled) {
        x[b.n] = interface_w(b, s);
    }
}

void Sim::advance(const SysState& cur, const SysState& prev, const SysForce* force,
                  SysState& next) {
    const double k2 = k_ * k_;
    if (nq_ > 0) {
        std::fill(ri_.begin(), ri_.end(), 0.0);
    }
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        Block& b = blocks_[j];
        block_x(b, cur, b.xl);
        block_x(b, prev, b.xm);
        for (std::size_t i = 0; i < b.nloc; ++i) {
            b.tmp[i] = 2.0 * b.xl[i] - b.xm[i];
        }
        b.pm.multiply(b.tmp, b.r1);
        b.damp.multiply(b.xm, b.r2);
        apply_string_stiffness(b.st, b.xl, b.r3);
        for (std::size_t i = 0; i < b.nloc; ++i) {
            b.r1[i] += 0.5 * k_ * b.r2[i] - k2 * b.r3[i];
        }
        // r1 = right-hand side of [A_ss a; a^T A_NN][u; w] = r.
        for (std::size_t i = 0; i < b.n; ++i) {
            b.z[i] = b.r1[i];
        }
        if (force != nullptr) {
            const auto& fu = force->u[j];
            for (std::size_t i = 0; i < b.n; ++i) {
                b.z[i] += k2 * fu[i];
            }
        }
        if (b.coupled) {
            double dot = 0.0;
            for (std::size_t i = 0; i < b.n; ++i) {
                dot += b.y[i] * b.z[i];
            }
            const double contrib = b.r1[b.n] - dot;
            for (const auto& [idx, coef] : prow_[b.row]) {
                ri_[idx] += coef * contrib;
            }
        }
        b.chol.solve(b.z);
    }
    if (nq_ > 0) {
        for (std::size_t i = 0; i < nq_; ++i) {
            tq1_[i] = 2.0 * cur.q[i] - prev.q[i];
        }
        pq_.multiply(tq1_, rq_);
        cq_.multiply(prev.q, tq2_);
        kq_.multiply(cur.q, tq3_);
        for (std::size_t i = 0; i < nq_; ++i) {
            ri_[i] += rq_[i] + 0.5 * k_ * tq2_[i] - k2 * tq3_[i];
        }
        next.q = ri_;
        aq_.solve(next.q);
    }
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        Block& b = blocks_[j];
        auto& un = next.u[j];
        if (b.coupled) {
            const double w = interface_w(b, next);
            for (std::size_t i = 0; i < b.n; ++i) {
                un[i] = b.z[i] - b.y[i] * w;
            }
        } else {
            for (std::size_t i = 0; i < b.n; ++i) {
                un[i] = b.z[i];
            }
        }
    }
    for (std::size_t h = 0; h < hammers_.size(); ++h) {
        const double f = force != nullptr ? force->y[h] : 0.0;
        next.y[h] = 2.0 * cur.y[h] - prev.y[h] + k2 * f / hammers_[h].mass;
    }
}

double Sim::gap(const Contact& c, const SysState& s) const {
    double g = c.g0;
    if (c.hammer >= 0) {
        g += c.hcoef * s.y[static_cast<std::size_t>(c.hammer)];
    }
    for (const auto& e : c.entries) {
        g += e.coef * s.u[static_cast<std::size_t>(e.block)][e.node];
    }
    return g;
}

void Sim::solve_contacts(const std::vector<double>& gfree, const std::vector<double>& gcur,
                         const std::vector<double>& gprev, std::vector<double>& g,
                         std::vector<double>& phi, std::size_t step, NewtonStats& stats) const {
    const std::size_t nc = contacts_.size();
    std::vector<double> dphi(nc, 0.0);
    std::vector<double> f(nc, 0.0);
    std::vector<double> rcoef(nc, 0.0);
    double scale = 1e-300;
    for (std::size_t c = 0; c < nc; ++c) {
        scale = std::max({scale, std::fabs(gfree[c]), std::fabs(gprev[c]), std::fabs(gcur[c])});
        // Hunt-Crossley dashpot R = wt K mu [g^n]_+^p along the contact direction (SPEC-HAM).
        rcoef[c] = contacts_[c].law.mu * contacts_[c].law.force(gcur[c]);
    }
    auto evaluate = [&](const std::vector<double>& gg, std::vector<double>& ph,
                        std::vector<double>& dph, std::vector<double>& ff) {
        for (std::size_t c = 0; c < nc; ++c) {
            double d = 0.0;
            const double base = discrete_gradient(contacts_[c].law, gg[c], gprev[c], d);
            ph[c] = base + rcoef[c] * (gg[c] - gprev[c]) / (2.0 * k_);
            dph[c] = d + rcoef[c] / (2.0 * k_);
            if (gg[c] <= 0.0 && gprev[c] <= 0.0 && gcur[c] <= 0.0) {
                ph[c] = 0.0;
                dph[c] = 0.0;
            }
        }
        double res = 0.0;
        for (std::size_t c = 0; c < nc; ++c) {
            double s = gg[c] - gfree[c];
            for (std::size_t c2 = 0; c2 < nc; ++c2) {
                s += gamma_[c * nc + c2] * ph[c2];
            }
            ff[c] = s;
            res = std::max(res, std::fabs(s));
        }
        return res;
    };
    g = gfree;
    double res = evaluate(g, phi, dphi, f);
    const double tol = 1e-14 * scale;        // convergence: 1e-14 relative to the largest gap
    const double floor_tol = 1e-13 * scale;  // round-off floor at which a stalled iteration stops
    bool stalled = false;
    int it = 0;
    std::vector<double> gnew(nc);
    std::vector<double> phin(nc);
    std::vector<double> dphin(nc);
    std::vector<double> fnew(nc);
    for (; it < 50 && res > tol; ++it) {
        std::vector<std::size_t> act;
        for (std::size_t c = 0; c < nc; ++c) {
            if (g[c] > 0.0 || gprev[c] > 0.0 || gcur[c] > 0.0) {
                act.push_back(c);
            }
        }
        const std::size_t na = act.size();
        std::vector<double> dg(nc, 0.0);
        if (na > 0) {
            std::vector<double> jac(na * na, 0.0);
            std::vector<double> rhs(na, 0.0);
            for (std::size_t a = 0; a < na; ++a) {
                for (std::size_t b = 0; b < na; ++b) {
                    jac[a * na + b] = gamma_[act[a] * nc + act[b]] * dphi[act[b]];
                }
                jac[a * na + a] += 1.0;
                rhs[a] = -f[act[a]];
            }
            solve_dense(jac, rhs, na);
            for (std::size_t a = 0; a < na; ++a) {
                dg[act[a]] = rhs[a];
            }
        }
        for (std::size_t c = 0; c < nc; ++c) {
            if (std::find(act.begin(), act.end(), c) != act.end()) {
                continue;
            }
            double s = -f[c];
            for (const std::size_t a : act) {
                s -= gamma_[c * nc + a] * dphi[a] * dg[a];
            }
            dg[c] = s;
        }
        double lambda = 1.0;
        double rn = res;
        bool accepted = false;
        for (int ls = 0; ls < 40; ++ls) {
            for (std::size_t c = 0; c < nc; ++c) {
                gnew[c] = g[c] + lambda * dg[c];
            }
            rn = evaluate(gnew, phin, dphin, fnew);
            if (rn < res || rn <= tol) {
                accepted = true;
                break;
            }
            lambda *= 0.5;
        }
        if (!accepted) {
            // No further descent: only round-off remains if the residual is already tiny.
            if (res <= floor_tol) {
                stalled = true;
                break;
            }
            throw RefError("contact Newton solve stalled at step " + std::to_string(step) +
                           " (residual " + std::to_string(res / scale) + ")");
        }
        g = gnew;
        phi = phin;
        dphi = dphin;
        f = fnew;
        res = rn;
    }
    if (res > tol && !stalled) {
        throw RefError("contact Newton solve did not converge in 50 iterations at step " +
                       std::to_string(step) + " (relative residual " + std::to_string(res / scale) +
                       ")");
    }
    stats.max_iterations = std::max(stats.max_iterations, it);
    stats.max_relative_residual = std::max(stats.max_relative_residual, res / scale);
    ++stats.contact_steps;
}

double Sim::bridge_parallel(std::size_t b, const SysState& s) const {
    double w = 0.0;
    for (const auto& [idx, coef] : par_[b]) {
        w += coef * s.q[idx];
    }
    return w;
}

Sim::EnergyParts Sim::pair_energy(SysState& newer, SysState& older, const std::vector<double>& gnew,
                                  const std::vector<double>& gold, bool /*cache_valid*/) {
    EnergyParts e;
    // H^(n-1/2) = 1/2 |v|_M^2 + 1/2 x^n . K x^(n-1) + theta k^2 / 2 |v|_K^2, v = (x^n - x^(n-1))/k
    // (Bilbao 2009, section 7.2.2, energy of the theta-scheme).
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        Block& b = blocks_[j];
        std::vector<double> xn(b.nloc);
        std::vector<double> xo(b.nloc);
        block_x(b, newer, xn);
        block_x(b, older, xo);
        std::vector<double> kxn;
        std::vector<double> kxo;
        apply_string_stiffness(b.st, xn, kxn);
        apply_string_stiffness(b.st, xo, kxo);
        double kin = 0.0;
        double pot = 0.0;
        double ker = 0.0;
        for (std::size_t i = 0; i < b.nloc; ++i) {
            const double v = (xn[i] - xo[i]) / k_;
            kin += 0.5 * b.mass.get(i, i) * v * v;
            pot += 0.5 * xn[i] * kxo[i];
            ker += 0.5 * theta_ * k_ * k_ * v * (kxn[i] - kxo[i]) / k_;
        }
        e.string += kin + pot + ker;
    }
    if (nq_ > 0) {
        std::vector<double> kxn;
        std::vector<double> kxo;
        kq_.multiply(newer.q, kxn);
        kq_.multiply(older.q, kxo);
        for (std::size_t i = 0; i < nq_; ++i) {
            const double v = (newer.q[i] - older.q[i]) / k_;
            const double part = 0.5 * mq_.get(i, i) * v * v + 0.5 * newer.q[i] * kxo[i] +
                                0.5 * theta_ * k_ * v * (kxn[i] - kxo[i]);
            if (has_plate_ && i < plate_dofs_) {
                e.plate += part;
            } else {
                e.bridge += part;
            }
        }
    }
    for (std::size_t h = 0; h < hammers_.size(); ++h) {
        const double v = (newer.y[h] - older.y[h]) / k_;
        e.hammer += 0.5 * hammers_[h].mass * v * v;
    }
    for (std::size_t c = 0; c < contacts_.size(); ++c) {
        e.contact +=
            0.5 * (contacts_[c].law.potential(gnew[c]) + contacts_[c].law.potential(gold[c]));
    }
    return e;
}

double Sim::step_loss(const SysState& next, const SysState& prev, const SysState& /*cur*/,
                      const std::vector<double>& gnext, const std::vector<double>& gprev,
                      const std::vector<double>& gcur) const {
    double loss = 0.0;
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        const Block& b = blocks_[j];
        std::vector<double> xn(b.nloc);
        std::vector<double> xo(b.nloc);
        block_x(b, next, xn);
        block_x(b, prev, xo);
        std::vector<double> vb(b.nloc);
        for (std::size_t i = 0; i < b.nloc; ++i) {
            vb[i] = (xn[i] - xo[i]) / (2.0 * k_);
        }
        loss += k_ * b.damp.quadratic(vb, vb);
    }
    if (nq_ > 0) {
        std::vector<double> vb(nq_);
        for (std::size_t i = 0; i < nq_; ++i) {
            vb[i] = (next.q[i] - prev.q[i]) / (2.0 * k_);
        }
        loss += k_ * cq_.quadratic(vb, vb);
    }
    for (std::size_t c = 0; c < contacts_.size(); ++c) {
        const double r = contacts_[c].law.mu * contacts_[c].law.force(gcur[c]);
        const double dg = (gnext[c] - gprev[c]) / (2.0 * k_);
        loss += k_ * r * dg * dg;
    }
    return loss;
}

double Sim::observe(const ObservationSpec& o, const SysState& cur, const SysState& prev,
                    const SysState& next, const std::vector<double>& gcur,
                    const std::vector<double>& phi) const {
    auto interp = [&](const std::vector<double>& u, double h, double x, int nint) {
        const double fx = x / h;
        int i0 = static_cast<int>(std::floor(fx));
        i0 = std::max(0, std::min(i0, nint));
        const double a = fx - i0;
        auto node = [&](int i) {
            return (i >= 1 && i <= nint) ? u[static_cast<std::size_t>(i - 1)] : 0.0;
        };
        return (1.0 - a) * node(i0) + a * node(i0 + 1);
    };
    switch (o.kind) {
        case ObservationKind::StringDisplacement:
        case ObservationKind::StringVelocity: {
            const auto s = static_cast<std::size_t>(o.index);
            const int blk = string_blocks_[s][static_cast<std::size_t>(o.polarisation)];
            if (blk < 0) {
                return 0.0;
            }
            const auto bj = static_cast<std::size_t>(blk);
            const double h = blocks_[bj].h;
            const int nint = static_cast<int>(blocks_[bj].n);
            if (o.kind == ObservationKind::StringDisplacement) {
                return interp(cur.u[bj], h, o.x, nint);
            }
            std::vector<double> d(blocks_[bj].n);
            for (std::size_t i = 0; i < d.size(); ++i) {
                d[i] = (next.u[bj][i] - prev.u[bj][i]) / (2.0 * k_);
            }
            return interp(d, h, o.x, nint);
        }
        case ObservationKind::BridgeForce:
        case ObservationKind::BridgeForcePerp: {
            const auto b = static_cast<std::size_t>(o.index);
            double fv = 0.0;
            double fh = 0.0;
            for (std::size_t j = 0; j < blocks_.size(); ++j) {
                const Block& bk = blocks_[j];
                if (!bk.coupled || bk.bridge != o.index) {
                    continue;
                }
                std::vector<double> x(bk.nloc);
                block_x(bk, cur, x);
                double f = 0.0;
                for (std::size_t i = (bk.n >= 2 ? bk.n - 2 : 0); i <= bk.n; ++i) {
                    f += bk.stiff.get(bk.n, i) * x[i];
                }
                (bk.pol == 0 ? fv : fh) -= f;  // force of the string on the bridge, SPEC-BRG
            }
            const double th = sc_.bridges[b].desc.thetaB;
            return o.kind == ObservationKind::BridgeForce ? std::cos(th) * fv - std::sin(th) * fh
                                                          : std::sin(th) * fv + std::cos(th) * fh;
        }
        case ObservationKind::BridgeDisplacement:
            return bridge_parallel(static_cast<std::size_t>(o.index), cur);
        case ObservationKind::BridgeVelocity:
            return (bridge_parallel(static_cast<std::size_t>(o.index), next) -
                    bridge_parallel(static_cast<std::size_t>(o.index), prev)) /
                   (2.0 * k_);
        case ObservationKind::HammerForce: {
            double f = 0.0;
            for (std::size_t c = 0; c < contacts_.size(); ++c) {
                if (contacts_[c].hammer == o.index) {
                    f += phi[c];
                }
            }
            return f;
        }
        case ObservationKind::HammerPenetration: {
            for (std::size_t c = 0; c < contacts_.size(); ++c) {
                if (contacts_[c].hammer == o.index) {
                    return gcur[c];
                }
            }
            return 0.0;
        }
        case ObservationKind::HammerPosition:
            return cur.y[static_cast<std::size_t>(o.index)];
        case ObservationKind::BarrierForce: {
            double f = 0.0;
            for (std::size_t c = 0; c < contacts_.size(); ++c) {
                if (contacts_[c].barrier == o.index) {
                    f += phi[c];
                }
            }
            return f;
        }
        case ObservationKind::PlateDisplacement: {
            if (!has_plate_) {
                throw RefError("observation of the plate without a plate");
            }
            double w = 0.0;
            for (const auto& [idx, wt] : bilinear_weights(plate_op_.grid, o.point.x, o.point.y)) {
                w += wt * cur.q[idx];
            }
            return w;
        }
        case ObservationKind::LongitudinalBridgeForce:
        case ObservationKind::LongitudinalDisplacement:
            throw RefError(
                "longitudinal observations need geometric.longitudinal on a fixed-end string");
    }
    return 0.0;
}

void Sim::initial_states(SysState& cur, SysState& prev) {
    cur = zero_state();
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        const Block& b = blocks_[j];
        const StringSpec& spec = sc_.strings[static_cast<std::size_t>(b.string)];
        if (!spec.initial.enabled) {
            continue;
        }
        const double len = phys_[static_cast<std::size_t>(b.string)].length;
        const double xp = spec.initial.position;
        if (!(xp > 0.0) || !(xp < len)) {
            throw RefError("strings[].initial.position must lie inside the string");
        }
        const double amp = spec.initial.amplitude * (b.pol == 0 ? std::cos(spec.initial.angle)
                                                                : std::sin(spec.initial.angle));
        // SPEC-PLK "Initial displacement": triangle of height A with apex at x_p, released at rest.
        for (std::size_t i = 0; i < b.n; ++i) {
            const double x = static_cast<double>(i + 1) * b.h;
            cur.u[j][i] = x <= xp ? amp * x / xp : amp * (len - x) / (len - xp);
        }
    }
    prev = cur;
    // Start at rest: the time-symmetric start x^-1 = x^1 of the theta-scheme, i.e.
    // x^-1 = x^0 - k^2/2 (M + theta k^2 K + k C / 2)^-1 K x^0 (the bridge at rest). The damping
    // term makes the start symmetric only to O(k), which is immaterial for a start at rest.
    for (std::size_t j = 0; j < blocks_.size(); ++j) {
        Block& b = blocks_[j];
        std::vector<double> x(b.nloc, 0.0);
        std::copy(cur.u[j].begin(), cur.u[j].end(), x.begin());
        std::vector<double> kx;
        b.stiff.multiply(x, kx);
        kx.resize(b.n);
        b.chol.solve(kx);
        for (std::size_t i = 0; i < b.n; ++i) {
            prev.u[j][i] = cur.u[j][i] - 0.5 * k_ * k_ * kx[i];
        }
    }
    if (has_plate_ && sc_.plate.has_value() && sc_.plate->has_initial) {
        const PlateSpec& ps = *sc_.plate;
        for (const auto& [idx, wgt] :
             bilinear_weights(plate_op_.grid, ps.initial_x, ps.initial_y)) {
            cur.q[idx] = ps.initial_amplitude * wgt;
        }
        // Time-symmetric start at rest, as for the strings.
        std::vector<double> kq;
        kq_.multiply(cur.q, kq);
        aq_.solve(kq);
        prev.q = cur.q;
        for (std::size_t i = 0; i < nq_; ++i) {
            prev.q[i] -= 0.5 * k_ * k_ * kq[i];
        }
    }
    for (std::size_t h = 0; h < hammers_.size(); ++h) {
        cur.y[h] = -hammers_[h].gap;
        prev.y[h] = cur.y[h] - hammers_[h].velocity * k_;
    }
}

void Sim::build() {
    validate_scenario(sc_);
    theta_ = sc_.theta;
    k_ = 1.0 / (sc_.fs * sc_.oversampling);
    steps_ = static_cast<std::size_t>(std::llround(sc_.duration / k_));
    for (const auto& s : sc_.strings) {
        phys_.push_back(derive_string(s.params));
        grid_n_.push_back(s.intervals);
    }
    build_blocks();
    build_interface();
    build_contacts();
}

Results Sim::run() {
    build();
    Results out;
    out.name = sc_.name;
    out.fs = sc_.fs;
    out.oversampling = sc_.oversampling;
    out.sim_rate = sc_.fs * sc_.oversampling;
    out.dt = k_;
    out.steps = steps_;

    SysState prev;
    SysState cur;
    initial_states(cur, prev);
    SysState next = zero_state();
    const std::size_t nc = contacts_.size();

    // Unit responses of every contact direction (generalised force -grad g), for the contact solve.
    if (nc > 0) {
        resp_.assign(nc, zero_state());
        gamma_.assign(nc * nc, 0.0);
        const SysState zero = zero_state();
        for (std::size_t c = 0; c < nc; ++c) {
            SysForce f;
            f.u.resize(blocks_.size());
            for (std::size_t j = 0; j < blocks_.size(); ++j) {
                f.u[j].assign(blocks_[j].n, 0.0);
            }
            f.y.assign(hammers_.size(), 0.0);
            for (const auto& e : contacts_[c].entries) {
                f.u[static_cast<std::size_t>(e.block)][e.node] -= e.coef;
            }
            if (contacts_[c].hammer >= 0) {
                f.y[static_cast<std::size_t>(contacts_[c].hammer)] -= contacts_[c].hcoef;
            }
            advance(zero, zero, &f, resp_[c]);
        }
        for (std::size_t c = 0; c < nc; ++c) {
            for (std::size_t c2 = 0; c2 < nc; ++c2) {
                // Gamma_cc' = a_c . resp_c' with a_c = -grad g_c.
                const SysState& r = resp_[c2];
                double s = 0.0;
                for (const auto& e : contacts_[c].entries) {
                    s -= e.coef * r.u[static_cast<std::size_t>(e.block)][e.node];
                }
                if (contacts_[c].hammer >= 0) {
                    s -= contacts_[c].hcoef * r.y[static_cast<std::size_t>(contacts_[c].hammer)];
                }
                gamma_[c * nc + c2] = s;
            }
        }
    }

    out.signals.resize(sc_.observations.size());
    for (std::size_t i = 0; i < sc_.observations.size(); ++i) {
        out.signals[i].name = sc_.observations[i].name;
        out.signals[i].data.assign(steps_, 0.0);
    }
    EnergyTrace& et = out.energy;
    if (opt_.record_energy) {
        for (auto* v : {&et.total, &et.string, &et.bridge, &et.plate, &et.hammer, &et.contact}) {
            v->assign(steps_ + 1, 0.0);
        }
        et.loss.assign(steps_, 0.0);
        et.balance.assign(steps_, 0.0);
    }

    std::vector<double> gcur(nc);
    std::vector<double> gprev(nc);
    std::vector<double> gnext(nc);
    std::vector<double> gfree(nc);
    std::vector<double> phi(nc, 0.0);
    for (std::size_t c = 0; c < nc; ++c) {
        gcur[c] = gap(contacts_[c], cur);
        gprev[c] = gap(contacts_[c], prev);
    }
    auto store_energy = [&](std::size_t idx, SysState& newer, SysState& older,
                            const std::vector<double>& gn, const std::vector<double>& go) {
        const EnergyParts e = pair_energy(newer, older, gn, go, false);
        et.total[idx] = e.total();
        et.string[idx] = e.string;
        et.bridge[idx] = e.bridge;
        et.plate[idx] = e.plate;
        et.hammer[idx] = e.hammer;
        et.contact[idx] = e.contact;
    };
    if (opt_.record_energy) {
        store_energy(0, cur, prev, gcur, gprev);
    }

    for (std::size_t n = 0; n < steps_; ++n) {
        advance(cur, prev, nullptr, next);
        std::fill(phi.begin(), phi.end(), 0.0);
        bool any = false;
        for (std::size_t c = 0; c < nc; ++c) {
            gfree[c] = gap(contacts_[c], next);
            gnext[c] = gfree[c];
            if (gfree[c] > 0.0 || gprev[c] > 0.0 || gcur[c] > 0.0) {
                any = true;
            }
        }
        if (any) {
            solve_contacts(gfree, gcur, gprev, gnext, phi, n, out.newton);
            for (std::size_t c = 0; c < nc; ++c) {
                if (phi[c] == 0.0) {
                    continue;
                }
                const SysState& r = resp_[c];
                for (std::size_t j = 0; j < blocks_.size(); ++j) {
                    for (std::size_t i = 0; i < blocks_[j].n; ++i) {
                        next.u[j][i] += phi[c] * r.u[j][i];
                    }
                }
                for (std::size_t i = 0; i < nq_; ++i) {
                    next.q[i] += phi[c] * r.q[i];
                }
                for (std::size_t h = 0; h < hammers_.size(); ++h) {
                    next.y[h] += phi[c] * r.y[h];
                }
            }
            // Recompute the gaps from the corrected state (consistent to round-off with gnext).
            for (std::size_t c = 0; c < nc; ++c) {
                gnext[c] = gap(contacts_[c], next);
            }
        }
        for (std::size_t i = 0; i < sc_.observations.size(); ++i) {
            out.signals[i].data[n] = observe(sc_.observations[i], cur, prev, next, gcur, phi);
        }
        if (opt_.record_energy) {
            store_energy(n + 1, next, cur, gnext, gcur);
            et.loss[n] = step_loss(next, prev, cur, gnext, gprev, gcur);
            et.balance[n] = et.total[n + 1] - et.total[n] + et.loss[n];
        }
        std::swap(prev, cur);
        std::swap(cur, next);
        gprev = gcur;
        gcur = gnext;
    }
    return out;
}

}  // namespace

Results simulate(const Scenario& scenario, const SimulationOptions& options) {
    validate_scenario(scenario);
    for (const auto& s : scenario.strings) {
        if (s.geometric.tension_modulation || s.geometric.longitudinal) {
            return simulate_geometric(scenario, options);
        }
    }
    Sim sim(scenario, options);
    return sim.run();
}

}  // namespace pme::ref
