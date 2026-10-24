// SPDX-License-Identifier: MIT
// P0.5-R6 tension modulation and longitudinal waves (SPEC-GEO); the tension-modulation part of
// P0.5-T2.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "pme/analysis/partials.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

Scenario geometric_scenario(double amplitude, int intervals, double fs, int oversampling,
                            double duration, bool longitudinal) {
    const StringParams p = steel_string(1.0, 110.0, 1e-4);
    Scenario sc = string_scenario(p, intervals, fs, duration, 0.3, 0.37);
    sc.oversampling = oversampling;
    sc.strings[0].initial.amplitude = amplitude;
    sc.strings[0].geometric.tension_modulation = true;
    sc.strings[0].geometric.longitudinal = longitudinal;
    return sc;
}

double fundamental(const std::vector<double>& y, std::size_t begin, std::size_t end, double fs,
                   double guess) {
    const std::vector<double> window(y.begin() + static_cast<std::ptrdiff_t>(begin),
                                     y.begin() + static_cast<std::ptrdiff_t>(end));
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = {guess};
    opt.envelope_frame_s = 0.0;
    return pme::analysis::track_partials(window, fs, opt).partials[0].frequency_hz;
}

}  // namespace

TEST_CASE("P0.5-T2 lossless string with tension modulation conserves energy") {
    Scenario sc = geometric_scenario(5e-3, 60, 48000.0, 1, 0.2, false);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    // The potential V_KC is part of the energy and a large pluck makes it matter.
    double vmax = 0.0;
    for (const double v : r.energy.contact) {
        vmax = std::max(vmax, v);
    }
    CHECK(vmax / r.energy.total[0] > 1e-3);
    CHECK(r.newton.max_iterations <= 10);
}

TEST_CASE("P0.5-R6 large-amplitude pluck conserves energy to 1e-11 per step") {
    Scenario sc = geometric_scenario(2e-2, 80, 48000.0, 2, 0.1, false);
    sc.strings[0].params.b1 = 0.0;
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-11);
}

TEST_CASE("P0.5-R6 pitch glides down as the amplitude decays and scales with amplitude squared") {
    const double fs = 24000.0;
    auto glide = [&](double amplitude) {
        Scenario sc = geometric_scenario(amplitude, 100, fs, 1, 0.7, false);
        sc.strings[0].params.b1 = 2.0;
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        const auto& y = r.signal("y");
        const auto early = static_cast<std::size_t>(0.1 * fs);
        const auto late_begin = static_cast<std::size_t>(0.5 * fs);
        const double f_early = fundamental(y, 0, early, fs, 112.0);
        const double f_late = fundamental(y, late_begin, late_begin + early, fs, 111.0);
        return std::pair{f_early, f_late};
    };
    const auto [e_big, l_big] = glide(5e-3);
    const auto [e_small, l_small] = glide(1e-6);
    const StringPhysics ph = derive_string(steel_string(1.0, 110.0, 1e-4));
    INFO("big: early " << e_big << " late " << l_big << "; small: early " << e_small << " late "
                       << l_small);
    CHECK(e_big > l_big);  // glide direction: the pitch falls as the amplitude decays
    CHECK(std::fabs(e_small - l_small) / e_small < 1e-4);  // linear at small amplitude
    // The linear frequency (SPEC-STR "Modes") is reached at small amplitude within 0.1% (the
    // discrete scheme's own dispersion at this grid is of that order).
    const double f_lin = stiff_string_frequency_hz(ph, 1);
    CHECK(std::fabs(e_small / f_lin - 1.0) <= 1e-3);
    // The early shift is proportional to the squared amplitude: halving the amplitude quarters it.
    const auto [e_half, l_half] = glide(2.5e-3);
    const double shift_big = e_big - e_small;
    const double shift_half = e_half - e_small;
    INFO("shifts " << shift_big << " " << shift_half);
    CHECK(shift_big / shift_half > 3.0);
    CHECK(shift_big / shift_half < 4.4);
}

