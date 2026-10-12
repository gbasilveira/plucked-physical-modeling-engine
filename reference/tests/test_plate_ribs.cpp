// SPDX-License-Identifier: MIT
// P0.5-R4 follow-up P0.5-ribs: axis-parallel ribs as Euler-Bernoulli beams on the reference plate
// and the modal solve plate_modes() (consumer: B2-T4). See docs/dev/reference-solver.md, "Ribs".
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "pme/ref/plate.hpp"
#include "pme/ref/scenario.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

// The P0.5-T4 spruce-like plate (grain along x): 0.6 m x 0.4 m, h = 3 mm.
pme::PlateParams spruce_plate() {
    pme::PlateParams p;
    p.lengthX = 0.6;
    p.lengthY = 0.4;
    p.thickness = 0.003;
    p.youngsL = 1.0e10;
    p.youngsR = 0.5e9;
    p.shearLR = 0.6e9;
    p.poissonLR = 0.37;
    p.poissonRL = 0.37 * 0.5e9 / 1.0e10;
    p.density = 400.0;
    return p;
}

// The convergence cases run on three grids with ratio 2. Optimised builds use the B2-T4
// configuration (96 x 64 finest, 20 modes, 6 Levy roots per family); debug builds a smaller one (64 x 48) so
// that every case stays within 60 s.
#ifdef PME_REF_OPTIMISED
constexpr int kFineNx = 96;
constexpr int kFineNy = 64;
constexpr std::size_t kModes = 20;
constexpr std::size_t kRoots = 6;
#else
constexpr int kFineNx = 64;
constexpr int kFineNy = 48;
constexpr std::size_t kModes = 10;
constexpr std::size_t kRoots = 3;
#endif

constexpr double kSpruceE = 1.0e10;
constexpr double kSpruceRho = 400.0;

// A rib parallel to y at x0 from y0 to y1, section 20 mm x 15 mm (height normal to the plate),
// material scaled from spruce by `scale`.
pme::RibDesc rib_y(double x0, double y0, double y1, double scale = 1.0) {
    pme::RibDesc r;
    r.start = pme::Point{x0, y0, 0.0};
    r.end = pme::Point{x0, y1, 0.0};
    r.width = 0.02;
    r.height = 0.015;
    r.youngs = kSpruceE * scale;
    r.density = kSpruceRho * scale;
    return r;
}

pme::RibDesc rib_x(double y0, double x0, double x1, double scale = 1.0) {
    pme::RibDesc r = rib_y(y0, x0, x1, scale);
    r.start = pme::Point{x0, y0, 0.0};
    r.end = pme::Point{x1, y0, 0.0};
    return r;
}

double rib_ei(const pme::RibDesc& r) {
    return r.youngs * r.width * r.height * r.height * r.height / 12.0;
}
double rib_rho_a(const pme::RibDesc& r) { return r.density * r.width * r.height; }

struct Mode {
    double f = 0.0;
    int m = 0;
    int n = 0;
};

// All discrete simply supported modes of the unribbed plate, ascending.
std::vector<Mode> discrete_modes(const PlateConstants& c, int nx, int ny) {
    std::vector<Mode> all;
    for (int m = 1; m < nx; ++m) {
        for (int n = 1; n < ny; ++n) {
            all.push_back({plate_discrete_frequency_hz(c, nx, ny, m, n), m, n});
        }
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const Mode& a, const Mode& b) { return a.f < b.f; });
    return all;
}

struct Refined {
    std::vector<double> extrapolated;
    std::vector<double> order;
};

// Richardson extrapolation of three grids with ratio 2 (second order): f* = f3 + (f3 - f2)/3.
Refined refine3(const std::vector<double>& f1, const std::vector<double>& f2,
                const std::vector<double>& f3) {
    Refined out;
    for (std::size_t i = 0; i < f1.size(); ++i) {
        out.order.push_back(std::log2((f1[i] - f2[i]) / (f2[i] - f3[i])));
        out.extrapolated.push_back(f3[i] + (f3[i] - f2[i]) / 3.0);
    }
    return out;
}

