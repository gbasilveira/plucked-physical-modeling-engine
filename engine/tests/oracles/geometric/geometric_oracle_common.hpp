// SPDX-License-Identifier: MIT
// Shared helpers of the A7 oracle tests ORC-11, ORC-12 and A7-T3 (SPEC-GEO "Geometric nonlinearity
// and longitudinal waves"; SPEC-ORC rows "Longitudinal forcing" and "Tension modulation"). Every
// expected value here comes from the SPEC formulas, written out independently of the engine:
//
//   * the slope y_x = sum_n q_n sqrt(2/L) k_n cos(k_n x), k_n = n pi / L (orthonormal sine modes,
//     SPEC-CONV), and the same for z_x of the horizontal polarisation;
//   * the quadrature of  f_m^L = -(EA - T) / (2 rhoA) * Int_0^L (y_x^2 + z_x^2) chi_m'(x) dx  with
//     chi_m' = sqrt(2/L) K_m cos(K_m x)  (SPEC-ORC "Longitudinal forcing"), composite
//     Gauss-Legendre in float64;
//   * the quadrature of S = Int_0^L (y_x^2 + z_x^2) dx and DeltaT = (EA - T) S / (2 L)
//     (SPEC-ORC "Tension modulation");
//   * the SPEC-GEO pair sum
//         f_m^L = -(EA - T) K_m / (2 rhoA sqrt(2 L)) sum_P sum_{i,j>=1} k_i k_j q_P,i q_P,j
//                 [delta_{i+j,m} + delta_{|i-j|,m}]
//     written out here as an independent O(N) per mode loop (the formula check of ORC-11);
//   * the arrival-time estimator of A7-T3: zero-phase low-pass, half-height crossing.
//
// Only public headers (pme/string.hpp) and the A2 oracle helpers (string_oracle_common.hpp, read
// only: Gauss-Legendre, report helpers) are used.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "string_oracle_common.hpp"

namespace pme::oracle::geo {

namespace str = pme::oracle::str;
using str::kPi;

/// SPEC-GEO: EA is the core's axial stiffness E_c pi d_c^2 / 4.
inline double coreEA(const str::StringSpec& s) { return s.ECore * kPi * s.dCore * s.dCore / 4.0; }

/// SplitMix64 (public-domain generator), so the random states are identical on every platform.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }
    /// Uniform in [-1, 1).
    double uniform() { return 2.0 * static_cast<double>(next() >> 11U) / 9007199254740992.0 - 1.0; }

private:
    std::uint64_t state_;
};

/// Modal states of the two polarisations, float32 as in the engine (m^(3/2)).
struct ModalStates {
    std::vector<float> qv;
    std::vector<float> qh;
};

/// Random states of N modes: q_n = u_n * amplitude * sqrt(L/2) / n with u_n uniform in [-1, 1), so
/// that the displacement of mode n is of order `amplitude` (m) / n: a forte note has about 1 mm.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline ModalStates randomStates(int n, double length, std::uint64_t seed, double amplitude,
                                bool vertical, bool horizontal) {
    SplitMix64 rng(seed);
    ModalStates s;
    s.qv.assign(static_cast<std::size_t>(n), 0.0F);
    s.qh.assign(static_cast<std::size_t>(n), 0.0F);
    for (int i = 1; i <= n; ++i) {
        const double scale = amplitude * std::sqrt(length / 2.0) / static_cast<double>(i);
        const double a = rng.uniform() * scale;
        const double b = rng.uniform() * scale;  // drawn in every case: the sequences are fixed
        if (vertical) {
            s.qv[static_cast<std::size_t>(i - 1)] = static_cast<float>(a);
        }
        if (horizontal) {
            s.qh[static_cast<std::size_t>(i - 1)] = static_cast<float>(b);
        }
    }
    return s;
}

/// Slope y_x(x) = sum_n q_n sqrt(2/L) k_n cos(k_n x) (SPEC-STR "Modes", SPEC-CONV).
inline double slopeAt(double length, std::span<const float> q, double x) {
    double sum = 0.0;
    for (std::size_t i = 0; i < q.size(); ++i) {
        const double kn = static_cast<double>(i + 1) * kPi / length;
        sum += static_cast<double>(q[i]) * std::sqrt(2.0 / length) * kn *
               std::cos(static_cast<double>(i + 1) * kPi * (x / length));
    }
    return sum;
}

/// Integrals over [0, L] by composite Gauss-Legendre with `panels` equal panels:
///   slope2 = Int (y_x^2 + z_x^2) dx and, for m = 1..M, mode[m-1] = Int (y_x^2 + z_x^2) chi_m' dx.
struct SlopeQuadrature {
    double slope2{};
    std::vector<double> chiPrime;  ///< Int (y_x^2 + z_x^2) chi_m' dx, m = 1..M
};

