// SPDX-License-Identifier: MIT
// Covers: A3-R3, A3-T1
// The Woodbury solve against a dense LU solve on random systems of the SPEC-NL structure.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "test_support.hpp"

namespace {

// Dense solve of A x = b by LU with partial pivoting (written independently of the engine).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): matrix then right-hand side
std::vector<double> denseSolve(std::vector<double> A, std::vector<double> b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t piv = col;
        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(A[row * n + col]) > std::abs(A[piv * n + col])) {
                piv = row;
            }
        }
        if (piv != col) {
            for (std::size_t k = 0; k < n; ++k) {
                std::swap(A[piv * n + k], A[col * n + k]);
            }
            std::swap(b[piv], b[col]);
        }
        for (std::size_t row = col + 1; row < n; ++row) {
            const double f = A[row * n + col] / A[col * n + col];
            for (std::size_t k = col; k < n; ++k) {
                A[row * n + k] -= f * A[col * n + k];
            }
            b[row] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            s -= A[i * n + k] * x[k];
        }
        x[i] = s / A[i * n + i];
    }
    return x;
}

}  // namespace

TEST_CASE("A3-T1 Woodbury result equals a dense solve within 1e-12 on random systems") {
    pme::nl::test::Rng rng(20261011U);
    double worst = 0.0;
    for (int sys = 0; sys < 1000; ++sys) {
        const std::size_t n = 1 + rng.index(256);
        const std::size_t r = 1 + rng.index(4);
        // Columns have the SPEC-NL structure: h_j = diag(gamma/m) d_j * s_j, w_j = d_j * t_j with
        // positive scalings, so that W^T H has non-negative eigenvalues. Scales are chosen so that
        // w^T h is O(1) (hammer-like systems), which keeps I + H W^T well conditioned.
        std::vector<double> gm(n);
        for (double& x : gm) {
            x = rng.uniform(0.1, 10.0);
        }
        std::vector<std::vector<double>> h(r, std::vector<double>(n));
        std::vector<std::vector<double>> w(r, std::vector<double>(n));
        for (std::size_t j = 0; j < r; ++j) {
            std::vector<double> d(n);
            double norm2 = 0.0;
            for (double& x : d) {
                x = rng.uniform(-1.0, 1.0);
                norm2 += x * x;
            }
            const double scale = rng.uniform(0.05, 5.0) / (norm2 + 1e-3);
            for (std::size_t i = 0; i < n; ++i) {
                h[j][i] = gm[i] * d[i];
                w[j][i] = d[i] * scale;
            }
        }
        std::vector<double> a(n);
        for (double& x : a) {
            x = rng.uniform(-3.0, 3.0);
        }
        std::vector<const double*> hp(r);
        std::vector<const double*> wp(r);
        for (std::size_t j = 0; j < r; ++j) {
            hp[j] = h[j].data();
            wp[j] = w[j].data();
        }
        std::vector<double> zW(n);
        pme::nl::lowRankSolve(zW.data(), a.data(), n, r, hp.data(), wp.data());

        // Dense: (I + sum_j h_j w_j^T) z = a.
        std::vector<double> A(n * n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            A[i * n + i] = 1.0;
            for (std::size_t k = 0; k < n; ++k) {
                for (std::size_t j = 0; j < r; ++j) {
                    A[i * n + k] += h[j][i] * w[j][k];
                }
            }
        }
        const std::vector<double> zD = denseSolve(A, a, n);
        double num = 0.0;
        double den = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            num += (zW[i] - zD[i]) * (zW[i] - zD[i]);
            den += zD[i] * zD[i];
        }
        const double rel = std::sqrt(num / den);
        worst = std::max(worst, rel);
        REQUIRE(rel <= 1e-12);
    }
    MESSAGE("A3-T1 worst relative error over 1000 systems: " << worst);
}

TEST_CASE("A3-R3 r = 0 copies and aliasing z with a is allowed") {
    std::vector<double> a{1.0, 2.0, 3.0};
    std::vector<double> z(3);
    pme::nl::lowRankSolve(z.data(), a.data(), 3, 0, nullptr, nullptr);
    CHECK(z == a);
    const std::vector<double> h{0.5, 0.5, 0.5};
    const std::vector<double> w{1.0, 1.0, 1.0};
    const std::array<const double*, 1> hp{h.data()};
    const std::array<const double*, 1> wp{w.data()};
    std::vector<double> alias = a;
    pme::nl::lowRankSolve(alias.data(), alias.data(), 3, 1, hp.data(), wp.data());
    pme::nl::lowRankSolve(z.data(), a.data(), 3, 1, hp.data(), wp.data());
    CHECK(alias == z);
    // r = 1 closed form: a - h (w.a) / (1 + w.h) with w.a = 6, w.h = 1.5.
    CHECK(z[0] == doctest::Approx(1.0 - 0.5 * 6.0 / 2.5));
}

namespace {

// V = K/4 (c^T z)^4 on 4 coordinates, for the full-system reference below.
class RefQuartic final : public pme::nl::Potential<double> {
public:
    static constexpr std::size_t kN = 4;
    std::array<double, kN> c{};
    double k{0.0};
    [[nodiscard]] double proj(std::span<const double> z) const noexcept {
        double s = 0.0;
        for (std::size_t i = 0; i < kN; ++i) {
            s += c[i] * z[i];
        }
        return s;
    }
    [[nodiscard]] double value(std::span<const double> z) const noexcept override {
        const double s = proj(z);
        return 0.25 * k * s * s * s * s;
    }
    void gradient(std::span<const double> z, std::span<double> g) const noexcept override {
        const double s = proj(z);
        for (std::size_t i = 0; i < kN; ++i) {
            g[i] = k * s * s * s * c[i];
        }
    }
};

}  // namespace