// Sample points for mode tracking: a lattice off every symmetry line of the plate.
std::vector<std::pair<double, double>> tracking_points(const PlateConstants& c) {
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 6; ++j) {
            pts.emplace_back(c.lx * ((i + 0.5) / 8.0 + 0.013), c.ly * ((j + 0.5) / 6.0 + 0.011));
        }
    }
    return pts;
}

// Frequencies of `other` that belong to the first `k` modes of `fine`, matched by the modal
// assurance criterion MAC = (a.b)^2 / (a.a b.b) of the shapes sampled at the tracking points. A
// sorted-index comparison fails where two modes cross between grids (their dispersion differs).
std::vector<double> track_frequencies(const PlateModes& fine, const PlateModes& other,
                                      std::size_t k) {
    std::vector<double> out;
    std::vector<bool> used(other.values.size(), false);
    for (std::size_t i = 0; i < k; ++i) {
        double best = -1.0;
        std::size_t arg = 0;
        for (std::size_t j = 0; j < other.values.size(); ++j) {
            double ab = 0.0;
            double aa = 0.0;
            double bb = 0.0;
            for (std::size_t q = 0; q < fine.values[i].size(); ++q) {
                ab += fine.values[i][q] * other.values[j][q];
                aa += fine.values[i][q] * fine.values[i][q];
                bb += other.values[j][q] * other.values[j][q];
            }
            const double mac = ab * ab / (aa * bb);
            if (mac > best) {
                best = mac;
                arg = j;
            }
        }
        INFO("fine mode " << i + 1 << " best MAC " << best << " with mode " << arg + 1);
        REQUIRE(best > 0.9);
        REQUIRE(!used[arg]);  // two modes mapped onto one: the tracking is ambiguous
        used[arg] = true;
        out.push_back(other.frequencies_hz[arg]);
    }
    return out;
}

// ---- exact continuum solution: Levy-type plate with one full-width rib parallel to y ---------

using Mat4 = std::array<double, 16>;

Mat4 mul4(const Mat4& a, const Mat4& b) {
    Mat4 c{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 4; ++k) {
                s += a[i * 4 + k] * b[k * 4 + j];
            }
            c[i * 4 + j] = s;
        }
    }
    return c;
}

// exp(a) by scaling and squaring with a Taylor series.
Mat4 expm4(const Mat4& a) {
    double nrm = 0.0;
    for (const double v : a) {
        nrm = std::max(nrm, std::fabs(v));
    }
    int squarings = 0;
    while (nrm * 4.0 > 0.5) {
        nrm *= 0.5;
        ++squarings;
    }
    Mat4 scaled = a;
    for (double& v : scaled) {
        v /= std::pow(2.0, squarings);
    }
    Mat4 term{};
    Mat4 sum{};
    for (std::size_t i = 0; i < 4; ++i) {
        term[i * 5] = 1.0;
        sum[i * 5] = 1.0;
    }
    for (int k = 1; k <= 20; ++k) {
        term = mul4(term, scaled);
        for (std::size_t i = 0; i < 16; ++i) {
            term[i] /= k;
            sum[i] += term[i];
        }
    }
    for (int s = 0; s < squarings; ++s) {
        sum = mul4(sum, sum);
    }
    return sum;
}

std::array<double, 4> apply4(const Mat4& m, const std::array<double, 4>& v) {
    std::array<double, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t k = 0; k < 4; ++k) {
            out[i] += m[i * 4 + k] * v[k];
        }
    }
    return out;
}

