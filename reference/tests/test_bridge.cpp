// SPDX-License-Identifier: MIT
// P0.5-R3 bridge: lumped mass-spring-dashpot or conductance, shared by several strings, both
// polarisations; P0.5-T2 string + lumped bridge energy case.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "pme/analysis/partials.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

StringParams flexible_string(double length, double f1) {
    StringParams p = steel_string(length, f1, 1e-4);
    p.epsWind = -1.0;  // EI = 0: a perfectly flexible string
    return p;
}

BridgeSpec conductance_bridge(double g) {
    BridgeSpec b;
    b.kind = BridgeModelKind::Conductance;
    b.conductance = g;
    return b;
}

StringSpec bridged_string(const StringParams& p, int intervals, double amplitude, double angle) {
    StringSpec s;
    s.params = p;
    s.intervals = intervals;
    s.bridge = 0;
    s.initial.enabled = true;
    s.initial.amplitude = amplitude;
    s.initial.position = 0.31 * p.L;
    s.initial.angle = angle;
    return s;
}

std::vector<double> decay_rates(const Results& r, const std::string& signal, double f0, int count) {
    pme::analysis::PartialTrackingOptions opt;
    for (int n = 1; n <= count; ++n) {
        opt.approx_frequencies_hz.push_back(n * f0);
    }
    opt.envelope_frame_s = 0.0;
    const auto fit = pme::analysis::track_partials(r.signal(signal), r.sim_rate, opt);
    std::vector<double> sigma;
    for (const auto& p : fit.partials) {
        sigma.push_back(p.sigma_per_s);
    }
    return sigma;
}

}  // namespace

TEST_CASE("P0.5-R3 flexible string on a pure conductance decays at the ORC-06 rate") {
    const StringParams p = flexible_string(1.0, 110.0);
    const StringPhysics ph = derive_string(p);
    const double z0 = std::sqrt(ph.tension * ph.rho_a);
    for (const double z0g : {0.02, 0.1}) {
        const double g = z0g / z0;
        const double sigma_exact = -110.0 * std::log((1.0 - z0g) / (1.0 + z0g));
        Scenario sc;
        sc.name = "conductance";
        sc.fs = 48000.0;
        sc.duration = 1.0;
        sc.bridges.push_back(conductance_bridge(g));
        sc.strings.push_back(bridged_string(p, 200, 1e-3, 0.0));
        ObservationSpec o;
        o.name = "y";
        o.kind = ObservationKind::StringDisplacement;
        o.x = 0.27;
        sc.observations.push_back(o);
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        const auto sigma = decay_rates(r, "y", 110.0, 6);
        for (std::size_t n = 0; n < sigma.size(); ++n) {
            INFO("Z0 G = " << z0g << " partial " << n + 1 << " sigma " << sigma[n] << " exact "
                           << sigma_exact);
            CHECK(std::fabs(sigma[n] / sigma_exact - 1.0) <= 0.01);
        }
    }
}

TEST_CASE(
    "P0.5-R3 two strings on one conductance: in-phase decays fast and anti-phase does not decay") {
    const StringParams p = flexible_string(1.0, 110.0);
    const StringPhysics ph = derive_string(p);
    const double z0 = std::sqrt(ph.tension * ph.rho_a);
    const double z0g = 0.02;
    const double g = z0g / z0;
    for (const double sign : {1.0, -1.0}) {
        Scenario sc;
        sc.name = "unison";
        sc.fs = 48000.0;
        sc.duration = 1.0;
        sc.bridges.push_back(conductance_bridge(g));
        sc.strings.push_back(bridged_string(p, 200, 1e-3, 0.0));
        sc.strings.push_back(bridged_string(p, 200, sign * 1e-3, 0.0));
        ObservationSpec o;
        o.name = "y";
        o.kind = ObservationKind::StringDisplacement;
        o.x = 0.27;
        sc.observations.push_back(o);
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        const auto sigma = decay_rates(r, "y", 110.0, 3);
        // Two strings in phase load the conductance twice: the effective Z0 G doubles.
        const double in_phase = -110.0 * std::log((1.0 - 2.0 * z0g) / (1.0 + 2.0 * z0g));
        for (const double s : sigma) {
            if (sign > 0.0) {
                INFO("in phase sigma " << s << " exact " << in_phase);
                CHECK(std::fabs(s / in_phase - 1.0) <= 0.01);
            } else {
                INFO("anti-phase sigma " << s);
                CHECK(std::fabs(s) < 1e-3 * in_phase);  // no loss through the bridge
            }
        }
    }
}

