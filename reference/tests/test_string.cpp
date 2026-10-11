// SPDX-License-Identifier: MIT
// P0.5-R1 stiff damped string; P0.5-T1 (frequency convergence) and the string part of P0.5-T2.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "pme/analysis/partials.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/simulation.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

std::vector<double> measure_partials(const std::vector<double>& signal, double fs,
                                     const std::vector<double>& approx) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = approx;
    opt.envelope_frame_s = 0.0;
    const auto res = pme::analysis::track_partials(signal, fs, opt);
    std::vector<double> f;
    f.reserve(res.partials.size());
    for (const auto& p : res.partials) {
        f.push_back(p.frequency_hz);
    }
    return f;
}

void run_t1(double target_b) {
    const StringParams params = steel_string(1.0, 110.0, target_b);
    const StringPhysics ph = derive_string(params);
    const double b = kPi * kPi * ph.ei / (ph.tension * ph.length * ph.length);
    CAPTURE(b);
    CHECK(b == doctest::Approx(target_b).epsilon(1e-6));
    const int partials = 5;
    std::vector<double> analytic;
    for (int n = 1; n <= partials; ++n) {
        analytic.push_back(stiff_string_frequency_hz(ph, n));
    }
    const int grids[] = {32, 64, 128, 256};
    std::vector<std::vector<double>> err(4);
    for (int gi = 0; gi < 4; ++gi) {
        const int n_int = grids[gi];
        // Delta t is refined with the grid (k proportional to h) so that both error terms are
        // O(h^2).
        const double fs = 8000.0 * n_int / 32.0;
        Scenario sc = string_scenario(params, n_int, fs, 0.5, 0.31, 0.27);
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        const double k = 1.0 / fs;
        // Starting points of the fit: the analytic values, shifted by the coarse grid's error.
        std::vector<double> start;
        for (int n = 1; n <= partials; ++n) {
            start.push_back(string_scheme_frequency_hz(ph, n_int, n, k, sc.theta));
        }
        const auto f = measure_partials(r.signal("y"), fs, start);
        for (int n = 1; n <= partials; ++n) {
            const double fm = f[static_cast<std::size_t>(n - 1)];
            // Cross-check against the scheme's exact discrete dispersion relation.
            const double fd = string_scheme_frequency_hz(ph, n_int, n, k, sc.theta);
            INFO("grid " << n_int << " partial " << n << " measured " << fm << " scheme " << fd
                         << " analytic " << analytic[static_cast<std::size_t>(n - 1)]);
            CHECK(std::fabs(fm / fd - 1.0) < 1e-7);
            err[static_cast<std::size_t>(gi)].push_back(
                std::fabs(fm / analytic[static_cast<std::size_t>(n - 1)] - 1.0));
        }
    }
    for (int n = 0; n < partials; ++n) {
        const auto idx = static_cast<std::size_t>(n);
        for (std::size_t gi = 1; gi < 4; ++gi) {
            CHECK(err[gi][idx] < err[gi - 1][idx]);  // error decreases monotonically
        }
        const double order = std::log2(err[2][idx] / err[3][idx]);
        INFO("partial " << n + 1 << " observed order " << order);
        CHECK(order > 1.8);  // the theta-scheme with k ~ h is second order: within 10% of 2
        CHECK(order < 2.2);
    }
}

}  // namespace

TEST_CASE("P0.5-T1 string frequencies converge at second order B 1e-4") { run_t1(1e-4); }

TEST_CASE("P0.5-T1 string frequencies converge at second order B 1e-2") { run_t1(1e-2); }

TEST_CASE("P0.5-T2 lossless string conserves energy") {
    const StringParams params = steel_string(1.0, 110.0, 1e-3);
    Scenario sc = string_scenario(params, 120, 48000.0, 0.25, 0.2, 0.3);
    const Results r = simulate(sc);
    CHECK(max_relative_energy_step(r.energy) <= 1e-12);
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-12);
    // Kinetic plus elastic energy of the released triangle is positive and stays put.
    CHECK(r.energy.total.front() > 0.0);
}

TEST_CASE("P0.5-R1 clamped string is stiffer than a pinned string") {
    StringParams p = steel_string(1.0, 110.0, 1e-2);
    Scenario pinned = string_scenario(p, 100, 48000.0, 0.5, 0.3, 0.27);
    p.boundary = pme::Boundary::Clamped;
    Scenario clamped = string_scenario(p, 100, 48000.0, 0.5, 0.3, 0.27);
    SimulationOptions so;
    so.record_energy = true;
    const Results a = simulate(pinned, so);
    const Results b = simulate(clamped, so);
    const StringPhysics ph = derive_string(p);
    const auto fa = measure_partials(a.signal("y"), 48000.0, {110.0});
    const auto fb = measure_partials(b.signal("y"), 48000.0, {110.0});
    // Fletcher: f1 clamped ~ f1 pinned (1 + 2 sqrt(B)/pi + 4 B/pi^2).
    const double bb = kPi * kPi * ph.ei / (ph.tension * ph.length * ph.length);
    const double fletcher = 1.0 + 2.0 * std::sqrt(bb) / kPi + 4.0 * bb / (kPi * kPi);
    CHECK(fb[0] / fa[0] == doctest::Approx(fletcher).epsilon(2e-3));
    CHECK(max_relative_energy_step(b.energy) <= 1e-12);
}

TEST_CASE("P0.5-R1 loss mapping gives sigma = b1 + b3 omega^2") {
    StringParams p = steel_string(1.0, 110.0, 1e-3);
    p.b1 = 0.8;
    p.b3 = 4e-6;
    const StringPhysics ph = derive_string(p);
    Scenario sc = string_scenario(p, 200, 96000.0, 2.0, 0.3, 0.27);
    const Results r = simulate(sc);
    std::vector<double> approx;
    for (int n = 1; n <= 3; ++n) {
        approx.push_back(stiff_string_frequency_hz(ph, n));
    }
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = approx;
    opt.envelope_frame_s = 0.0;
    const auto res = pme::analysis::track_partials(r.signal("y"), 96000.0, opt);
    for (int n = 1; n <= 3; ++n) {
        const double w = 2.0 * kPi * stiff_string_frequency_hz(ph, n);
        const double sigma = p.b1 + p.b3 * w * w;
        CHECK(res.partials[static_cast<std::size_t>(n - 1)].sigma_per_s ==
              doctest::Approx(sigma).epsilon(0.01));
    }
    // The dissipated energy accounts for the decay exactly (energy balance).
    CHECK(max_relative_energy_imbalance(r.energy) <= 1e-11);
    CHECK(r.energy.total.back() < r.energy.total.front());
}

TEST_CASE("P0.5-R1 b2 loss and unmodelled fields are rejected with a clear error") {
    StringParams p = steel_string(1.0, 110.0, 1e-3);
    p.b2 = 0.01;
    CHECK_THROWS_WITH_AS(derive_string(p), doctest::Contains("b2"), RefError);
    p.b2 = 0.0;
    p.sigmaOverride = {1.0};
    CHECK_THROWS_WITH_AS(derive_string(p), doctest::Contains("sigmaOverride"), RefError);
}