// Characteristic function of w = X(x) sin(beta y), beta = n pi / Ly, rib at x0 with bending
// stiffness ei and mass per length rho_a. Left end X = X'' = 0 is shot with X'(0) and X'''(0);
//   D11 X'''' - 2 (D12 + 2 D66) beta^2 X'' + (D22 beta^4 - rho h omega^2) X = 0,
//   continuity of X, X', X'' at x0 and D11 [X'''] = -(EI beta^4 - rho_a omega^2) X(x0),
// and the right end X = X'' = 0 gives a 2 x 2 determinant whose zeros are the frequencies.
double levy_characteristic(const PlateConstants& c, int n, double x0, double ei, double rho_a,
                           double f_hz) {
    const double w = 2.0 * kPi * f_hz;
    const double beta = n * kPi / c.ly;
    const double b2 = beta * beta;
    const double q = c.d22 * b2 * b2 - c.rho_h() * w * w;
    Mat4 a{};
    a[1] = 1.0;
    a[6] = 1.0;
    a[11] = 1.0;
    a[12] = -q / c.d11;
    a[14] = 2.0 * (c.d12 + 2.0 * c.d66) * b2 / c.d11;
    Mat4 left = a;
    Mat4 right = a;
    for (double& v : left) {
        v *= x0;
    }
    for (double& v : right) {
        v *= (c.lx - x0);
    }
    const Mat4 e1 = expm4(left);
    const Mat4 e2 = expm4(right);
    const double jump = -(ei * b2 * b2 - rho_a * w * w) / c.d11;
    std::array<std::array<double, 4>, 2> cols{};
    cols[0] = {0.0, 1.0, 0.0, 0.0};
    cols[1] = {0.0, 0.0, 0.0, 1.0};
    std::array<std::array<double, 2>, 2> r{};  // r[col][row]
    for (std::size_t k = 0; k < 2; ++k) {
        std::array<double, 4> y = apply4(e1, cols[k]);
        y[3] += jump * y[0];
        y = apply4(e2, y);
        r[k][0] = y[0];
        r[k][1] = y[2];
    }
    return r[0][0] * r[1][1] - r[1][0] * r[0][1];
}

// All frequencies of mode family n below f_max (Hz): sign changes of the characteristic function
// on a fine scan, refined by bisection to 1e-12 relative.
std::vector<double> levy_roots(const PlateConstants& c, int n, double x0, double ei, double rho_a,
                               double f_max) {
    std::vector<double> roots;
    const double df = 0.05;
    double fa = 1.0;
    double va = levy_characteristic(c, n, x0, ei, rho_a, fa);
    for (double fb = fa + df; fb <= f_max; fb += df) {
        const double vb = levy_characteristic(c, n, x0, ei, rho_a, fb);
        if ((va < 0.0) != (vb < 0.0)) {
            double lo = fa;
            double hi = fb;
            double vlo = va;
            for (int it = 0; it < 200 && (hi - lo) > 1e-12 * hi; ++it) {
                const double mid = 0.5 * (lo + hi);
                const double vm = levy_characteristic(c, n, x0, ei, rho_a, mid);
                if ((vm < 0.0) == (vlo < 0.0)) {
                    lo = mid;
                    vlo = vm;
                } else {
                    hi = mid;
                }
            }
            roots.push_back(0.5 * (lo + hi));
        }
        fa = fb;
        va = vb;
    }
    return roots;
}

}  // namespace

TEST_CASE("P0.5-R4 plate without ribs keeps one uniform nodal mass") {
    const PlateConstants c = derive_plate(spruce_plate());
    const PlateOperator op = build_plate_operator(c, 24, 16);
    REQUIRE(op.node_mass.size() == op.grid.count);
    for (const double m : op.node_mass) {
        CHECK(m == op.mass);  // bit-identical to the scalar path
    }
}

TEST_CASE("P0.5-R4 modal solve reproduces the unribbed discrete plate") {
    const PlateConstants c = derive_plate(spruce_plate());
    for (const auto& [nx, ny] : {std::pair{24, 16}, std::pair{30, 20}}) {
        const auto ref = discrete_modes(c, nx, ny);
        const PlateModes modes = plate_modes(c, {}, nx, ny, 30);
        REQUIRE(modes.frequencies_hz.size() == 30);
        double worst = 0.0;
        for (std::size_t i = 0; i < 30; ++i) {
            worst = std::max(worst, std::fabs(modes.frequencies_hz[i] / ref[i].f - 1.0));
        }
        INFO("grid " << nx << "x" << ny << " worst relative error " << worst << ", iterations "
                     << modes.iterations);
        CHECK(worst <= 1e-10);
        CHECK(modes.max_residual <= 1e-9);
    }
}

