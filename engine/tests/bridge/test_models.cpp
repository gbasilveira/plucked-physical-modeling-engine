// SPDX-License-Identifier: MIT
// Covers: A4-R2, A4-R5
// The three parallel-axis bridge models (conductance, mass-spring-damper, modal set) and the
// external-motion and extra-force hooks of BridgePoint (SPEC-BODY "Local bridge model",
// "Sympathetic path"; SPEC-GEO "Transmission to the body").
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

#include "pme/body.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/osc/oscillator.hpp"

namespace {

using pme::Status;
using pme::bridge::BridgePoint;
using pme::bridge::ParallelModel;

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): theta_b, G_h
BridgePoint<double> makePoint(const ParallelModel& model, double thetaB = 0.0, double gH = 0.0) {
    BridgePoint<double> p;
    pme::BridgePointDesc d;
    d.thetaB = thetaB;
    d.conductanceH = gH;
    REQUIRE(p.configure(d, model, kFs) == Status::Ok);
    return p;
}

}  // namespace

TEST_CASE("A4-R2 conductance model grows w as dt G times the summed force") {
    ParallelModel model;
    REQUIRE(pme::bridge::conductanceModel(2.0e-3, model) == Status::Ok);
    BridgePoint<double> p = makePoint(model);
    CHECK(p.modeCount() == 0);
    const double dt = 1.0 / kFs;
    double expected = 0.0;
    for (int n = 0; n < 200; ++n) {
        const double f1 = 3.0 + 0.01 * n;
        const double f2 = -1.0;
        p.addForce(f1, 0.0);
        p.addForce(f2, 0.0);  // two segments: forces summed
        p.advance();
        expected += dt * 2.0e-3 * (f1 + f2);
        CHECK(p.wV() == doctest::Approx(expected).epsilon(1e-12));
        CHECK(p.wH() == 0.0);
    }
    // delta_tt of a force-driven conductance: (dw^n - dw^{n-1}) / dt^2 = G (F^n - F^{n-1}) / dt
    p.addForce(7.0, 0.0);
    p.advance();
    const double lastDw = p.incrementV();
    p.addForce(9.0, 0.0);
    p.advance();
    CHECK(p.accelV() == doctest::Approx((p.incrementV() - lastDw) * kFs * kFs).epsilon(1e-12));
    CHECK(p.conductanceDissipatedEnergy() > 0.0);
}

TEST_CASE("A4-R2 conductance model rigid bridge never moves") {
    ParallelModel model;
    REQUIRE(pme::bridge::conductanceModel(0.0, model) == Status::Ok);
    BridgePoint<float> p;
    REQUIRE(p.configure(pme::BridgePointDesc{}, model, kFs) == Status::Ok);
    for (int n = 0; n < 50; ++n) {
        p.addForce(5.0F, 2.0F);
        p.advance();
        CHECK(p.wV() == 0.0F);
        CHECK(p.wH() == 0.0F);
        CHECK(p.accelV() == 0.0F);
    }
}

TEST_CASE("A4-R2 mass-spring-damper bridge mode has the compiled frequency and decay") {
    const double m = 0.3;
    const double fb = 410.0;
    const double omega = 2.0 * kPi * fb;
    const double sigma = 35.0;
    ParallelModel model;
    REQUIRE(pme::bridge::massSpringDamperModel(m, m * omega * omega, 2.0 * m * sigma, model) ==
            Status::Ok);
    REQUIRE(model.modes.size() == 1);
    CHECK(model.modes[0].omega == doctest::Approx(omega).epsilon(1e-14));
    CHECK(model.modes[0].sigma == doctest::Approx(sigma).epsilon(1e-14));
    CHECK(model.modes[0].phi == doctest::Approx(1.0 / std::sqrt(m)).epsilon(1e-14));
    CHECK(model.conductance == 0.0);

    BridgePoint<double> p = makePoint(model);
    CHECK(p.modeCount() == 1);
    // One force impulse, then free response. The discrete poles are exactly z = exp((-sigma +/-
    // i omega_d) dt) (SPEC-OSC), so w^{n+1} = 2 Re(z) w^n - |z|^2 w^{n-1} after the impulse step.
    p.addForce(40.0, 0.0);
    p.advance();
    std::vector<double> w{0.0, p.wV()};
    for (int n = 0; n < 400; ++n) {
        p.advance();
        w.push_back(p.wV());
    }
    const double dt = 1.0 / kFs;
    const double omegaD = std::sqrt((omega - sigma) * (omega + sigma));
    const double decay = std::exp(-sigma * dt);
    const double twoRe = 2.0 * decay * std::cos(omegaD * dt);
    const double mod2 = decay * decay;
    double maxErr = 0.0;
    double scale = 0.0;
    for (std::size_t i = 2; i < w.size(); ++i) {
        const double pred = twoRe * w[i - 1] - mod2 * w[i - 2];
        maxErr = std::max(maxErr, std::abs(w[i] - pred));
        scale = std::max(scale, std::abs(w[i]));
    }
    CHECK(scale > 0.0);
    CHECK(maxErr < 1e-11 * scale);
    // The mass-weighted energy decays (sigma > 0) and is positive.
    CHECK(p.modeEnergy() > 0.0);
}