TEST_CASE("P0.5-R6 longitudinal mode frequencies match the SPEC-GEO linear values") {
    // fs_sim = 960 kHz so that the time-discretisation error of the theta-scheme (omega k)^2 / 12
    // stays below 0.02% for the first three longitudinal modes (the grid adds (K h)^2 / 24). The
    // free modes are isolated by releasing a longitudinal triangle at a tiny transverse amplitude;
    // the forced response is checked in the next case.
    Scenario sc = geometric_scenario(1e-9, 100, 48000.0, 20, 0.02, true);
    sc.strings[0].geometric.has_longitudinal_initial = true;
    sc.strings[0].geometric.longitudinal_initial_amplitude = 1e-5;
    sc.strings[0].geometric.longitudinal_initial_position = 0.37;
    ObservationSpec lx;
    lx.name = "xi";
    lx.kind = ObservationKind::LongitudinalDisplacement;
    lx.x = 0.21;
    sc.observations.push_back(lx);
    SimulationOptions so;
    so.record_energy = false;
    const Results r = simulate(sc, so);
    const StringPhysics ph = derive_string(sc.strings[0].params);
    // SPEC-GEO "Longitudinal modes": omega_m = K_m sqrt(EA / rhoA), K_m = m pi / L.
    const double c = std::sqrt(ph.ea / ph.rho_a);
    pme::analysis::PartialTrackingOptions opt;
    for (int m = 1; m <= 3; ++m) {
        opt.approx_frequencies_hz.push_back(m * c / (2.0 * ph.length));
    }
    opt.envelope_frame_s = 0.0;
    const auto fit = pme::analysis::track_partials(r.signal("xi"), r.sim_rate, opt);
    for (int m = 1; m <= 3; ++m) {
        const double expect = m * c / (2.0 * ph.length);
        const double got = fit.partials[static_cast<std::size_t>(m - 1)].frequency_hz;
        INFO("longitudinal mode " << m << " expected " << expect << " Hz, measured " << got);
        CHECK(std::fabs(got / expect - 1.0) <= 1e-3);
    }
}

TEST_CASE("P0.5-R6 transverse motion drives the longitudinal waves and the bridge force F_L") {
    Scenario sc = geometric_scenario(2e-3, 100, 48000.0, 4, 0.05, true);
    ObservationSpec lx;
    lx.name = "xi";
    lx.kind = ObservationKind::LongitudinalDisplacement;
    lx.x = 0.37;
    sc.observations.push_back(lx);
    ObservationSpec fl;
    fl.name = "fl";
    fl.kind = ObservationKind::LongitudinalBridgeForce;
    sc.observations.push_back(fl);
    SimulationOptions so;
    so.record_energy = false;
    const Results r = simulate(sc, so);
    double xmax = 0.0;
    double fmax = 0.0;
    for (const double v : r.signal("xi")) {
        xmax = std::max(xmax, std::fabs(v));
    }
    for (const double v : r.signal("fl")) {
        fmax = std::max(fmax, std::fabs(v));
    }
    CHECK(xmax > 0.0);
    CHECK(fmax > 0.0);
    // Without transverse motion there is no longitudinal motion.
    Scenario quiet = geometric_scenario(0.0, 100, 48000.0, 4, 0.01, true);
    quiet.observations.push_back(lx);
    const Results q = simulate(quiet, so);
    double qmax = 0.0;
    for (const double v : q.signal("xi")) {
        qmax = std::max(qmax, std::fabs(v));
    }
    CHECK(qmax == 0.0);
}

TEST_CASE("P0.5-R6 geometric scenarios outside the supported set are rejected") {
    Scenario sc = geometric_scenario(5e-3, 60, 48000.0, 1, 0.01, false);
    HammerSpec h;
    h.mass = 0.008;
    h.K = 1e9;
    h.velocity = 1.0;
    h.strings = {0};
    sc.hammers.push_back(h);
    CHECK_THROWS_WITH_AS(simulate(sc), doctest::Contains("single fixed-end string"), RefError);
}