TEST_CASE("P0.5-R4 modal solve is deterministic and returns mass-normalised shapes") {
    const PlateConstants c = derive_plate(spruce_plate());
    const std::vector<pme::RibDesc> ribs = {rib_y(0.15, 0.0, 0.4)};
    const std::vector<std::pair<double, double>> pts = {{0.21, 0.13}, {0.43, 0.27}};
    const PlateModes a = plate_modes(c, ribs, 24, 16, 6, pts);
    const PlateModes b = plate_modes(c, ribs, 24, 16, 6, pts);
    CHECK(a.frequencies_hz == b.frequencies_hz);
    CHECK(a.values == b.values);
    REQUIRE(a.values.size() == 6);
    CHECK(a.values[0].size() == 2);
    // The lowest mode of this plate has one sign: positive by the sign convention.
    CHECK(a.values[0][0] > 0.0);
    CHECK(a.values[0][1] > 0.0);
    // phi^T M phi = 1: with the rib's mass included, sum m_i phi_i^2 = 1 is checked through the
    // unribbed discrete sine mode, whose normalisation is known: phi = sin sin * 2/sqrt(M_total).
    const PlateModes plain = plate_modes(c, {}, 24, 16, 1, {{0.3, 0.2}});
    const double m_node = c.rho_h() * (c.lx / 24) * (c.ly / 16);
    const double expect =
        std::sin(kPi * 12.0 / 24) * std::sin(kPi * 8.0 / 16) * 2.0 / std::sqrt(m_node * 24 * 16);
    CHECK(plain.values[0][0] == doctest::Approx(expect).epsilon(1e-9));
}

TEST_CASE("P0.5-R4 ribs with zero stiffness and mass leave the plate unchanged") {
    const PlateConstants c = derive_plate(spruce_plate());
    const double tiny = 1e-20;
    const std::vector<pme::RibDesc> ribs = {rib_y(0.15, 0.0, 0.4, tiny), rib_y(0.3, 0.0, 0.4, tiny),
                                            rib_y(0.45, 0.0, 0.4, tiny)};
    const auto ref = discrete_modes(c, 24, 16);
    const PlateModes modes = plate_modes(c, ribs, 24, 16, 20);
    double worst = 0.0;
    for (std::size_t i = 0; i < 20; ++i) {
        worst = std::max(worst, std::fabs(modes.frequencies_hz[i] / ref[i].f - 1.0));
    }
    INFO("worst relative error " << worst);
    CHECK(worst <= 1e-10);
}

TEST_CASE("P0.5-R4 a rib on a nodal line leaves those modes unchanged") {
    const PlateConstants c = derive_plate(spruce_plate());
    const int nx = 24;
    const int ny = 16;
    const std::vector<pme::RibDesc> ribs = {rib_y(0.3, 0.0, 0.4)};
    const int count = 60;
    const PlateModes ribbed = plate_modes(c, ribs, nx, ny, count);
    const auto plain = discrete_modes(c, nx, ny);
    const double f_top = ribbed.frequencies_hz.back();
    int even_checked = 0;
    int odd_moved = 0;
    double worst_even = 0.0;
    for (const Mode& md : plain) {
        if (md.f >= f_top) {
            break;
        }
        double nearest = 1e300;
        for (const double f : ribbed.frequencies_hz) {
            nearest = std::min(nearest, std::fabs(f / md.f - 1.0));
        }
        if (md.m % 2 == 0) {
            worst_even = std::max(worst_even, nearest);
            ++even_checked;
        } else if (nearest > 1e-3) {
            ++odd_moved;
        }
    }
    INFO("even-m modes checked " << even_checked << ", worst relative deviation " << worst_even
                                 << ", odd-m modes that moved by > 0.1%: " << odd_moved);
    CHECK(even_checked >= 20);
    CHECK(worst_even <= 1e-9);
    CHECK(odd_moved >= 10);
}