TEST_CASE(
    "A3-T1 savStep equals a full N x N solve of the SPEC-NL step with 4 coordinates and 2 "
    "dashpots") {
    using namespace pme::nl;
    constexpr std::size_t N = RefQuartic::kN;
    constexpr double fs = 48000.0;
    constexpr double dt = 1.0 / fs;
    pme::nl::test::Rng rng(97U);
    double worst = 0.0;
    for (int trial = 0; trial < 10; ++trial) {
        SavSegment<double> seg;
        REQUIRE(seg.configure(3, 1, 2, fs) == pme::Status::Ok);
        const std::array<double, N> m{0.5, 1.3, 0.8, 0.01};
        std::array<double, N> beta{}, kap{}, gam{};
        for (std::size_t i = 0; i < 3; ++i) {
            pme::osc::OscCoeffsF64 c;
            REQUIRE(pme::osc::compileOscillatorF64(
                        2.0 * 3.141592653589793 * (200.0 + 300.0 * static_cast<double>(i)),
                        5.0 + static_cast<double>(i), fs, c) == pme::Status::Ok);
            REQUIRE(seg.setCoordinate(i, c, m[i]) == pme::Status::Ok);
            beta[i] = c.beta;
            kap[i] = c.kappa;
            gam[i] = c.gamma;
        }
        REQUIRE(seg.setLumped(3, m[3]) == pme::Status::Ok);
        beta[3] = 1.0;
        kap[3] = 0.0;
        gam[3] = dt * dt;
        RefQuartic pot;
        for (double& x : pot.c) {
            x = rng.uniform(-1.0, 1.0);
        }
        pot.k = 1.0e4;
        std::array<double, N> q{}, u{};
        for (std::size_t i = 0; i < N; ++i) {
            q[i] = rng.uniform(-1.0, 1.0) * 1e-2;
            u[i] = rng.uniform(-1.0, 1.0) * 1e-4;
            seg.q()[i] = q[i];
            seg.u()[i] = u[i];
        }
        const std::array<const Potential<double>*, 1> pots{&pot};
        seg.initialisePsi(pots);
        double psi = seg.psi();
        std::array<std::array<double, N>, 2> cd{};
        for (auto& a : cd) {
            for (double& x : a) {
                x = rng.uniform(-1.0, 1.0);
            }
        }
        std::array<Dashpot<double>, 2> d;
        for (std::size_t j = 0; j < 2; ++j) {
            d[j].c = std::span<const double>(cd[j]);
            d[j].R = 0.3 * static_cast<double>(j + 1);
        }
        for (int step = 0; step < 200; ++step) {
            std::array<double, N> F{};
            for (double& x : F) {
                x = rng.uniform(-1.0, 1.0) * 0.1;
            }
            // Full-system reference: (I + G (g g^T/4 + sum R c c^T/(2 dt))) z = free + G (F - g psi
            // + K z^{n-1}).
            const double den = std::sqrt(2.0 * (pot.value(q) + seg.params().eps0));
            std::array<double, N> g{};
            pot.gradient(q, g);
            for (double& x : g) {
                x /= den;
            }
            std::array<double, N> zp{};
            for (std::size_t i = 0; i < N; ++i) {
                zp[i] = q[i] - u[i];
            }
            std::array<std::array<double, N>, N> kmat{};
            for (std::size_t i = 0; i < N; ++i) {
                for (std::size_t k2 = 0; k2 < N; ++k2) {
                    double coef = g[i] * 0.25 * g[k2];
                    for (std::size_t j = 0; j < 2; ++j) {
                        coef += d[j].R * cd[j][i] * cd[j][k2] / (2.0 * dt);
                    }
                    kmat[i][k2] = coef;
                }
            }
            std::vector<double> A(N * N);
            std::vector<double> b(N);
            for (std::size_t i = 0; i < N; ++i) {
                double rhs =
                    q[i] + beta[i] * u[i] - kap[i] * q[i] + gam[i] / m[i] * (F[i] - g[i] * psi);
                for (std::size_t k2 = 0; k2 < N; ++k2) {
                    A[i * N + k2] = (i == k2 ? 1.0 : 0.0) + gam[i] / m[i] * kmat[i][k2];
                    rhs += gam[i] / m[i] * kmat[i][k2] * zp[k2];
                }
                b[i] = rhs;
            }
            const std::vector<double> z = denseSolve(A, b, N);
            double dp = 0.0;
            for (std::size_t i = 0; i < N; ++i) {
                dp += g[i] * (z[i] - zp[i]);
            }
            psi += 0.5 * dp;

            savStep<double>(seg, pots, d, F);
            double num = 0.0;
            double dn = 0.0;
            for (std::size_t i = 0; i < N; ++i) {
                num += std::abs(seg.q()[i] - z[i]) + std::abs(seg.u()[i] - (z[i] - q[i]));
                dn += std::abs(z[i]) + std::abs(z[i] - q[i]);
            }
            worst = std::max(worst, std::max(num / dn, std::abs(seg.psi() - psi) / psi));
            for (std::size_t i = 0; i < N; ++i) {  // resync to the engine state each step
                q[i] = seg.q()[i];
                u[i] = seg.u()[i];
            }
            psi = seg.psi();
        }
    }
    MESSAGE("A3-T1 savStep vs full dense step, worst relative difference: " << worst);
    CHECK(worst <= 1e-11);
}
