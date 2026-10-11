// SPDX-License-Identifier: MIT
// Covers: A4-T2
// A mass-like bridge raises the partials by Delta omega_n = 2 f0 Z0 / (omega_n m) = -2 f0 Z0 Im Y
// (SPEC-BRG "Small-admittance results"; Y = 1/(i omega m) under the e^{+i omega t} convention of
// SPEC-CONV, SI-0012). The shift is measured as the difference between the coupled and the rigid
// discrete system, both from long lossless double-precision runs.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <vector>

#include "pme/analysis/partials.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::bridge_test;
constexpr double kPi = std::numbers::pi;

std::vector<double> runSignal(const pme::StringModes& m, const pme::bridge::ParallelModel& model,
                              std::size_t samples) {
    Rig<double> rig;
    REQUIRE(rig.addSegment(m, pme::BridgeEnd::AtL) == pme::Status::Ok);
    REQUIRE(rig.point.configure(pme::BridgePointDesc{}, model, kFs) == pme::Status::Ok);
    // Pluck-like start: triangular shape released at rest, q_n proportional to sin(n pi xp /
    // L)/n^2.
    const double xp = 0.18;
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const auto n = static_cast<double>(i + 1);
        rig.segs[0].qV()[i] = 1.0e-3 * std::sin(n * kPi * xp) / (n * n);
    }
    const double x0 = 0.123 * m.L;  // observation point
    std::vector<double> shape(m.k.size());
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        shape[i] = std::sqrt(2.0 / m.L) * std::sin(m.k[i] * x0);
    }
    std::vector<double> y(samples);
    for (std::size_t n = 0; n < samples; ++n) {
        rig.step();
        double s = 0.0;
        for (std::size_t i = 0; i < shape.size(); ++i) {
            s += shape[i] * rig.segs[0].qV()[i];
        }
        y[n] = s;
    }
    return y;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): signal, seeds
std::vector<double> track(const std::vector<double>& y, const std::vector<double>& seedsHz) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = seedsHz;
    opt.envelope_frame_s = 0.0;
    const auto res = pme::analysis::track_partials(y, kFs, opt);
    std::vector<double> f;
    f.reserve(res.partials.size());
    for (const auto& p : res.partials) {
        f.push_back(p.frequency_hz);
    }
    return f;
}

/// Root of cot(x) = mu x in (n pi, n pi + pi/2): ideal string with an end mass (SPEC-BRG
/// cross-check; P0.5-R3).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): n, mu
double endMassRoot(int n, double mu) {
    double lo = n * kPi + 1e-12;
    double hi = n * kPi + kPi / 2.0 - 1e-12;
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        // g(x) = cot(x) - mu x decreases from +inf to -mu x on the interval
        const double g = std::cos(mid) / std::sin(mid) - mu * mid;
        (g > 0.0 ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

TEST_CASE("A4-T2 a mass bridge raises the partials by 2 f0 Z0 / (omega_n m) within 2 percent") {
    // A long thin plain string keeps the inharmonicity B n^2 below 1e-3 for n <= 10: the
    // small-admittance formula is derived for the ideal string (EI = 0), and bending stiffness
    // changes the string's end impedance by O(B n^2) (3 percent at n = 10 for d = 1 mm, L = 0.65
    // m).
    pme::StringParams params = plainSteel(1.0, 262.0, true);
    params.dCore = params.dOuter = 0.5e-3;
    const pme::StringModes m = compile(params, 16000.0);
    REQUIRE(m.k.size() >= 10);
    const double mass = 0.1;  // kg; Z0 / (omega_1 m) is checked below to be < 0.01
    pme::bridge::ParallelModel massModel;
    REQUIRE(pme::bridge::massSpringDamperModel(mass, 0.0, 0.0, massModel) == pme::Status::Ok);
    pme::bridge::ParallelModel rigidModel;
    REQUIRE(pme::bridge::conductanceModel(0.0, rigidModel) == pme::Status::Ok);

    const double z0 = std::sqrt(m.tension * m.rhoA);
    const double f0 = std::sqrt(m.tension / m.rhoA) / (2.0 * m.L);
    const int count = 10;
    std::vector<double> predictedHz;  // Delta f_n = Delta omega_n / (2 pi)
    std::vector<double> rigidSeed;
    for (int i = 0; i < count; ++i) {
        const double omega = m.omega[static_cast<std::size_t>(i)];
        const double ratio = z0 / (omega * mass);  // Z0 |Y|
        CHECK(ratio < 0.01);
        const double dOmega = 2.0 * f0 * z0 / (omega * mass);  // -2 f0 Z0 Im Y, Im Y = -1/(omega m)
        predictedHz.push_back(dOmega / (2.0 * kPi));
        rigidSeed.push_back(omega / (2.0 * kPi));
    }

    const auto samples = static_cast<std::size_t>(20.0 * kFs);
    const std::vector<double> fRigid = track(runSignal(m, rigidModel, samples), rigidSeed);
    std::vector<double> coupledSeed = rigidSeed;
    for (int i = 0; i < count; ++i) {
        coupledSeed[static_cast<std::size_t>(i)] += predictedHz[static_cast<std::size_t>(i)];
    }
    const std::vector<double> fCoupled = track(runSignal(m, massModel, samples), coupledSeed);
    REQUIRE(fRigid.size() == static_cast<std::size_t>(count));
    REQUIRE(fCoupled.size() == static_cast<std::size_t>(count));

    const double mu = mass / (m.rhoA * m.L);
    std::cout << "A4-T2 m=" << mass << " kg Z0=" << z0 << " kg/s f0=" << f0 << " Hz\n"
              << "  n  rigid_err_Hz  predicted_Hz  measured_Hz  ratio  exact_end_mass_Hz\n";
    for (int i = 0; i < count; ++i) {
        const auto u = static_cast<std::size_t>(i);
        const double rigidErr = fRigid[u] - m.omega[u] / (2.0 * kPi);
        const double measured = fCoupled[u] - fRigid[u];
        // The estimator must resolve the shift: rigid-bridge error below 0.2 percent of it.
        CHECK(std::abs(rigidErr) < 0.002 * predictedHz[u]);
        const double ratio = measured / predictedHz[u];
        CHECK(std::abs(ratio - 1.0) <= 0.02);
        // exact ideal-string end-mass equation cot(kL) = m k / rhoA, cross-check
        const double x = endMassRoot(i + 1, mu);
        const double c = std::sqrt(m.tension / m.rhoA);
        const double exactHz = (x - (i + 1) * kPi) * c / (2.0 * kPi * m.L);
        CHECK(std::abs(exactHz / predictedHz[u] - 1.0) <= 0.02);
        std::cout << "  " << i + 1 << "  " << sci(rigidErr) << "  " << sci(predictedHz[u], 6)
                  << "  " << sci(measured, 6) << "  " << ratio << "  " << sci(exactHz, 6) << "\n";
    }
}