// Reviewer test. Quasi-static limit of SPEC-GEO's longitudinal equation, an independent check of
// the forcing's sign and scale: when the transverse motion is slow compared with the longitudinal
// modes, EA xi_xx = -(EA - T)/2 (y_x^2)_x with xi = 0 at both ends gives
// EA xi_x = -(EA - T)/2 (y_x^2 - <y_x^2>), hence F_L = EA xi_x(L) + (EA - T)/2 y_x(L)^2
// = (EA - T)/2 <y_x^2> = (EA - T) S / (2 L) = Delta T, the Kirchhoff-Carrier tension rise. The
// right-hand side is computed from the energy trace alone: V_KC = (EA - T) S^2 / (8 L) is the
// "contact" part of the energy, so S = sqrt(8 L V / (EA - T)). Averaging F_L over one period of
// the first longitudinal mode removes the free longitudinal oscillation (all its modes have
// frequencies that are multiples of the first).
TEST_CASE(
    "P0.5-R6 reviewer: the window-averaged longitudinal bridge force equals the tension rise") {
    const StringParams p = steel_string(1.0, 55.0, 1e-4);
    Scenario sc = string_scenario(p, 100, 48000.0, 0.03, 0.3, 0.37);
    sc.oversampling = 4;
    sc.strings[0].initial.amplitude = 4e-3;
    sc.strings[0].geometric.tension_modulation = true;
    sc.strings[0].geometric.longitudinal = true;
    ObservationSpec fl;
    fl.name = "fl";
    fl.kind = ObservationKind::LongitudinalBridgeForce;
    sc.observations.push_back(fl);
    const Results r = simulate(sc);
    const StringPhysics ph = derive_string(p);
    const double gamma = ph.ea - ph.tension;
    const double c = std::sqrt(ph.ea / ph.rho_a);
    const double period = 2.0 * ph.length / c;  // first longitudinal mode
    const auto w = static_cast<std::size_t>(std::llround(period * r.sim_rate));
    const auto& f = r.signal("fl");
    REQUIRE(f.size() > 8 * w);
    double worst = 0.0;
    double biggest = 0.0;
    double sum_avg = 0.0;
    double sum_rise = 0.0;
    // Skip the first two windows (start-up of the longitudinal motion).
    for (std::size_t centre = 2 * w; centre + w < f.size(); centre += w / 2) {
        double avg = 0.0;
        double v_avg = 0.0;
        for (std::size_t i = centre - w / 2; i < centre + w / 2; ++i) {
            avg += f[i];
            v_avg += 0.5 * (r.energy.contact[i + 1] + r.energy.contact[i]);
        }
        avg /= static_cast<double>(w);
        v_avg /= static_cast<double>(w);
        const double dt_rise = 0.5 * gamma / ph.length * std::sqrt(8.0 * ph.length * v_avg / gamma);
        biggest = std::max(biggest, dt_rise);
        worst = std::max(worst, std::fabs(avg - dt_rise));
        sum_avg += avg;
        sum_rise += dt_rise;
    }
    INFO("worst |<F_L> - Delta T| = " << worst << " N, largest Delta T = " << biggest << " N");
    CHECK(biggest > 0.0);
    // Scale and sign: the mean over the run agrees to 6% (3.6% observed; a wrong factor or sign
    // would be off by 100% or more). The residual is physical: the moving corners of the triangular
    // release vary faster than the longitudinal period, so the response is not exactly
    // quasi-static, and individual windows follow S(t) less tightly still.
    CHECK(std::fabs(sum_avg / sum_rise - 1.0) < 0.06);
    CHECK(worst / biggest < 0.2);
}
