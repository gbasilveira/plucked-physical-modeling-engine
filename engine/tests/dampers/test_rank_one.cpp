// SPDX-License-Identifier: MIT
// Covers: A6-R2, A6-T1
// The rank-one damper: equivalence of the Dashpot-plus-explicit-force form with SPEC's low-rank
// pair, and monotone energy decrease of a lossless string.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "fixtures.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/rank_one.hpp"

namespace {

using namespace pme;
using namespace pme::dmp;
using pme::dmp_test::examplePad;
using pme::dmp_test::kFs;

constexpr std::span<const nl::Potential<double>* const> kNoPotentials{};

// A segment of the string's vertical modes, float64, with the pluck as initial displacement.
nl::SavSegment<double> makeSegment(const StringModes& m, std::size_t extra = 0) {
    nl::SavSegment<double> seg;
    REQUIRE(seg.configure(m.omega.size(), extra, 1, m.fs) == Status::Ok);
    for (std::size_t i = 0; i < m.omega.size(); ++i) {
        osc::OscCoeffsF64 c;
        REQUIRE(osc::compileOscillatorF64(m.omega[i], m.sigma[i], m.fs, c) == Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, m.rhoA) == Status::Ok);
    }
    for (std::size_t i = 0; i < extra; ++i) {
        REQUIRE(seg.setLumped(m.omega.size() + i, 0.01) == Status::Ok);
    }
    const std::vector<double> q = pme::dmp_test::pluckModal(m, 0.2);
    for (std::size_t i = 0; i < q.size(); ++i) {
        seg.q()[i] = q[i];
    }
    return seg;
}

}  // namespace

TEST_CASE("A6-R2 Dashpot plus explicit force equals SPEC's low-rank pair") {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    const std::size_t N = m.omega.size();
    const DamperParams p = examplePad();
    RankOneDamper<double> damper;
    REQUIRE(damper.configure(m, p, N) == Status::Ok);
    const double dt = 1.0 / m.fs;

    nl::SavSegment<double> seg = makeSegment(m);
    // Direct implementation of SPEC-DMP "Rank-one" state, kept outside the segment.
    std::vector<double> q = std::vector<double>(seg.q(), seg.q() + N);
    std::vector<double> u(N, 0.0);
    std::vector<double> beta(N);
    std::vector<double> kappa(N);
    std::vector<double> gamma(N);
    for (std::size_t i = 0; i < N; ++i) {
        osc::OscCoeffsF64 c;
        REQUIRE(osc::compileOscillatorF64(m.omega[i], m.sigma[i], m.fs, c) == Status::Ok);
        beta[i] = c.beta;
        kappa[i] = c.kappa;
        gamma[i] = c.gamma;
    }
    const std::span<const double> cvec = damper.direction64();

    std::vector<double> force(N, 0.0);
    std::vector<double> a(N);
    std::vector<double> z(N);
    std::vector<double> h(N);
    std::vector<double> w(N);
    double worst = 0.0;
    double scale = 0.0;
    for (int step = 0; step < 1000; ++step) {
        // e varies per sample, as it does while a damper moves.
        const double e = 0.5 + 0.5 * std::sin(0.01 * static_cast<double>(step));

        // Dashpot + explicit force through A3's savStep.
        std::fill(force.begin(), force.end(), 0.0);
        damper.addSpringForce(e, seg, force);
        const std::array<nl::Dashpot<double>, 1> dash{damper.dashpot(e)};
        nl::savStep<double>(seg, kNoPotentials, dash, force);

        // SPEC: h = gamma (.) e c / m, w = c (K_d / 2 + R_d / (2 dt)), solved with lowRankSolve.
        double yPrev = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            yPrev += cvec[i] * (q[i] - u[i]);  // c^T z^{n-1}
        }
        for (std::size_t i = 0; i < N; ++i) {
            const double free = q[i] + (beta[i] * u[i] - kappa[i] * q[i]);
            h[i] = gamma[i] * e * cvec[i] / m.rhoA;
            w[i] = cvec[i] * (p.Kd / 2.0 + p.Rd / (2.0 * dt));
            // The y^{n-1} part of the SPEC force: -e c (K_d / 2 - R_d / (2 dt)) y^{n-1}.
            a[i] =
                free - gamma[i] * e * cvec[i] / m.rhoA * (p.Kd / 2.0 - p.Rd / (2.0 * dt)) * yPrev;
        }
        const double* hp = h.data();
        const double* wp = w.data();
        nl::lowRankSolve(z.data(), a.data(), N, 1, &hp, &wp);
        for (std::size_t i = 0; i < N; ++i) {
            u[i] = z[i] - q[i];
            q[i] = z[i];
        }

        for (std::size_t i = 0; i < N; ++i) {
            worst = std::max(worst, std::fabs(seg.q()[i] - q[i]));
            scale = std::max(scale, std::fabs(q[i]));
        }
    }
    MESSAGE("worst |q_savStep - q_SPEC| / max|q| over 1000 steps: " << worst / scale);
    CHECK(worst <= 1e-12 * scale);
    CHECK(scale > 0.0);
}

