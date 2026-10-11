// SPDX-License-Identifier: MIT
// Shared helpers of the reference-solver tests.
#pragma once

#include <cmath>

#include "pme/ref/common.hpp"
#include "pme/ref/scenario.hpp"
#include "pme/ref/simulation.hpp"

namespace pme::ref::test {

using pme::StringParams;

// A steel string of length L and target pitch f1 whose inharmonicity B = pi^2 EI / (T L^2) equals
// `target_b` (core diameter found by bisection on SPEC-STR's formulas).
inline StringParams steel_string(double length, double f1, double target_b) {
    StringParams p;
    p.L = length;
    p.rhoCore = 7850.0;
    p.ECore = 2.0e11;
    p.f1Target = f1;
    double lo = 1e-5;
    double hi = 5e-2;
    for (int i = 0; i < 200; ++i) {
        p.dCore = 0.5 * (lo + hi);
        const StringPhysics ph = derive_string(p);
        const double b = kPi * kPi * ph.ei / (ph.tension * length * length);
        (b < target_b ? lo : hi) = p.dCore;
    }
    p.dCore = 0.5 * (lo + hi);
    return p;
}

// Single plain string, pinned ends, released from a triangle at x_p, observed at x_obs.
inline Scenario string_scenario(const StringParams& params, int intervals, double fs,
                                double duration, double x_pluck, double x_obs) {
    Scenario sc;
    sc.name = "test-string";
    sc.fs = fs;
    sc.duration = duration;
    StringSpec s;
    s.params = params;
    s.intervals = intervals;
    s.initial.enabled = true;
    s.initial.amplitude = 1e-3;
    s.initial.position = x_pluck;
    sc.strings.push_back(s);
    ObservationSpec o;
    o.name = "y";
    o.kind = ObservationKind::StringDisplacement;
    o.x = x_obs;
    sc.observations.push_back(o);
    return sc;
}

}  // namespace pme::ref::test