TEST_CASE("P0.5-R4 a weak rib shifts frequencies by the first-order perturbation") {
    const PlateConstants c = derive_plate(spruce_plate());
    const int nx = 24;
    const int ny = 16;
    const double eps = 1e-4;
    const double x0 = 0.3;  // node 12
    const pme::RibDesc unit = rib_y(x0, 0.0, 0.4);
    const pme::RibDesc weak = rib_y(x0, 0.0, 0.4, eps);
    const auto plain = discrete_modes(c, nx, ny);
    // Non-degenerate modes: a gap of at least 2% to both neighbours.
    std::vector<std::size_t> chosen;
    for (std::size_t i = 0; i < 30 && chosen.size() < 10; ++i) {
        const double lo = i > 0 ? plain[i].f / plain[i - 1].f - 1.0 : 1.0;
        const double hi = plain[i + 1].f / plain[i].f - 1.0;
        if (lo > 0.02 && hi > 0.02) {
            chosen.push_back(i);
        }
    }
    REQUIRE(chosen.size() == 10);
    const PlateModes shifted = plate_modes(c, {weak}, nx, ny, 30);
    const PlateModes base = plate_modes(c, {}, nx, ny, 30);
    const double hx = c.lx / nx;
    const double hy = c.ly / ny;
    int nonzero = 0;
    for (const std::size_t i : chosen) {
        const Mode& md = plain[i];
        const double w2_0 = std::pow(2.0 * kPi * base.frequencies_hz[i], 2);
        const double w2_e = std::pow(2.0 * kPi * shifted.frequencies_hz[i], 2);
        const double measured = (w2_e - w2_0) / eps;
        // Discrete sine mode phi: rib energy sum over the line nodes, rib mass likewise.
        const double s = std::sin(md.m * kPi * x0 / c.lx);
        const double ay = 4.0 / (hy * hy) * std::pow(std::sin(md.n * kPi / (2.0 * ny)), 2);
        const double w2 = std::pow(2.0 * kPi * md.f, 2);
        // phi^T K_rib phi = EI hy ay^2 s^2 ny/2, phi^T M_rib phi = rhoA hy s^2 ny/2,
        // phi^T M phi = rho h hx hy (nx/2)(ny/2).
        const double predicted =
            2.0 * s * s * (rib_ei(unit) * ay * ay - w2 * rib_rho_a(unit)) / (c.rho_h() * hx * nx);
        INFO("mode (" << md.m << "," << md.n << ") measured " << measured << " predicted "
                      << predicted);
        CHECK(std::fabs(measured - predicted) <= 0.01 * std::fabs(predicted) + 1e-7 * w2);
        if (std::fabs(predicted) > 1e-3 * w2) {
            ++nonzero;
        }
    }
    CHECK(nonzero >= 3);
}

