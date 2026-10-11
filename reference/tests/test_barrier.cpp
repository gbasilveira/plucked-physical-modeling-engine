// SPDX-License-Identifier: MIT
// P0.5-R5 distributed barrier (penalty potential, Newton solve); the barrier part of P0.5-T2.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "pme/analysis/measures.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

// The ORC-15 setup on the reference: a string released from a triangle onto a flat barrier. With
// side = +1 the barrier is a floor at b below the string; side = -1 mirrors it into a ceiling.
Scenario barrier_scenario(int side, double mu, int intervals = 100) {
    const StringParams p = steel_string(1.0, 110.0, 1e-3);
    Scenario sc;
    sc.name = "string-barrier";
    sc.fs = 48000.0;
    sc.oversampling = 2;
    sc.duration = 0.04;
    StringSpec s;
    s.params = p;
    s.intervals = intervals;
    s.initial.enabled = true;
    s.initial.amplitude = side * 3e-3;
    s.initial.position = 0.3;
    sc.strings.push_back(s);
    BarrierSpec b;
    b.string = 0;
    b.params.side = side;
    b.params.K = 1.0e9;
    b.params.alpha = 2.0;
    b.params.muB = mu;
    const double h = p.L / intervals;
    for (int j = 1; j < intervals; ++j) {
        b.params.x.push_back(j * h);
        b.params.b.push_back(-side * 1e-3);
        b.params.w.push_back(h);
    }
    sc.barriers.push_back(b);
    ObservationSpec f;
    f.name = "force";
    f.kind = ObservationKind::BarrierForce;
    sc.observations.push_back(f);
    ObservationSpec y;
    y.name = "y";
    y.kind = ObservationKind::StringDisplacement;
    y.x = 0.45;
    sc.observations.push_back(y);
    return sc;
}

}  // namespace

TEST_CASE("P0.5-R5 string released onto a flat barrier conserves energy without damping") {
    const Results r = simulate(barrier_scenario(+1, 0.0));
    // ORC-15 bound (1e-9 relative per step) and the tighter P0.5-T2 bound.
    CHECK(max_relative_energy_step(r.energy) <= 1e-9);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    // The barrier is hit: contact force and contact potential appear, and the string stays above
    // the floor to within the penalty penetration.
    double peak = 0.0;
    for (const double f : r.signal("force")) {
        peak = std::max(peak, f);
    }
    CHECK(peak > 0.0);
    double potential = 0.0;
    for (const double e : r.energy.contact) {
        potential = std::max(potential, e);
    }
    CHECK(potential > 0.0);
    CHECK(r.newton.contact_steps > 0);
    double ymin = 0.0;
    for (const double y : r.signal("y")) {
        ymin = std::min(ymin, y);
    }
    CHECK(ymin > -1.5e-3);  // floor at -1 mm
}

TEST_CASE("P0.5-T2 string with barrier (no damping) conserves energy per step") {
    const Results r = simulate(barrier_scenario(+1, 0.0, 60));
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
}

TEST_CASE("P0.5-R5 contact damping makes the energy non-increasing") {
    const Results r = simulate(barrier_scenario(+1, 50.0));
    const auto rep = pme::analysis::account_energy(r.energy.total, 1e-9);
    CHECK(rep.non_increasing);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    CHECK(r.energy.total.back() < r.energy.total.front());
    double dissipated = 0.0;
    double least = 0.0;
    for (const double l : r.energy.loss) {
        least = std::min(least, l);
        dissipated += l;
    }
    CHECK(least >= 0.0);  // dissipation is never negative
    CHECK(dissipated > 0.0);
}

TEST_CASE("P0.5-R5 the two barrier sides give mirror-image motion") {
    const Results up = simulate(barrier_scenario(+1, 0.0));
    const Results down = simulate(barrier_scenario(-1, 0.0));
    const auto& a = up.signal("y");
    const auto& b = down.signal("y");
    double worst = 0.0;
    double scale = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::fabs(a[i] + b[i]));
        scale = std::max(scale, std::fabs(a[i]));
    }
    CHECK(worst / scale <= 1e-12);
}
