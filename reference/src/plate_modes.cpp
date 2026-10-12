// SPDX-License-Identifier: MIT
// Modal solve of the discrete (ribbed) plate operator (P0.5-R4 follow-up P0.5-ribs, SPEC-BODY
// "Plate model (offline)"): the lowest eigenpairs of K phi = omega^2 M phi by block inverse
// (subspace) iteration with Rayleigh-Ritz and a cyclic Jacobi solver for the projected problem.
// Written in the reference: no LAPACK, no SciPy, nothing from the design compiler.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "pme/ref/common.hpp"
#include "pme/ref/plate.hpp"

namespace pme::ref {

namespace {

constexpr double kRitzTolerance = 1e-13;      // relative change of the `count` lowest Ritz values
constexpr double kLoopResidual = 1e-10;       // transformed residual required inside the loop
constexpr double kReturnResidual = 1e-9;      // physical residual checked on return
// Round-off allowance added to both residual limits: a vector stored in float64 carries relative
// errors of eps in every component, i.e. a high-frequency part of size eps whose image under K is
// about eps * rho(K), so no float64 vector can have a relative residual below eps * rho(K) / omega^2
// (about 5e-10 for the lowest mode of a 96 x 64 grid). rho(K) is bounded by the largest row sum.
constexpr double kRoundoffFactor = 4.0;
constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

using Vec = std::vector<double>;

// splitmix64: a fixed, portable generator (std distributions differ between standard libraries).
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Uniform in [-1, 1).
    double uniform() {
        return static_cast<double>(next() >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }

private:
    std::uint64_t state_;
};

double dot(const Vec& a, const Vec& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        s += a[i] * b[i];
    }
    return s;
}

// Modified Gram-Schmidt with a second pass over the columns, in column order. Throws RefError when
// a column collapses.
void orthonormalise(std::vector<Vec>& q) {
    for (std::size_t j = 0; j < q.size(); ++j) {
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < j; ++i) {
                const double d = dot(q[i], q[j]);
                for (std::size_t k = 0; k < q[j].size(); ++k) {
                    q[j][k] -= d * q[i][k];
                }
            }
        }
        const double nrm = std::sqrt(dot(q[j], q[j]));
        if (!(nrm > 0.0) || !std::isfinite(nrm)) {
            throw RefError("plate_modes: the subspace block lost rank");
        }
        for (double& v : q[j]) {
            v /= nrm;
        }
    }
}

// Cyclic Jacobi eigen-solver for a symmetric p x p matrix b (row-major, destroyed). Returns the
// eigenvalues ascending and the eigenvectors as columns of v (v[i * p + j] = component i of
// eigenvector j).
void jacobi_eigen(Vec& b, std::size_t p, Vec& values, Vec& v) {
    v.assign(p * p, 0.0);
    for (std::size_t i = 0; i < p; ++i) {
        v[i * p + i] = 1.0;
    }
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        double total = 0.0;
        for (std::size_t i = 0; i < p; ++i) {
            for (std::size_t j = 0; j < p; ++j) {
                const double x = b[i * p + j] * b[i * p + j];
                total += x;
                if (i != j) {
                    off += x;
                }
            }
        }
        if (off <= 1e-32 * total) {
            break;
        }
        for (std::size_t r = 0; r + 1 < p; ++r) {
            for (std::size_t s = r + 1; s < p; ++s) {
                const double brs = b[r * p + s];
                if (brs == 0.0) {
                    continue;
                }
                const double theta = (b[s * p + s] - b[r * p + r]) / (2.0 * brs);
                double t = 0.0;
                if (std::fabs(theta) > 1e150) {
                    t = 1.0 / (2.0 * theta);
                } else {
                    t = (theta >= 0.0 ? 1.0 : -1.0) /
                        (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                }
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double sn = t * c;
                for (std::size_t k = 0; k < p; ++k) {  // columns r, s
                    const double bkr = b[k * p + r];
                    const double bks = b[k * p + s];
                    b[k * p + r] = c * bkr - sn * bks;
                    b[k * p + s] = sn * bkr + c * bks;
                }
                for (std::size_t k = 0; k < p; ++k) {  // rows r, s
                    const double brk = b[r * p + k];
                    const double bsk = b[s * p + k];
                    b[r * p + k] = c * brk - sn * bsk;
                    b[s * p + k] = sn * brk + c * bsk;
                }
                for (std::size_t k = 0; k < p; ++k) {
                    const double vkr = v[k * p + r];
                    const double vks = v[k * p + s];
                    v[k * p + r] = c * vkr - sn * vks;
                    v[k * p + s] = sn * vkr + c * vks;
                }
            }
        }
    }
    std::vector<std::size_t> order(p);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t c) {
        return b[a * p + a] < b[c * p + c];
    });
    values.resize(p);
    Vec vs(p * p);
    for (std::size_t j = 0; j < p; ++j) {
        values[j] = b[order[j] * p + order[j]];
        for (std::size_t i = 0; i < p; ++i) {
            vs[i * p + j] = v[i * p + order[j]];
        }
    }
    v = vs;
}

}  // namespace