TEST_CASE("P0.5-R4 ribbed plate frequencies converge at second order") {
    const PlateConstants c = derive_plate(spruce_plate());
    const std::vector<pme::RibDesc> ribs = {rib_y(0.15, 0.0, 0.4), rib_y(0.3, 0.0, 0.4),
                                            rib_y(0.45, 0.0, 0.4)};
    const auto pts = tracking_points(c);
    const std::size_t k = kModes;
    // The coarse grids get extra modes so that every fine mode finds its partner.
    const PlateModes fine = plate_modes(c, ribs, kFineNx, kFineNy, static_cast<int>(k), pts);
    const PlateModes mid =
        plate_modes(c, ribs, kFineNx / 2, kFineNy / 2, static_cast<int>(k) + 8, pts);
    const PlateModes coarse =
        plate_modes(c, ribs, kFineNx / 4, kFineNy / 4, static_cast<int>(k) + 8, pts);
    MESSAGE("[ribbed] iterations " << coarse.iterations << " / " << mid.iterations << " / "
                                   << fine.iterations << ", residuals " << coarse.max_residual
                                   << " / " << mid.max_residual << " / " << fine.max_residual);
    const std::vector<double> f1 = track_frequencies(fine, coarse, k);
    const std::vector<double> f2 = track_frequencies(fine, mid, k);
    const std::vector<double> f3(fine.frequencies_hz.begin(),
                                 fine.frequencies_hz.begin() + static_cast<long>(k));
    const Refined r = refine3(f1, f2, f3);
    for (std::size_t i = 0; i < k; ++i) {
        MESSAGE("[ribbed] mode " << i + 1 << ": " << f1[i] << " " << f2[i] << " " << f3[i] << " -> "
                                 << r.extrapolated[i] << " Hz, order " << r.order[i]);
        INFO("mode " << i + 1 << " observed order " << r.order[i]);
        CHECK(std::fabs(r.order[i] - 2.0) <= 0.2);  // within 10% of 2
    }
}

TEST_CASE("P0.5-R4 ribbed Levy plate matches the exact solution") {
    const PlateConstants c = derive_plate(spruce_plate());
    const double x0 = 0.3;
    const pme::RibDesc rib = rib_y(x0, 0.0, 0.4);
    const double ei = rib_ei(rib);
    const double rho_a = rib_rho_a(rib);

    // Sign of the jump condition: a heavy rib without stiffness lowers every frequency.
    {
        const double heavy = 9.6;  // kg/m: 10x the plate strip
        const auto plain = levy_roots(c, 1, x0, 0.0, 0.0, 300.0);
        const auto loaded = levy_roots(c, 1, x0, 0.0, heavy, 300.0);
        REQUIRE(plain.size() >= 3);
        REQUIRE(loaded.size() >= 3);
        CHECK(plain[0] == doctest::Approx(plate_frequency_hz(c, 1, 1)).epsilon(1e-9));
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(loaded[i] < plain[i]);
        }
        CHECK(loaded[0] < 0.95 * plain[0]);
    }
    // No rib: the exact solution reproduces the analytic simply supported modes.
    {
        const auto plain = levy_roots(c, 2, x0, 0.0, 0.0, 300.0);
        REQUIRE(plain.size() >= 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(plain[i] ==
                  doctest::Approx(plate_frequency_hz(c, static_cast<int>(i) + 1, 2)).epsilon(1e-9));
        }
    }

    // All Levy frequencies below the larger of the 6th roots of n = 1 and n = 2.
    const auto r1 = levy_roots(c, 1, x0, ei, rho_a, 900.0);
    const auto r2 = levy_roots(c, 2, x0, ei, rho_a, 900.0);
    REQUIRE(r1.size() >= kRoots);
    REQUIRE(r2.size() >= kRoots);
    const double f_scan = std::max(r1[kRoots - 1], r2[kRoots - 1]) * 1.0001;
    std::vector<double> exact;
    for (int n = 1; n <= 14; ++n) {
        for (const double f : levy_roots(c, n, x0, ei, rho_a, f_scan)) {
            exact.push_back(f);
        }
    }
    std::sort(exact.begin(), exact.end());
    const std::size_t k = exact.size();
    MESSAGE("[levy] " << k << " exact frequencies below " << f_scan << " Hz");

    const std::vector<pme::RibDesc> ribs = {rib};
    const auto pts = tracking_points(c);
    const int kk = static_cast<int>(k);
    const PlateModes fine = plate_modes(c, ribs, kFineNx, kFineNy, kk, pts);
    const PlateModes mid = plate_modes(c, ribs, kFineNx / 2, kFineNy / 2, kk + 8, pts);
    const PlateModes coarse = plate_modes(c, ribs, kFineNx / 4, kFineNy / 4, kk + 8, pts);
    const std::vector<double> f1 = track_frequencies(fine, coarse, k);
    const std::vector<double> f2 = track_frequencies(fine, mid, k);
    const std::vector<double> f3(fine.frequencies_hz.begin(),
                                 fine.frequencies_hz.begin() + static_cast<long>(k));
    const Refined r = refine3(f1, f2, f3);
    // Tracked modes keep the fine grid's order, which can differ from the exact order where two
    // frequencies lie closer than the discretisation error: compare the sorted sets.
    std::vector<double> extrapolated = r.extrapolated;
    std::sort(extrapolated.begin(), extrapolated.end());
    double worst = 0.0;
    for (std::size_t i = 0; i < k; ++i) {
        const double err = std::fabs(extrapolated[i] / exact[i] - 1.0);
        worst = std::max(worst, err);
        MESSAGE("[levy] mode " << i + 1 << " exact " << exact[i] << " extrapolated "
                               << extrapolated[i] << " error " << err);
        INFO("mode " << i + 1 << " exact " << exact[i] << " extrapolated " << extrapolated[i]);
        CHECK(err <= 1e-3);
    }
    INFO("worst error " << worst);
    CHECK(worst <= 1e-3);
}