TEST_CASE("A4-R2 lossless mass-spring bridge keeps its discrete energy") {
    ParallelModel model;
    REQUIRE(pme::bridge::massSpringDamperModel(0.5, 0.5 * std::pow(2.0 * kPi * 200.0, 2), 0.0,
                                               model) == Status::Ok);
    BridgePoint<double> p = makePoint(model);
    p.addForce(10.0, 0.0);
    p.advance();
    p.advance();
    const double e0 = p.modeEnergy();
    REQUIRE(e0 > 0.0);
    for (int n = 0; n < 5000; ++n) {
        p.advance();
        CHECK(std::abs(p.modeEnergy() - e0) <= 1e-11 * e0);
    }
}

TEST_CASE("A4-R2 free mass and mass with damper under a constant force") {
    const double dt = 1.0 / kFs;
    {
        ParallelModel model;
        REQUIRE(pme::bridge::massSpringDamperModel(0.8, 0.0, 0.0, model) == Status::Ok);
        BridgePoint<double> p = makePoint(model);
        const double force = 2.5;
        for (int n = 0; n < 300; ++n) {
            p.addForce(force, 0.0);
            p.advance();
            // free mass: w^{n+1} = F/m dt^2 (n+1)(n+2)/2 after the step with index n
            const auto k = static_cast<double>(n + 1);
            CHECK(p.wV() ==
                  doctest::Approx(force / 0.8 * dt * dt * k * (k + 1.0) / 2.0).epsilon(1e-12));
            CHECK(p.accelV() == doctest::Approx(force / 0.8).epsilon(1e-9));
        }
    }
    {
        const double m = 0.8;
        const double r = 40.0;
        ParallelModel model;
        REQUIRE(pme::bridge::massSpringDamperModel(m, 0.0, r, model) == Status::Ok);
        BridgePoint<double> p = makePoint(model);
        const double force = 2.5;
        for (int n = 0; n < 20000; ++n) {
            p.addForce(force, 0.0);
            p.advance();
        }
        // terminal velocity F / (2 m sigmaHat) with sigmaHat = tanh(sigma dt) / dt
        const double sigmaHat = std::tanh(r / (2.0 * m) * dt) / dt;
        const double terminal = force / (2.0 * m * sigmaHat);
        CHECK(p.incrementV() / dt == doctest::Approx(terminal).epsilon(1e-9));
        CHECK(terminal == doctest::Approx(force / r).epsilon(1e-3));
    }
}

TEST_CASE("A4-R2 modal-set model equals the sum of its modes plus the conductance") {
    pme::LocalBridge local;
    local.omega = {2.0 * kPi * 150.0, 2.0 * kPi * 420.0, 2.0 * kPi * 1100.0};
    local.sigma = {20.0, 35.0, 80.0};
    local.phi = {1.2, -0.7, 0.4};
    local.modeIndex = {0, 1, 2};
    local.conductanceHf = 4.0e-4;
    ParallelModel full;
    REQUIRE(pme::bridge::modalSetModel(local, full) == Status::Ok);
    CHECK(full.kind == ParallelModel::Kind::ModalSet);
    BridgePoint<double> all = makePoint(full);
    CHECK(all.modeCount() == 3);

    std::vector<BridgePoint<double>> parts;
    for (std::size_t k = 0; k < 3; ++k) {
        pme::LocalBridge one;
        one.omega = {local.omega[k]};
        one.sigma = {local.sigma[k]};
        one.phi = {local.phi[k]};
        one.modeIndex = {0};
        one.conductanceHf = 0.0;
        ParallelModel pm;
        REQUIRE(pme::bridge::modalSetModel(one, pm) == Status::Ok);
        parts.push_back(makePoint(pm));
    }
    ParallelModel gOnly;
    REQUIRE(pme::bridge::conductanceModel(local.conductanceHf, gOnly) == Status::Ok);
    BridgePoint<double> cond = makePoint(gOnly);

    double maxErr = 0.0;
    double scale = 0.0;
    for (int n = 0; n < 3000; ++n) {
        const double f = (n < 40 ? 5.0 : 0.0) + 0.3 * std::sin(0.01 * n);
        all.addForce(f, 0.0);
        all.advance();
        double sum = 0.0;
        for (auto& p : parts) {
            p.addForce(f, 0.0);
            p.advance();
            sum += p.wV();
        }
        cond.addForce(f, 0.0);
        cond.advance();
        sum += cond.wV();
        maxErr = std::max(maxErr, std::abs(all.wV() - sum));
        scale = std::max(scale, std::abs(all.wV()));
    }
    CHECK(scale > 0.0);
    CHECK(maxErr <= 1e-12 * scale);
}

