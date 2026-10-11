// SPDX-License-Identifier: MIT
// Covers: A1-R3, A1-R4, A1-R5
// ModeBank layout and update (A1-R3), derived quantities (A1-R4), discrete energy (A1-R5).
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/test/tolerance.hpp"

namespace {

constexpr double kPi = std::numbers::pi;

bool aligned(const void* p) { return std::bit_cast<std::uintptr_t>(p) % 64U == 0U; }

pme::OscCoeffs compile(double freqHz, double sigma, double fs) {
    pme::OscCoeffs c;
    REQUIRE(pme::compileOscillator(2.0 * kPi * freqHz, sigma, fs, c) == pme::Status::Ok);
    return c;
}

}  // namespace

TEST_CASE("A1-R3 every array is 64-byte aligned and padded to a multiple of 16") {
    for (const std::size_t n : {1U, 15U, 16U, 17U, 64U, 100U}) {
        pme::osc::ModeBank bank;
        bank.resize(n);
        CAPTURE(n);
        CHECK(bank.size() == n);
        CHECK(bank.paddedSize() % 16U == 0U);
        CHECK(bank.paddedSize() >= n);
        CHECK(bank.paddedSize() < n + 16U);
        CHECK(aligned(bank.q()));
        CHECK(aligned(bank.u()));
        CHECK(aligned(bank.beta()));
        CHECK(aligned(bank.kappa()));
        CHECK(aligned(bank.gamma()));
        CHECK(aligned(bank.omegaHat2()));
        CHECK(aligned(bank.sigmaHat()));
    }
    pme::osc::ModeBank empty;
    CHECK(empty.size() == 0U);
    empty.resize(0);
    CHECK(empty.paddedSize() == 0U);
}

TEST_CASE("A1-R3 padding lanes keep zero coefficients and zero state") {
    const std::size_t n = 21;
    pme::osc::ModeBank bank;
    bank.resize(n);
    const pme::OscCoeffs c = compile(440.0, 2.0, 48000.0);
    for (std::size_t i = 0; i < n; ++i) {
        bank.setMode(i, c);
        bank.q()[i] = 1.0F;
    }
    std::vector<float> force(n, 0.5F);
    for (int s = 0; s < 1000; ++s) {
        bank.step(force);
    }
    for (std::size_t i = n; i < bank.paddedSize(); ++i) {
        CHECK(bank.q()[i] == 0.0F);
        CHECK(bank.u()[i] == 0.0F);
        CHECK(bank.beta()[i] == 0.0F);
        CHECK(bank.kappa()[i] == 0.0F);
        CHECK(bank.gamma()[i] == 0.0F);
    }
    bank.clearState();
    for (std::size_t i = 0; i < bank.paddedSize(); ++i) {
        CHECK(bank.q()[i] == 0.0F);
        CHECK(bank.u()[i] == 0.0F);
    }
}

TEST_CASE("A1-R3 float32 and float64 kernels agree for a 27.5 Hz mode over 1e5 steps") {
    const double fs = 48000.0;
    const pme::OscCoeffs c = compile(27.5, 0.0, fs);

    pme::osc::ModeBank bank;
    bank.resize(1);
    bank.setMode(0, c);
    bank.q()[0] = 1.0F;
    // u^0 = q^0 - q^-1 for a free oscillation with amplitude 1 and zero phase velocity offset.
    const double w = 2.0 * kPi * 27.5 / fs;
    bank.u()[0] = static_cast<float>(std::cos(0.0) - std::cos(-w));

    auto q = static_cast<double>(bank.q()[0]);
    auto u = static_cast<double>(bank.u()[0]);
    const float forceF = 0.0F;
    const double forceD = 0.0;
    double worst = 0.0;
    for (int n = 0; n < 100000; ++n) {
        bank.step(std::span<const float>(&forceF, 1));
        // Same (float32) coefficients, float64 state: isolates state rounding.
        pme::osc::stepModes<double, float>(&q, &u, bank.beta(), bank.kappa(), bank.gamma(), &forceD,
                                           1);
        worst = std::fmax(worst, std::fabs(static_cast<double>(bank.q()[0]) - q));
    }
    MESSAGE("worst |q_float32 - q_float64| over 1e5 steps (amplitude 1): " << worst);
    CHECK(worst < 5e-5);
}