TEST_CASE("P0.5-T2 lossless energy is conserved with a ribbed plate") {
    Scenario sc;
    sc.name = "ribbed-plate-energy";
    sc.fs = 4000.0;
    sc.duration = 0.1;
    PlateSpec ps;
    ps.params = spruce_plate();
    // Two crossing ribs: one full width parallel to y, one partial parallel to x.
    ps.params.ribs.push_back(rib_y(0.3, 0.0, 0.4));
    ps.params.ribs.push_back(rib_x(0.2, 0.15, 0.45));
    ps.nx = 24;
    ps.ny = 16;
    ps.has_initial = true;
    ps.initial_x = 0.21;
    ps.initial_y = 0.13;
    ps.initial_amplitude = 1e-3;
    sc.plate = ps;
    BridgeSpec br;
    br.desc.position = pme::Point{0.31, 0.19, 0.0};
    sc.bridges.push_back(br);
    StringSpec s;
    s.params = steel_string(0.7, 196.0, 1e-3);
    s.intervals = 60;
    s.bridge = 0;
    s.initial.enabled = true;
    s.initial.amplitude = 1e-3;
    s.initial.position = 0.15;
    sc.strings.push_back(s);
    ObservationSpec o;
    o.name = "w";
    o.kind = ObservationKind::PlateDisplacement;
    o.point = pme::Point{0.43, 0.27, 0.0};
    sc.observations.push_back(o);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(r.energy.plate.back() > r.energy.plate.front());
    CHECK(r.energy.string.back() < r.energy.string.front());
}

