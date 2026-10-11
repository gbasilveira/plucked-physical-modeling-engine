// SPDX-License-Identifier: MIT
// P0.5-R2 hammer contact; P0.5-T3 (hammer against a rigid wall) and the hammer part of P0.5-T2.
#include <doctest/doctest.h>

#include <cmath>

#include "pme/analysis/measures.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

// SPEC-HAM "Oracle": contact time of a mass m hitting a rigid felt contact K [delta]_+^p at v.
double oracle_contact_time(double m, double v, double k_felt, double p) {
    const double dm = std::pow((p + 1.0) * m * v * v / (2.0 * k_felt), 1.0 / (p + 1.0));
    const double e = 1.0 / (p + 1.0);
    return 2.0 * dm / v * std::sqrt(kPi) * std::tgamma(1.0 + e) / std::tgamma(0.5 + e);
}

Scenario wall_scenario(double m, double v, double k_felt, double p, double dt, double duration) {
    Scenario sc;
    sc.name = "hammer-wall";
    sc.fs = 1.0 / dt;
    sc.duration = duration;
    HammerSpec h;
    h.mass = m;
    h.K = k_felt;
    h.p = p;
    h.velocity = v;
    h.wall = true;
    sc.hammers.push_back(h);
    ObservationSpec pen;
    pen.name = "penetration";
    pen.kind = ObservationKind::HammerPenetration;
    sc.observations.push_back(pen);
    ObservationSpec pos;
    pos.name = "position";
    pos.kind = ObservationKind::HammerPosition;
    sc.observations.push_back(pos);
    return sc;
}

}  // namespace

// Felt stiffnesses in the range of piano hammers (Chaigne and Askenfelt 1994 report
// K = 4.5e9 N/m^2.5 for p = 2.5 around middle C; bass hammers are softer, treble hammers stiffer).
TEST_CASE("P0.5-T3 hammer against a rigid wall matches the contact-time oracle") {
    const double m = 0.008;
    const double v = 3.0;
    const double p = 2.5;
    for (const double k_felt : {1.0e9, 4.5e9, 2.0e10}) {
        const double tc = oracle_contact_time(m, v, k_felt, p);
        const double dt = tc / 500.0;  // fine: the error is O(dt^2), 100 times below the 0.1% bound
        Scenario sc = wall_scenario(m, v, k_felt, p, dt, 3.0 * tc);
        const Results r = simulate(sc);
        const auto c = pme::analysis::measure_contact(r.signal("penetration"), 1.0 / dt, 0.0);
        REQUIRE(c.found);
        const double rel = c.duration_s / tc - 1.0;
        INFO("K = " << k_felt << " oracle t_c = " << tc << " measured = " << c.duration_s
                    << " relative error = " << rel);
        CHECK(std::fabs(rel) <= 1e-3);
        // Rebound speed: -v (no dissipation), from the last two positions.
        const auto& y = r.signal("position");
        const double vout = (y[y.size() - 1] - y[y.size() - 2]) / dt;
        INFO("rebound speed " << vout);
        CHECK(std::fabs(vout / (-v) - 1.0) <= 1e-9);
        CHECK(max_relative_energy_step(r.energy) <= 1e-12);
        CHECK(r.newton.max_relative_residual <= 1e-12);
    }
}

TEST_CASE("P0.5-T3 contact time converges at second order in the time step") {
    const double m = 0.008;
    const double v = 3.0;
    const double p = 2.5;
    const double k_felt = 4.5e9;
    const double tc = oracle_contact_time(m, v, k_felt, p);
    double prev = 0.0;
    for (const double steps : {100.0, 200.0, 400.0}) {
        const double dt = tc / steps;
        const Results r = simulate(wall_scenario(m, v, k_felt, p, dt, 3.0 * tc));
        const auto c = pme::analysis::measure_contact(r.signal("penetration"), 1.0 / dt, 0.0);
        const double err = std::fabs(c.duration_s / tc - 1.0);
        if (prev > 0.0) {
            const double order = std::log2(prev / err);
            INFO("steps " << steps << " error " << err << " order " << order);
            CHECK(order > 1.7);
        }
        prev = err;
    }
}

TEST_CASE("P0.5-T2 string with hammer (no Hunt-Crossley) conserves energy") {
    const StringParams params = steel_string(1.0, 110.0, 1e-3);
    Scenario sc;
    sc.name = "string-hammer";
    sc.fs = 48000.0;
    sc.oversampling = 2;
    sc.duration = 0.05;
    StringSpec s;
    s.params = params;
    s.intervals = 100;
    sc.strings.push_back(s);
    HammerSpec h;
    h.mass = 0.008;
    h.K = 4.5e9;
    h.p = 2.5;
    h.velocity = 2.0;
    h.width = 0.01;
    h.strike_x = 0.125;
    h.strings = {0};
    sc.hammers.push_back(h);
    ObservationSpec f;
    f.name = "force";
    f.kind = ObservationKind::HammerForce;
    sc.observations.push_back(f);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    double peak = 0.0;
    for (const double x : r.signal("force")) {
        peak = std::max(peak, x);
    }
    CHECK(peak > 1.0);  // the hammer does hit the string
    // The hammer ends up moving away: kinetic energy left is positive, total conserved.
    CHECK(r.energy.hammer.back() > 0.0);
    CHECK(r.newton.max_relative_residual <= 1e-12);
}

TEST_CASE("P0.5-R2 Hunt-Crossley term dissipates and the energy balance accounts for it") {
    const StringParams params = steel_string(1.0, 110.0, 1e-3);
    Scenario sc;
    sc.name = "string-hammer-hc";
    sc.fs = 48000.0;
    sc.oversampling = 2;
    sc.duration = 0.05;
    StringSpec s;
    s.params = params;
    s.intervals = 100;
    sc.strings.push_back(s);
    HammerSpec h;
    h.mass = 0.008;
    h.K = 4.5e9;
    h.p = 2.5;
    h.mu = 1.0;
    h.velocity = 2.0;
    h.width = 0.01;
    h.strike_x = 0.125;
    h.strings = {0};
    sc.hammers.push_back(h);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-11);
    double dissipated = 0.0;
    for (const double l : r.energy.loss) {
        CHECK(l >= 0.0);
        dissipated += l;
    }
    CHECK(dissipated > 0.0);
    CHECK(r.energy.total.back() < r.energy.total.front());
}