TEST_CASE("A1-R3 step stores the previous state when asked and matches the formula") {
    pme::osc::ModeBank bank;
    bank.resize(3);
    for (std::size_t i = 0; i < 3; ++i) {
        bank.setMode(i, compile(100.0 * static_cast<double>(i + 1), 3.0, 48000.0));
        bank.q()[i] = 0.25F * static_cast<float>(i + 1);
        bank.u()[i] = -0.01F;
    }
    const std::vector<float> force{0.1F, -0.2F, 0.3F};
    std::vector<float> qPrev(3);
    std::vector<float> uPrev(3);
    std::array<float, 3> expectQ{};
    std::array<float, 3> expectU{};
    for (std::size_t i = 0; i < 3; ++i) {
        // SPEC-OSC difference form, in the fixed order of the kernel.
        expectU[i] = bank.beta()[i] * bank.u()[i] - bank.kappa()[i] * bank.q()[i] +
                     bank.gamma()[i] * force[i];
        expectQ[i] = bank.q()[i] + expectU[i];
    }
    const float q0 = bank.q()[1];
    const float u0 = bank.u()[1];
    bank.step(force, qPrev, uPrev);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(bank.u()[i] == doctest::Approx(expectU[i]).epsilon(1e-6));
        CHECK(bank.q()[i] == doctest::Approx(expectQ[i]).epsilon(1e-6));
    }
    CHECK(qPrev[1] == q0);
    CHECK(uPrev[1] == u0);
}

namespace {

// Double-precision forced, damped mode (unit modal mass), recording q history.
struct ForcedRun {
    double fs, dt, omegaHat2, sigmaHat;
    std::vector<double> q, u, f;  // q[n], u[n] = q[n] - q[n-1], f[n]
};

ForcedRun runForced(int steps) {
    const double fs = 48000.0;
    pme::osc::OscCoeffsF64 c;
    REQUIRE(pme::osc::compileOscillatorF64(2.0 * kPi * 330.0, 20.0, fs, c) == pme::Status::Ok);
    ForcedRun r{fs, 1.0 / fs, c.omegaHat2, c.sigmaHat, {}, {}, {}};
    double q = 0.0;
    double u = 0.0;
    const double beta = c.beta;
    const double kappa = c.kappa;
    const double gamma = c.gamma;
    r.q.push_back(q);
    r.u.push_back(u);
    for (int n = 0; n < steps; ++n) {
        const double f = 1.0e4 * std::sin(2.0 * kPi * 500.0 * n / fs) + 3.0e3;
        r.f.push_back(f);
        double qq = q;
        double uu = u;
        pme::osc::stepModes<double, double>(&qq, &uu, &beta, &kappa, &gamma, &f, 1);
        q = qq;
        u = uu;
        r.q.push_back(q);
        r.u.push_back(u);
    }
    return r;
}

}  // namespace

TEST_CASE("A1-R4 derived quantities match their finite-difference definitions") {
    const ForcedRun r = runForced(2000);
    const double dt = r.dt;
    for (std::size_t n = 100; n + 1 < r.q.size(); n += 37) {
        // Step n produced u[n+1] from u[n]: q^{n-1} = q[n] - u[n].
        const double qm = r.q[n - 1];
        const double q0 = r.q[n];
        const double qp = r.q[n + 1];
        const double vHalf = pme::osc::halfStepVelocity(r.u[n + 1], dt);
        const double vC = pme::osc::centredVelocity(r.u[n + 1], r.u[n], dt);
        const double aC = pme::osc::centredAcceleration(r.u[n + 1], r.u[n], dt);
        PME_CHECK_REL(vHalf, (qp - q0) / dt, 1e-12);
        PME_CHECK_REL(vC, (qp - qm) / (2.0 * dt), 1e-12);
        PME_CHECK_REL(aC, (qp - 2.0 * q0 + qm) / (dt * dt), 1e-9);
        // The discrete equation: delta_tt q + 2 sigmaHat delta_t. q + omegaHat^2 q = f.
        const double residual = aC + 2.0 * r.sigmaHat * vC + r.omegaHat2 * q0 - r.f[n];
        const double scale = std::fabs(aC) + std::fabs(2.0 * r.sigmaHat * vC) +
                             std::fabs(r.omegaHat2 * q0) + std::fabs(r.f[n]);
        CHECK(std::fabs(residual) <= 1e-12 * scale);
    }
}