PlateModes plate_modes(const PlateConstants& c, const std::vector<pme::RibDesc>& ribs, int nx,
                       int ny, int count, const std::vector<std::pair<double, double>>& points,
                       const PlateModeOptions& options) {
    const PlateOperator op = build_plate_operator(c, ribs, nx, ny);
    const std::size_t n = op.grid.count;
    if (count < 1 || static_cast<std::size_t>(count) > n) {
        throw RefError("plate_modes: count must lie between 1 and the number of grid nodes");
    }
    const auto cnt = static_cast<std::size_t>(count);
    const std::size_t p =
        std::min(n, cnt + static_cast<std::size_t>(std::max(10, count / 2)));

    // A = M^(-1/2) K M^(-1/2), banded like K.
    Vec s(n);
    for (std::size_t i = 0; i < n; ++i) {
        s[i] = 1.0 / std::sqrt(op.node_mass[i]);
    }
    const std::size_t bw = op.stiffness.bandwidth();
    BandMatrix a(n, bw);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > bw ? i - bw : 0;
        const std::size_t hi = std::min(n - 1, i + bw);
        for (std::size_t j = lo; j <= hi; ++j) {
            a.at(i, j) = s[i] * op.stiffness.get(i, j) * s[j];
        }
    }
    const BandCholesky chol(a);
    // Largest absolute row sums of A and of K (Gershgorin bounds on their spectral radii).
    double gersh_a = 0.0;
    double gersh_k = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > bw ? i - bw : 0;
        const std::size_t hi = std::min(n - 1, i + bw);
        double ra = 0.0;
        double rk = 0.0;
        for (std::size_t j = lo; j <= hi; ++j) {
            ra += std::fabs(a.get(i, j));
            rk += std::fabs(op.stiffness.get(i, j));
        }
        gersh_a = std::max(gersh_a, ra);
        gersh_k = std::max(gersh_k, rk);
    }
    const double eps = std::numeric_limits<double>::epsilon();
    const double m_min = *std::min_element(op.node_mass.begin(), op.node_mass.end());

    // Fixed starting block: splitmix64 with a fixed seed, column after column.
    SplitMix64 rng(kSeed);
    std::vector<Vec> x(p, Vec(n));
    for (auto& col : x) {
        for (double& v : col) {
            v = rng.uniform();
        }
    }
    orthonormalise(x);

    // y = A v in difference form (plate_stiffness_apply): the Rayleigh quotients and residuals do not
    // carry the eps/(k h)^4 round-off of the expanded banded product.
    Vec tmp(n);
    Vec kv;
    auto apply_a = [&](const Vec& v, Vec& y) {
        for (std::size_t i = 0; i < n; ++i) {
            tmp[i] = s[i] * v[i];
        }
        plate_stiffness_apply(c, ribs, op.grid, tmp, kv);
        y.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = s[i] * kv[i];
        }
    };

    Vec theta_prev;
    Vec theta;
    std::vector<Vec> ax(p);
    Vec resid;
    Vec corr;
    int iterations = 0;
    bool converged = false;
    for (int it = 1; it <= options.max_iterations && !converged; ++it) {
        iterations = it;
        for (auto& col : x) {
            // Inverse iteration step y = A^(-1) x with one step of iterative refinement: the
            // factor belongs to the assembled banded matrix, the residual is formed in difference
            // form, so the fixed point is the eigenspace of the difference-form operator.
            const Vec rhs = col;
            chol.solve(col);
            apply_a(col, resid);
            for (std::size_t i = 0; i < n; ++i) {
                resid[i] = rhs[i] - resid[i];
            }
            chol.solve(resid);
            for (std::size_t i = 0; i < n; ++i) {
                col[i] += resid[i];
            }
        }
        orthonormalise(x);
        for (std::size_t j = 0; j < p; ++j) {
            apply_a(x[j], ax[j]);
        }
        // Rayleigh-Ritz: B = Q^T A Q.
        Vec bmat(p * p);
        for (std::size_t i = 0; i < p; ++i) {
            for (std::size_t j = i; j < p; ++j) {
                const double v = dot(x[i], ax[j]);
                bmat[i * p + j] = v;
                bmat[j * p + i] = v;
            }
        }
        Vec vec;
        jacobi_eigen(bmat, p, theta, vec);
        std::vector<Vec> xn(p, Vec(n, 0.0));
        std::vector<Vec> axn(p, Vec(n, 0.0));
        for (std::size_t j = 0; j < p; ++j) {
            for (std::size_t k = 0; k < p; ++k) {
                const double w = vec[k * p + j];
                for (std::size_t i = 0; i < n; ++i) {
                    xn[j][i] += w * x[k][i];
                    axn[j][i] += w * ax[k][i];
                }
            }
        }
        x = std::move(xn);
        ax = std::move(axn);
        bool ok = !theta_prev.empty();
        for (std::size_t j = 0; j < cnt && ok; ++j) {
            ok = std::fabs(theta[j] - theta_prev[j]) <= kRitzTolerance * theta[j];
            double r2 = 0.0;  // residual of the transformed problem, x unit length
            for (std::size_t i = 0; i < n; ++i) {
                const double r = ax[j][i] - theta[j] * x[j][i];
                r2 += r * r;
            }
            ok = ok && std::sqrt(r2) <=
                           (kLoopResidual + kRoundoffFactor * eps * gersh_a / theta[j]) * theta[j];
        }
        converged = ok;
        theta_prev = theta;
    }
    if (!converged) {
        throw RefError("plate_modes: subspace iteration did not converge in " +
                       std::to_string(options.max_iterations) + " iterations");
    }

    PlateModes out;
    out.iterations = iterations;
    out.frequencies_hz.resize(cnt);
    out.values.assign(cnt, Vec(points.size(), 0.0));
    std::vector<std::vector<std::pair<std::size_t, double>>> weights;
    weights.reserve(points.size());
    for (const auto& pt : points) {
        weights.push_back(bilinear_weights(op.grid, pt.first, pt.second));
    }
    for (std::size_t j = 0; j < cnt; ++j) {
        if (!(theta[j] > 0.0)) {
            throw RefError("plate_modes: non-positive eigenvalue");
        }
        out.frequencies_hz[j] = std::sqrt(theta[j]) / (2.0 * kPi);
        // phi = M^(-1/2) x: phi^T M phi = x^T x = 1.
        Vec phi(n);
        std::size_t big = 0;
        for (std::size_t i = 0; i < n; ++i) {
            phi[i] = s[i] * x[j][i];
            if (std::fabs(phi[i]) > std::fabs(phi[big])) {
                big = i;
            }
        }
        if (phi[big] < 0.0) {
            for (double& v : phi) {
                v = -v;
            }
        }
        // Residual check in physical space.
        Vec kphi;
        plate_stiffness_apply(c, ribs, op.grid, phi, kphi);
        double rn = 0.0;
        double mn = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double mphi = op.node_mass[i] * phi[i];
            const double r = kphi[i] - theta[j] * mphi;
            rn += r * r;
            mn += mphi * mphi;
        }
        const double rel = std::sqrt(rn) / (theta[j] * std::sqrt(mn));
        out.max_residual = std::max(out.max_residual, rel);
        if (!(rel <= kReturnResidual + kRoundoffFactor * eps * gersh_k / (theta[j] * m_min))) {
            throw RefError("plate_modes: residual of mode " + std::to_string(j + 1) +
                           " exceeds 1e-9 plus the float64 round-off allowance");
        }
        for (std::size_t q = 0; q < points.size(); ++q) {
            double v = 0.0;
            for (const auto& [idx, w] : weights[q]) {
                v += w * phi[idx];
            }
            out.values[j][q] = v;
        }
    }
    return out;
}

std::string plate_modes_to_json(const PlateModes& modes, int nx, int ny,
                                const std::vector<std::pair<double, double>>& points) {
    nlohmann::json j;
    j["count"] = modes.frequencies_hz.size();
    j["nx"] = nx;
    j["ny"] = ny;
    j["frequencies_hz"] = modes.frequencies_hz;
    j["iterations"] = modes.iterations;
    j["max_residual"] = modes.max_residual;
    if (!points.empty()) {
        nlohmann::json pts = nlohmann::json::array();
        for (const auto& pt : points) {
            pts.push_back({pt.first, pt.second});
        }
        j["points"] = pts;
        j["values"] = modes.values;
    }
    return j.dump(2);
}

}  // namespace pme::ref