TEST_CASE("P0.5-R4 ribs input validation") {
    const PlateConstants c = derive_plate(spruce_plate());
    auto with = [](const pme::RibDesc& r) {
        pme::PlateParams p = spruce_plate();
        p.ribs.push_back(r);
        return p;
    };
    // Diagonal rib.
    pme::RibDesc diag = rib_y(0.3, 0.0, 0.4);
    diag.end.x = 0.4;
    CHECK_THROWS_WITH_AS(derive_plate(with(diag)), doctest::Contains("ribs[0]"), RefError);
    CHECK_THROWS_WITH_AS(derive_plate(with(diag)), doctest::Contains("axis-parallel"), RefError);
    // z != 0.
    pme::RibDesc raised = rib_y(0.3, 0.0, 0.4);
    raised.start.z = 0.001;
    CHECK_THROWS_WITH_AS(derive_plate(with(raised)), doctest::Contains("ribs[0]"), RefError);
    // End point outside the plate.
    CHECK_THROWS_WITH_AS(derive_plate(with(rib_y(0.3, 0.0, 0.41))), doctest::Contains("ribs[0]"),
                         RefError);
    CHECK_THROWS_WITH_AS(derive_plate(with(rib_y(0.7, 0.0, 0.4))), doctest::Contains("ribs[0]"),
                         RefError);
    // Zero length.
    CHECK_THROWS_WITH_AS(derive_plate(with(rib_y(0.3, 0.2, 0.2))), doctest::Contains("ribs[0]"),
                         RefError);
    // Non-positive section or material, each field named.
    for (const char* field : {"width", "height", "youngs", "density"}) {
        pme::RibDesc bad = rib_y(0.3, 0.0, 0.4);
        if (std::string(field) == "width") {
            bad.width = 0.0;
        } else if (std::string(field) == "height") {
            bad.height = -0.01;
        } else if (std::string(field) == "youngs") {
            bad.youngs = 0.0;
        } else {
            bad.density = -1.0;
        }
        CHECK_THROWS_WITH_AS(derive_plate(with(bad)), doctest::Contains("ribs[0]"), RefError);
        CHECK_THROWS_WITH_AS(derive_plate(with(bad)), doctest::Contains(field), RefError);
    }
    // The second rib is the bad one.
    {
        pme::PlateParams p = spruce_plate();
        p.ribs.push_back(rib_y(0.3, 0.0, 0.4));
        p.ribs.push_back(diag);
        CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("ribs[1]"), RefError);
    }
    // Off-grid line and off-grid end point (24 x 16 grid: hx = 0.025, hy = 0.025).
    CHECK_THROWS_WITH_AS(build_plate_operator(c, {rib_y(0.31, 0.0, 0.4)}, 24, 16),
                         doctest::Contains("ribs[0]"), RefError);
    CHECK_THROWS_WITH_AS(build_plate_operator(c, {rib_y(0.31, 0.0, 0.4)}, 24, 16),
                         doctest::Contains("grid"), RefError);
    CHECK_THROWS_WITH_AS(build_plate_operator(c, {rib_y(0.3, 0.0, 0.3125)}, 24, 16),
                         doctest::Contains("ribs[0]"), RefError);
    CHECK_THROWS_WITH_AS(
        build_plate_operator(c, {rib_y(0.3, 0.0, 0.4), rib_x(0.19, 0.0, 0.6)}, 24, 16),
        doctest::Contains("ribs[1]"), RefError);
    // On a grid containing the lines the same ribs are accepted, with ends inside the plate too.
    CHECK_NOTHROW(build_plate_operator(c, {rib_y(0.3, 0.1, 0.3), rib_x(0.2, 0.0, 0.6)}, 24, 16));
}

TEST_CASE("P0.5-R4 ribs are read from the plate JSON") {
    const std::string text = R"({
      "length_x": 0.6, "length_y": 0.4, "thickness": 0.003,
      "youngs_l": 1e10, "youngs_r": 5e8, "shear_lr": 6e8,
      "poisson_lr": 0.37, "poisson_rl": 0.0185, "density": 400,
      "grid": {"nx": 24, "ny": 16},
      "ribs": [ {"start": {"x": 0.3, "y": 0.0}, "end": {"x": 0.3, "y": 0.4},
                 "width": 0.02, "height": 0.015, "youngs": 1e10, "density": 400} ]
    })";
    const PlateSpec spec = parse_plate_spec(text);
    REQUIRE(spec.params.ribs.size() == 1);
    CHECK(spec.params.ribs[0].end.y == doctest::Approx(0.4));
    CHECK(spec.params.ribs[0].height == doctest::Approx(0.015));
    CHECK(spec.nx == 24);
    // Unknown key inside a rib names the rib.
    const std::string bad = R"({"length_x": 0.6, "length_y": 0.4, "thickness": 0.003,
      "youngs_l": 1e10, "youngs_r": 5e8, "shear_lr": 6e8, "poisson_lr": 0.37, "poisson_rl": 0.0185,
      "density": 400,
      "ribs": [ {"start": {"x": 0.3, "y": 0.0}, "end": {"x": 0.3, "y": 0.4}, "width": 0.02,
                 "height": 0.015, "youngs": 1e10, "density": 400, "colour": 1} ]})";
    CHECK_THROWS_WITH_AS(parse_plate_spec(bad), doctest::Contains("ribs[0]"), RefError);
}