TEST_CASE("A4-R2 modal-set modes failing the bandwidth rule are dropped and other kinds reject") {
    pme::LocalBridge local;
    local.omega = {2.0 * kPi * 300.0, 2.0 * kPi * 23500.0};  // second: omega_d dt > 0.95 pi
    local.sigma = {10.0, 10.0};
    local.phi = {1.0, 1.0};
    local.modeIndex = {0, 1};
    ParallelModel set;
    REQUIRE(pme::bridge::modalSetModel(local, set) == Status::Ok);
    BridgePoint<double> p;
    REQUIRE(p.configure(pme::BridgePointDesc{}, set, kFs) == Status::Ok);
    CHECK(p.modeCount() == 1);
    CHECK(p.droppedModes() == 1);

    // the same mode as a mass-spring-damper is refused, and the point is left unchanged
    ParallelModel msd;
    const double hi = 2.0 * kPi * 23500.0;
    REQUIRE(pme::bridge::massSpringDamperModel(1.0, hi * hi, 20.0, msd) == Status::Ok);
    CHECK(p.configure(pme::BridgePointDesc{}, msd, kFs) == Status::Rejected);
    CHECK(p.modeCount() == 1);
    // overdamped mass-spring-damper (sigma >= omega) is refused by the SPEC-OSC bandwidth rule
    REQUIRE(pme::bridge::massSpringDamperModel(1.0, 100.0, 40.0, msd) == Status::Ok);
    CHECK(p.configure(pme::BridgePointDesc{}, msd, kFs) == Status::Rejected);
}