inline SlopeQuadrature quadrature(double length, std::span<const float> qv,
                                  std::span<const float> qh, int modesM,
                                  const str::GaussLegendre& rule, int panels) {
    SlopeQuadrature r;
    r.chiPrime.assign(static_cast<std::size_t>(modesM), 0.0);
    const double h = length / static_cast<double>(panels);
    for (int p = 0; p < panels; ++p) {
        const double mid = (static_cast<double>(p) + 0.5) * h;
        for (std::size_t g = 0; g < rule.x.size(); ++g) {
            const double x = mid + 0.5 * h * rule.x[g];
            const double w = rule.w[g] * 0.5 * h;
            const double yx = slopeAt(length, qv, x);
            const double zx = slopeAt(length, qh, x);
            const double s2 = yx * yx + zx * zx;
            r.slope2 += w * s2;
            for (int m = 1; m <= modesM; ++m) {
                const double km = static_cast<double>(m) * kPi / length;
                r.chiPrime[static_cast<std::size_t>(m - 1)] +=
                    w * s2 * std::sqrt(2.0 / length) * km *
                    std::cos(static_cast<double>(m) * kPi * (x / length));
            }
        }
    }
    return r;
}

/// SPEC-ORC "Longitudinal forcing" reference: f_m^L = -(EA - T) / (2 rhoA) Int (...) chi_m' dx,
/// from the finest of two panel counts, whose difference (the quadrature's own error estimate,
/// relative to the largest |f_m^L|) is returned in `selfError`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline std::vector<double> quadratureForce(double length, double ea, double tension, double rhoA,
                                           std::span<const float> qv, std::span<const float> qh,
                                           int modesM, double& selfError, double* slope2Out) {
    static const str::GaussLegendre rule = str::gaussLegendre(32);
    const SlopeQuadrature coarse = quadrature(length, qv, qh, modesM, rule, 48);
    const SlopeQuadrature fine = quadrature(length, qv, qh, modesM, rule, 96);
    const double factor = -(ea - tension) / (2.0 * rhoA);
    std::vector<double> f(static_cast<std::size_t>(modesM));
    double largest = 0.0;
    for (std::size_t m = 0; m < f.size(); ++m) {
        f[m] = factor * fine.chiPrime[m];
        largest = std::max(largest, std::fabs(f[m]));
    }
    selfError = 0.0;
    for (std::size_t m = 0; m < f.size(); ++m) {
        selfError =
            std::max(selfError, std::fabs(factor * (fine.chiPrime[m] - coarse.chiPrime[m])));
    }
    selfError = largest > 0.0 ? selfError / largest : selfError;
    if (slope2Out != nullptr) {
        // Relative panel-halving difference of S is folded into the same figure.
        selfError = std::max(selfError, std::fabs(fine.slope2 - coarse.slope2) / fine.slope2);
        *slope2Out = fine.slope2;
    }
    return f;
}

/// SPEC-GEO pair sum, an independent O(N) per mode loop. For each m the ordered pairs (i, j) with
/// i + j = m (i = 1..m-1) and with |i - j| = m (j = i + m and its mirror image, hence the 2).
inline std::vector<double> specPairSumForce(double length, double ea, double tension, double rhoA,
                                            std::span<const float> qv, std::span<const float> qh,
                                            int modesM) {
    const std::size_t n = qv.size();
    std::array<std::vector<double>, 2> kq;
    for (std::size_t p = 0; p < 2; ++p) {
        const std::span<const float> q = (p == 0) ? qv : qh;
        kq[p].resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            kq[p][i] = static_cast<double>(i + 1) * kPi / length * static_cast<double>(q[i]);
        }
    }
    std::vector<double> f(static_cast<std::size_t>(modesM));
    for (int m = 1; m <= modesM; ++m) {
        double sum = 0.0;
        for (const auto& a : kq) {
            for (int i = 1; i < m; ++i) {
                sum += a[static_cast<std::size_t>(i - 1)] * a[static_cast<std::size_t>(m - i - 1)];
            }
            for (std::size_t i = 1; i + static_cast<std::size_t>(m) <= n; ++i) {
                sum += 2.0 * a[i - 1] * a[i + static_cast<std::size_t>(m) - 1];
            }
        }
        const double km = static_cast<double>(m) * kPi / length;
        f[static_cast<std::size_t>(m - 1)] =
            -(ea - tension) * km / (2.0 * rhoA * std::sqrt(2.0 * length)) * sum;
    }
    return f;
}

/// SPEC-STR / SPEC-GEO: S = sum_n k_n^2 (q_v,n^2 + q_h,n^2), float64 reduction in index order.
inline double specS(double length, std::span<const float> qv, std::span<const float> qh) {
    double s = 0.0;
    for (std::size_t i = 0; i < qv.size(); ++i) {
        const double kn = static_cast<double>(i + 1) * kPi / length;
        s += kn * kn *
             (static_cast<double>(qv[i]) * static_cast<double>(qv[i]) +
              static_cast<double>(qh[i]) * static_cast<double>(qh[i]));
    }
    return s;
}