TEST_CASE("P0.5-R3 horizontal polarisation decays through the horizontal conductance") {
    const StringParams p = flexible_string(1.0, 110.0);
    const StringPhysics ph = derive_string(p);
    const double z0 = std::sqrt(ph.tension * ph.rho_a);
    const double z0g = 0.05;
    BridgeSpec b;
    b.kind = BridgeModelKind::MassSpringDashpot;
    b.stiffness = 1e12;  // effectively rigid vertical axis
    b.desc.conductanceH = z0g / z0;
    for (const double angle : {0.0, kPi / 2.0}) {
        Scenario sc;
        sc.name = "pol";
        sc.fs = 48000.0;
        sc.duration = 0.8;
        sc.bridges.push_back(b);
        sc.strings.push_back(bridged_string(p, 200, 1e-3, angle));
        ObservationSpec o;
        o.name = "y";
        o.kind = ObservationKind::StringDisplacement;
        o.x = 0.27;
        o.polarisation = angle == 0.0 ? 0 : 1;
        sc.observations.push_back(o);
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        const auto sigma = decay_rates(r, "y", 110.0, 3);
        const double exact = -110.0 * std::log((1.0 - z0g) / (1.0 + z0g));
        for (const double s : sigma) {
            if (angle != 0.0) {
                CHECK(std::fabs(s / exact - 1.0) <= 0.01);  // horizontal: loaded by G_h
            } else {
                CHECK(std::fabs(s) < 1e-2 * exact);  // vertical: rigid bridge, no loss
            }
        }
    }
}

TEST_CASE("P0.5-T2 lossless strings on a lumped mass-spring bridge conserve energy") {
    const StringParams p1 = steel_string(1.0, 110.0, 1e-3);
    StringParams p2 = p1;
    p2.f1Target = 110.4;
    Scenario sc;
    sc.name = "msd";
    sc.fs = 48000.0;
    sc.oversampling = 2;
    sc.duration = 0.4;
    BridgeSpec b;
    b.kind = BridgeModelKind::MassSpringDashpot;
    b.mass = 0.05;
    b.stiffness = 2.0e5;  // bridge resonance near 320 Hz
    b.desc.thetaB = 0.3;  // rotated axes: vertical and horizontal polarisations exchange energy
    sc.bridges.push_back(b);
    sc.strings.push_back(bridged_string(p1, 100, 1e-3, 0.4));
    sc.strings.push_back(bridged_string(p2, 100, 1e-3, 0.4));
    ObservationSpec o;
    o.name = "f";
    o.kind = ObservationKind::BridgeForce;
    sc.observations.push_back(o);
    ObservationSpec o2;
    o2.name = "fh";
    o2.kind = ObservationKind::StringDisplacement;
    o2.x = 0.27;
    o2.polarisation = 1;
    sc.observations.push_back(o2);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    // The bridge moves: energy sits in its mass and spring, and the horizontal polarisation is fed.
    double bridge_max = 0.0;
    for (const double e : r.energy.bridge) {
        bridge_max = std::max(bridge_max, e);
    }
    CHECK(bridge_max > 0.0);
    double hmax = 0.0;
    for (const double y : r.signal("fh")) {
        hmax = std::max(hmax, std::fabs(y));
    }
    CHECK(hmax > 0.0);
}

TEST_CASE("P0.5-R3 dashpot and conductance terms are the only dissipation") {
    const StringParams p = steel_string(1.0, 110.0, 1e-3);
    Scenario sc;
    sc.name = "msd-damped";
    sc.fs = 48000.0;
    sc.duration = 0.3;
    BridgeSpec b;
    b.mass = 0.05;
    b.stiffness = 2.0e5;
    b.damping = 5.0;
    b.desc.conductanceH = 0.2;
    b.desc.thetaB = 0.3;
    sc.bridges.push_back(b);
    sc.strings.push_back(bridged_string(p, 100, 1e-3, 0.4));
    const Results r = simulate(sc);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    double dissipated = 0.0;
    for (const double l : r.energy.loss) {
        CHECK(l >= 0.0);
        dissipated += l;
    }
    CHECK(dissipated > 0.0);
    CHECK(r.energy.total.back() <= r.energy.total.front());
}