TEST_CASE("A4-R2 model factories and configure reject invalid arguments") {
    ParallelModel out;
    out.conductance = 9.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(pme::bridge::conductanceModel(-1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::conductanceModel(nan, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(0.0, 1.0, 1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(-1.0, 1.0, 1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(1.0, -1.0, 1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(1.0, 1.0, -1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(nan, 1.0, 1.0, out) == Status::InvalidArgument);
    CHECK(pme::bridge::massSpringDamperModel(1.0, 1.0, std::numeric_limits<double>::infinity(),
                                             out) == Status::InvalidArgument);
    CHECK(out.conductance == 9.0);

    pme::LocalBridge lb;
    lb.omega = {100.0};
    lb.sigma = {1.0, 2.0};
    lb.phi = {1.0};
    CHECK(pme::bridge::modalSetModel(lb, out) == Status::InvalidArgument);
    lb.sigma = {1.0};
    lb.conductanceHf = -1.0;
    CHECK(pme::bridge::modalSetModel(lb, out) == Status::InvalidArgument);
    lb.conductanceHf = 0.0;
    lb.omega = {-5.0};
    CHECK(pme::bridge::modalSetModel(lb, out) == Status::InvalidArgument);
    lb.omega = {100.0};
    lb.sigma = {nan};
    CHECK(pme::bridge::modalSetModel(lb, out) == Status::InvalidArgument);
    lb.sigma = {1.0};
    lb.phi = {nan};
    CHECK(pme::bridge::modalSetModel(lb, out) == Status::InvalidArgument);
    CHECK(out.conductance == 9.0);

    ParallelModel good;
    REQUIRE(pme::bridge::conductanceModel(0.0, good) == Status::Ok);
    BridgePoint<double> p;
    pme::BridgePointDesc d;
    CHECK(p.configure(d, good, 0.0) == Status::InvalidArgument);
    CHECK(p.configure(d, good, nan) == Status::InvalidArgument);
    d.thetaB = nan;
    CHECK(p.configure(d, good, kFs) == Status::InvalidArgument);
    d.thetaB = 0.0;
    d.conductanceH = -1.0;
    CHECK(p.configure(d, good, kFs) == Status::InvalidArgument);
    d.conductanceH = 0.0;
    ParallelModel bad = good;
    bad.conductance = -1.0;
    CHECK(p.configure(d, bad, kFs) == Status::InvalidArgument);
    bad = good;
    bad.modes.push_back({-1.0, 0.0, 1.0});
    CHECK(p.configure(d, bad, kFs) == Status::InvalidArgument);
    bad.modes = {{10.0, nan, 1.0}};
    CHECK(p.configure(d, bad, kFs) == Status::InvalidArgument);
    bad.modes = {{10.0, 0.0, nan}};
    CHECK(p.configure(d, bad, kFs) == Status::InvalidArgument);
}

TEST_CASE("A4-R2 free-mass mode compile matches the overdamped continuation") {
    pme::osc::OscCoeffsF64 c;
    REQUIRE(pme::bridge::compileBridgeMode(0.0, 0.0, kFs, c) == Status::Ok);
    CHECK(c.omegaHat2 == 0.0);
    CHECK(c.beta == 1.0);
    CHECK(c.kappa == 0.0);
    CHECK(c.gamma == doctest::Approx(1.0 / (kFs * kFs)).epsilon(1e-15));
    REQUIRE(pme::bridge::compileBridgeMode(0.0, 30.0, kFs, c) == Status::Ok);
    CHECK(c.sigmaHat == doctest::Approx(std::tanh(30.0 / kFs) * kFs).epsilon(1e-14));
    // normal modes go through A1 unchanged
    pme::osc::OscCoeffsF64 a;
    pme::osc::OscCoeffsF64 b;
    REQUIRE(pme::bridge::compileBridgeMode(1000.0, 5.0, kFs, a) == Status::Ok);
    REQUIRE(pme::osc::compileOscillatorF64(1000.0, 5.0, kFs, b) == Status::Ok);
    CHECK(a.beta == b.beta);
    CHECK(a.kappa == b.kappa);
    // invalid arguments leave out unchanged
    const pme::osc::OscCoeffsF64 before = a;
    CHECK(pme::bridge::compileBridgeMode(0.0, -1.0, kFs, a) == Status::InvalidArgument);
    CHECK(pme::bridge::compileBridgeMode(0.0, 1.0, 0.0, a) == Status::InvalidArgument);
    CHECK(a.beta == before.beta);
}

TEST_CASE("A4-R5 a constant external velocity moves w_par by dt v_ext per sample") {
    ParallelModel model;
    REQUIRE(pme::bridge::conductanceModel(0.0, model) == Status::Ok);
    BridgePoint<double> p = makePoint(model);
    const double dt = 1.0 / kFs;
    // inert by default
    p.advance();
    CHECK(p.wV() == 0.0);
    CHECK(p.wExternal() == 0.0);
    p.setExternalVelocity(0.02);
    for (int n = 1; n <= 100; ++n) {
        p.advance();
        CHECK(p.wExternal() == doctest::Approx(n * dt * 0.02).epsilon(1e-12));
        CHECK(p.wParallel() == doctest::Approx(n * dt * 0.02).epsilon(1e-12));
        CHECK(p.wV() == doctest::Approx(n * dt * 0.02).epsilon(1e-12));
        CHECK(p.wH() == 0.0);
    }
    // Constant velocity: no acceleration after the first sample.
    CHECK(std::abs(p.accelV()) < 1e-9);
    p.setExternalVelocity(0.0);
    const double w = p.wV();
    p.advance();
    CHECK(p.wV() == w);

    // With theta_b the external motion acts on the parallel axis and projects onto v and h.
    BridgePoint<double> q = makePoint(model, 0.3);
    q.setExternalVelocity(0.01);
    for (int n = 0; n < 10; ++n) {
        q.advance();
    }
    CHECK(q.wV() == doctest::Approx(std::cos(0.3) * 10 * dt * 0.01).epsilon(1e-12));
    CHECK(q.wH() == doctest::Approx(std::sin(0.3) * 10 * dt * 0.01).epsilon(1e-12));
}

TEST_CASE("A4-R5 a constant extra vertical force gives the static deflection F over k_b") {
    const double m = 0.4;
    const double k = 2.0e5;  // f_b ~ 112 Hz
    ParallelModel model;
    REQUIRE(pme::bridge::massSpringDamperModel(m, k, 2.0 * m * 200.0, model) == Status::Ok);
    BridgePoint<double> p = makePoint(model);
    const double extra = 12.0;
    for (int n = 0; n < 48000; ++n) {
        p.addVerticalForce(extra);
        p.advance();
    }
    CHECK(p.wV() == doctest::Approx(extra / k).epsilon(1e-4));
    // exact discrete value: F / (m omegaHat^2)
    pme::osc::OscCoeffsF64 c;
    REQUIRE(pme::osc::compileOscillatorF64(std::sqrt(k / m), 200.0, kFs, c) == Status::Ok);
    CHECK(p.wV() == doctest::Approx(extra / (m * c.omegaHat2)).epsilon(1e-9));
    CHECK(p.wH() == 0.0);
    CHECK(p.stage() == pme::bridge::Stage::Advanced);
}