TEST_CASE("A1-R5 discrete energy balance holds for a damped, forced mode") {
    const ForcedRun r = runForced(4000);
    const double dt = r.dt;
    double worstRel = 0.0;
    for (std::size_t n = 1; n + 1 < r.q.size(); ++n) {
        // H^{n+1/2} uses q^n, q^{n+1}, u^{n+1}; H^{n-1/2} uses q^{n-1}, q^n, u^n.
        const double hNew = pme::osc::modeEnergy(r.omegaHat2, r.q[n], r.q[n + 1], r.u[n + 1], dt);
        const double hOld = pme::osc::modeEnergy(r.omegaHat2, r.q[n - 1], r.q[n], r.u[n], dt);
        const double balance = pme::osc::energyBalance(r.sigmaHat, r.f[n], r.u[n + 1], r.u[n], dt);
        const double scale = std::fabs(hNew) + std::fabs(hOld);
        const double err = std::fabs((hNew - hOld) - balance);
        worstRel = std::fmax(worstRel, err / scale);
        CHECK(err <= 1e-12 * scale);
    }
    MESSAGE("worst |dH - balance| / (|H_new| + |H_old|): " << worstRel);
}

TEST_CASE("A1-R5 undamped unforced mode conserves the discrete energy") {
    const double fs = 48000.0;
    pme::osc::OscCoeffsF64 c;
    REQUIRE(pme::osc::compileOscillatorF64(2.0 * kPi * 1000.0, 0.0, fs, c) == pme::Status::Ok);
    double q = 1.0;
    double u = 0.0;
    const double f = 0.0;
    const double dt = 1.0 / fs;
    double qPrev = q;
    double h0 = 0.0;
    for (int n = 0; n < 5000; ++n) {
        qPrev = q;
        pme::osc::stepModes<double, double>(&q, &u, &c.beta, &c.kappa, &c.gamma, &f, 1);
        const double h = pme::osc::modeEnergy(c.omegaHat2, qPrev, q, u, dt);
        if (n == 0) {
            h0 = h;
        }
        PME_CHECK_REL(h, h0, 1e-12);
    }
}

TEST_CASE("A1-R5 bank energy sums modes in index order") {
    const double fs = 48000.0;
    const std::size_t n = 20;
    pme::osc::ModeBank bank;
    bank.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        bank.setMode(i,
                     compile(55.0 * static_cast<double>(i + 1), 1.0 + static_cast<double>(i), fs));
        bank.q()[i] = 1.0F / static_cast<float>(i + 1);
    }
    std::vector<float> force(n, 0.25F);
    std::vector<float> qPrev(n);
    std::vector<float> uPrev(n);
    bank.step(force, qPrev, uPrev);
    const double dt = 1.0 / fs;
    double expectE = 0.0;
    double expectB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        expectE +=
            pme::osc::modeEnergy(bank.omegaHat2()[i], qPrev[i], bank.q()[i], bank.u()[i], dt);
        expectB += pme::osc::energyBalance(bank.sigmaHat()[i], force[i], bank.u()[i], uPrev[i], dt);
    }
    CHECK(pme::osc::bankEnergy(bank, qPrev, dt) == expectE);
    CHECK(pme::osc::bankEnergyBalance(bank, force, uPrev, dt) == expectB);
    CHECK(expectE > 0.0);
}