/// Sine coefficient q_n of a triangle of peak h at xp released at rest, closed form:
/// y0 = sum b_n sin(k_n x), b_n = 2 h L^2 sin(n pi xp / L) / (pi^2 n^2 xp (L - xp)),
/// q_n = b_n sqrt(L/2) (phi_n = sqrt(2/L) sin(k_n x)).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline double trianglePluckMode(double length, double xp, double h, int n) {
    const auto nn = static_cast<double>(n);
    const double b = 2.0 * h * length * length * std::sin(nn * kPi * (xp / length)) /
                     (kPi * kPi * nn * nn * xp * (length - xp));
    return b * std::sqrt(length / 2.0);
}

// ---------------------------------------------------------------------------------------------
// A7-T3 arrival-time estimator (identical on engine and reference signals)
// ---------------------------------------------------------------------------------------------

/// Zero-phase low-pass: Blackman-windowed sinc of half-length `halfSeconds`, unit DC gain, cutoff
/// fc (Hz), applied with zeros before the first sample (the signals are differences from
/// sample 0, so they start at 0) and the last sample held after the end.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline std::vector<double> lowPassZeroPhase(const std::vector<double>& x, double fs, double fc,
                                            double halfSeconds) {
    const auto half = static_cast<std::ptrdiff_t>(std::llround(halfSeconds * fs));
    std::vector<double> taps(static_cast<std::size_t>(2 * half + 1));
    double sum = 0.0;
    for (std::ptrdiff_t i = -half; i <= half; ++i) {
        const auto t = static_cast<double>(i);
        const double a = 2.0 * fc / fs;
        const double sinc = (i == 0) ? a : std::sin(kPi * a * t) / (kPi * t);
        const double u = kPi * t / static_cast<double>(half);
        const double win = 0.42 + 0.5 * std::cos(u) + 0.08 * std::cos(2.0 * u);
        taps[static_cast<std::size_t>(i + half)] = sinc * win;
        sum += sinc * win;
    }
    for (double& t : taps) {
        t /= sum;
    }
    const auto len = static_cast<std::ptrdiff_t>(x.size());
    std::vector<double> y(x.size());
    for (std::ptrdiff_t n = 0; n < len; ++n) {
        double s = 0.0;
        for (std::ptrdiff_t i = -half; i <= half; ++i) {
            const std::ptrdiff_t idx = n + i;
            const double v = (idx < 0) ? 0.0 : x[static_cast<std::size_t>(std::min(idx, len - 1))];
            s += taps[static_cast<std::size_t>(i + half)] * v;
        }
        y[static_cast<std::size_t>(n)] = s;
    }
    return y;
}

struct StepEstimate {
    double arrival{};   ///< s: first crossing of half the plateau height, linearly interpolated
    double height{};    ///< plateau height H (N)
    double ringing{};   ///< max |y - H| / |H| over the plateau window
    double preLevel{};  ///< max |y| / |H| before 0.5 t_pred (the acausal precursor of the filter)
};

/// Arrival of the front in `y` (already filtered, F_L(t) - F_L(0), sample k at k / fs). The
/// plateau height H is the mean of y over [t0 + 0.3 w, t0 + 0.7 w], t0 = tPred, w = the plateau
/// duration (a physical figure the caller derives: 2 x_p / c_L).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
inline StepEstimate estimateArrival(const std::vector<double>& y, double fs, double tPred,
                                    double plateau) {
    StepEstimate e;
    const double lo = tPred + 0.3 * plateau;
    const double hi = tPred + 0.7 * plateau;
    double sum = 0.0;
    int count = 0;
    for (std::size_t k = 0; k < y.size(); ++k) {
        const double t = static_cast<double>(k) / fs;
        if (t >= lo && t <= hi) {
            sum += y[k];
            ++count;
        }
    }
    if (count == 0) {
        e.arrival = -1.0;
        return e;
    }
    e.height = sum / count;
    const double half = 0.5 * e.height;
    e.arrival = -1.0;
    for (std::size_t k = 1; k < y.size(); ++k) {
        if ((y[k] - half) * (e.height >= 0.0 ? 1.0 : -1.0) >= 0.0) {
            const double f = (half - y[k - 1]) / (y[k] - y[k - 1]);
            e.arrival = (static_cast<double>(k - 1) + f) / fs;
            break;
        }
    }
    for (std::size_t k = 0; k < y.size(); ++k) {
        const double t = static_cast<double>(k) / fs;
        if (t >= lo && t <= hi) {
            e.ringing = std::max(e.ringing, std::fabs(y[k] - e.height) / std::fabs(e.height));
        }
        if (t < 0.5 * tPred) {
            e.preLevel = std::max(e.preLevel, std::fabs(y[k]) / std::fabs(e.height));
        }
    }
    return e;
}

}  // namespace pme::oracle::geo
