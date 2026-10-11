// SPDX-License-Identifier: MIT
// P0.5-R4 orthotropic Kirchhoff plate; P0.5-T4 (simply supported rectangular plate modes).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include "pme/analysis/partials.hpp"
#include "pme/ref/plate.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

// Spruce-like soundboard plate (grain along x): E_L = 10 GPa, E_R = 0.5 GPa, G_LR = 0.6 GPa,
// nu_LR = 0.37 (nu_RL = nu_LR E_R / E_L), rho = 400 kg/m^3, h = 3 mm, 0.6 m x 0.4 m.
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

struct Mode {
    double f = 0.0;
    int m = 0;
    int n = 0;
};

std::vector<Mode> lowest_modes(const PlateConstants& c, int count) {
    std::vector<Mode> all;
    for (int m = 1; m <= 10; ++m) {
        for (int n = 1; n <= 14; ++n) {
            all.push_back({plate_frequency_hz(c, m, n), m, n});
        }
    }
    std::sort(all.begin(), all.end(), [](const Mode& a, const Mode& b) { return a.f < b.f; });
    all.resize(static_cast<std::size_t>(count));
    return all;
}

}  // namespace

TEST_CASE("P0.5-R4 plate stiffness matrix has the discrete sine modes as eigenvectors") {
    const PlateConstants c = derive_plate(spruce_plate());
    const int nx = 24;
    const int ny = 18;
    const PlateOperator op = build_plate_operator(c, nx, ny);
    for (const auto& [m, n] : {std::pair{1, 1}, {2, 1}, {1, 3}, {3, 2}, {5, 4}}) {
        std::vector<double> v(op.grid.count);
        for (int j = 1; j < ny; ++j) {
            for (int i = 1; i < nx; ++i) {
                v[op.grid.index(i, j)] = std::sin(m * kPi * i / nx) * std::sin(n * kPi * j / ny);
            }
        }
        std::vector<double> kv;
        op.stiffness.multiply(v, kv);
        const double w = 2.0 * kPi * plate_discrete_frequency_hz(c, nx, ny, m, n);
        double worst = 0.0;
        double scale = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            worst = std::max(worst, std::fabs(kv[i] - w * w * op.mass * v[i]));
            scale = std::max(scale, std::fabs(kv[i]));
        }
        INFO("mode (" << m << "," << n << ") residual " << worst / scale);
        CHECK(worst / scale < 1e-9);  // round-off of the fourth differences of smooth data
    }
}

TEST_CASE("P0.5-T4 plate operator eigenvalues give the ten lowest orthotropic modes within 0.1%") {
    const PlateConstants c = derive_plate(spruce_plate());
    const auto modes = lowest_modes(c, 10);
    // Fine grid for the semi-discrete (space only) operator: the eigenvalues are exact for the
    // discrete sine modes, checked against the stiffness matrix in the previous case.
    const int nx = 240;
    const int ny = 240;
    double worst = 0.0;
    for (const Mode& md : modes) {
        const double fd = plate_discrete_frequency_hz(c, nx, ny, md.m, md.n);
        const double err = std::fabs(fd / md.f - 1.0);
        worst = std::max(worst, err);
        INFO("mode (" << md.m << "," << md.n << ") analytic " << md.f << " Hz discrete " << fd
                      << " Hz error " << err);
        CHECK(err <= 1e-3);
    }
    INFO("worst error " << worst);
    CHECK(worst <= 1e-3);
}

#ifdef PME_REF_OPTIMISED
TEST_CASE("P0.5-T4 plate simulation reproduces the lowest three modes within 0.1%") {
    const pme::PlateParams pp = spruce_plate();
    const PlateConstants c = derive_plate(pp);
    const auto modes = lowest_modes(c, 10);
    Scenario sc;
    sc.name = "plate-modes";
    sc.fs = 12000.0;
    sc.duration = 0.45;
    PlateSpec ps;
    ps.params = pp;
    ps.nx = 90;
    ps.ny = 60;
    ps.has_initial = true;
    ps.initial_x = 0.21;
    ps.initial_y = 0.13;
    ps.initial_amplitude = 1e-3;
    sc.plate = ps;
    ObservationSpec o;
    o.name = "w";
    o.kind = ObservationKind::PlateDisplacement;
    o.point = pme::Point{0.43, 0.27, 0.0};
    sc.observations.push_back(o);
    SimulationOptions so;
    so.record_energy = false;
    const Results r = simulate(sc, so);
    // Fit the ten lowest modes jointly, starting from the fully discrete scheme's frequencies.
    pme::analysis::PartialTrackingOptions opt;
    for (const Mode& md : modes) {
        opt.approx_frequencies_hz.push_back(
            plate_scheme_frequency_hz(c, ps.nx, ps.ny, md.m, md.n, 1.0 / sc.fs, sc.theta));
    }
    opt.envelope_frame_s = 0.0;
    const auto fit = pme::analysis::track_partials(r.signal("w"), sc.fs, opt);
    for (std::size_t i = 0; i < 3; ++i) {
        const Mode& md = modes[i];
        const double fm = fit.partials[i].frequency_hz;
        const double fs_pred = opt.approx_frequencies_hz[i];
        INFO("mode (" << md.m << "," << md.n << ") analytic " << md.f << " measured " << fm
                      << " scheme " << fs_pred);
        CHECK(std::fabs(fm / md.f - 1.0) <= 1e-3);
        CHECK(std::fabs(fm / fs_pred - 1.0) <= 1e-5);  // matches the scheme's own dispersion
    }
}
#endif

TEST_CASE("P0.5-R4 unmodelled plate features are rejected naming the field") {
    pme::PlateParams p = spruce_plate();
    p.outline.push_back(pme::Point{});
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("outline"), RefError);
    p = spruce_plate();
    p.ribs.push_back(pme::RibDesc{});
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("ribs"), RefError);
    p = spruce_plate();
    p.thicknessNx = 2;
    p.thicknessNy = 2;
    p.thicknessGrid = {0.003, 0.003, 0.003, 0.003};
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("thicknessGrid"), RefError);
    p = spruce_plate();
    p.bridges.push_back(pme::BridgeBeamDesc{});
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("bridges"), RefError);
    p = spruce_plate();
    p.rimStiffness = 1.0;
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("rimStiffness"), RefError);
    p = spruce_plate();
    p.grainAngle = 0.1;
    CHECK_THROWS_WITH_AS(derive_plate(p), doctest::Contains("grainAngle"), RefError);
}

TEST_CASE("P0.5-R4 plate with bilinear bridge point conserves energy") {
    Scenario sc;
    sc.name = "plate-energy";
    sc.fs = 4000.0;
    sc.duration = 0.1;
    PlateSpec ps;
    ps.params = spruce_plate();
    ps.nx = 24;
    ps.ny = 16;
    ps.has_initial = true;
    ps.initial_x = 0.21;
    ps.initial_y = 0.13;
    ps.initial_amplitude = 1e-3;
    sc.plate = ps;
    // One string on a bridge point of the plate (vertical axis = the plate, horizontal = rigid).
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
    // Energy flows from the string into the plate.
    CHECK(r.energy.plate.back() > r.energy.plate.front());
    CHECK(r.energy.string.back() < r.energy.string.front());
}