TEST_CASE("A6-R2 direction dashpot and spring energy") {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    const std::size_t N = m.omega.size();
    const DamperParams p = examplePad();
    RankOneDamper<double> damper;
    // Two lumped coordinates beyond the modes, damper starting at the first mode.
    REQUIRE(damper.configure(m, p, N + 2) == Status::Ok);
    CHECK(damper.direction().size() == N + 2);
    CHECK(damper.direction()[N] == 0.0);
    CHECK(damper.direction()[N + 1] == 0.0);
    CHECK(damper.Kd() == p.Kd);
    CHECK(damper.Rd() == p.Rd);
    CHECK(damper.dt() == 1.0 / kFs);
    // c is phi-bar_d in float64.
    const std::vector<double> phi = padShape(m, p);
    for (std::size_t i = 0; i < N; ++i) {
        CHECK(damper.direction64()[i] == phi[i]);
    }
    // R = e (R_d + K_d dt), never along the gradient.
    const nl::Dashpot<double> d = damper.dashpot(0.4);
    CHECK(d.R == doctest::Approx(0.4 * (p.Rd + p.Kd / kFs)).epsilon(1e-15));
    CHECK_FALSE(d.alongGradient);
    CHECK(d.c.size() == N + 2);
    CHECK(damper.dashpot(0.0).R == 0.0);

    // A damper placed after a lumped coordinate.
    RankOneDamper<double> shifted;
    REQUIRE(shifted.configure(m, p, N + 3, 3) == Status::Ok);
    CHECK(shifted.direction64()[0] == 0.0);
    CHECK(shifted.direction64()[3] == phi[0]);

    // Spring energy and pad displacement on a known state.
    nl::SavSegment<double> seg = makeSegment(m, 2);
    RankOneDamper<double> withLumped;
    REQUIRE(withLumped.configure(m, p, N + 2) == Status::Ok);
    const double y0 = withLumped.pad(seg.q());
    double direct = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        direct += phi[i] * seg.q()[i];
    }
    CHECK(y0 == doctest::Approx(direct).epsilon(1e-14));
    std::vector<double> force(N + 2, 0.0);
    const std::array<nl::Dashpot<double>, 1> dash{withLumped.dashpot(0.7)};
    withLumped.addSpringForce(0.7, seg, force);
    nl::savStep<double>(seg, kNoPotentials, dash, force);
    const double yNext = withLumped.pad(seg.q());
    const double yNow = withLumped.pad(seg.qPrev());
    CHECK(withLumped.springEnergy(0.7, seg) ==
          doctest::Approx(0.25 * 0.7 * p.Kd * (yNext * yNext + yNow * yNow)).epsilon(1e-14));

    // Configuration errors leave the damper unchanged.
    RankOneDamper<double> bad;
    CHECK(bad.configure(m, p, N - 1) == Status::InvalidArgument);
    CHECK(bad.configure(m, p, N + 1, 2) == Status::InvalidArgument);
    DamperParams broken = p;
    broken.Kd = -1.0;
    CHECK(bad.configure(m, broken, N) == Status::InvalidArgument);
    CHECK(bad.configure(StringModes{}, p, N) == Status::InvalidArgument);
    CHECK(bad.direction().empty());
}

TEST_CASE("A6-T1 rank-one damper makes the energy decrease monotonically") {
    // A lossless string (b = 0) of about 55 modes at 48 kHz, plucked.
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::lossless(0.65, 262.0));
    const std::size_t N = m.omega.size();
    REQUIRE(N >= 40);
    REQUIRE(N <= 80);
    for (double s : m.sigma) {
        REQUIRE(s == 0.0);
    }
    const DamperParams p = examplePad();
    for (double e : {1.0, 0.3}) {
        CAPTURE(e);
        RankOneDamper<double> damper;
        REQUIRE(damper.configure(m, p, N) == Status::Ok);
        nl::SavSegment<double> seg = makeSegment(m);
        std::vector<double> force(N, 0.0);
        const std::array<nl::Dashpot<double>, 1> dash{damper.dashpot(e)};
        const int steps = static_cast<int>(2.0 * kFs);
        double previous = 0.0;
        double first = 0.0;
        double worstRatio = 0.0;
        for (int step = 0; step < steps; ++step) {
            std::fill(force.begin(), force.end(), 0.0);
            damper.addSpringForce(e, seg, force);
            nl::savStep<double>(seg, kNoPotentials, dash, force);
            // total = sum rhoA H_n + E_d (no potential: psi^2/2 - eps0 = 0)
            const double energy = nl::savEnergy(seg) + damper.springEnergy(e, seg);
            if (step == 0) {
                first = energy;
            } else {
                worstRatio = std::max(worstRatio, energy / previous);
                REQUIRE(energy <= previous * (1.0 + 1e-13));
            }
            previous = energy;
        }
        MESSAGE("e " << e << ": E(0) " << first << " J, E(2 s) " << previous << " J, decay "
                     << first / previous
                     << ", worst E^{n+1/2}/E^{n-1/2} - 1 = " << (worstRatio - 1.0));
        CHECK(first > 0.0);
        CHECK(previous < first / 10.0);
    }
}
