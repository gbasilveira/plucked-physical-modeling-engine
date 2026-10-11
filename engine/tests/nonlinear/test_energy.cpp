// SPDX-License-Identifier: MIT
// Covers: A3-R2, A3-T2, A3-T3
// Energy conservation of the SAV scheme across many modes (A3-T2) and the energy balance with
// dashpots, r = 1, 2 and 4 (A3-T3). float64 instantiation.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <span>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "test_support.hpp"

namespace {

using pme::nl::Dashpot;
using pme::nl::Potential;
using pme::nl::SavSegment;
using pme::nl::test::QuarticPotential;
using pme::nl::test::Rng;
using pme::nl::test::TensionPotential;

constexpr double kFs = 48000.0;
constexpr std::size_t kModes = 64;

// 64 lossless unit-mass modes, 50 Hz to 5 kHz.
void buildModes(SavSegment<double>& seg, Rng& rng, double& linearEnergy) {
    pme::nl::SavParams params;
    params.driftRelaxation = false;  // A3-T2 and A3-T3 disable the drift rule
    REQUIRE(seg.configure(kModes, 0, 3, kFs, params) == pme::Status::Ok);
    linearEnergy = 0.0;
    for (std::size_t i = 0; i < kModes; ++i) {
        const double f = 50.0 + (5000.0 - 50.0) * static_cast<double>(i) / (kModes - 1);
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(2.0 * std::numbers::pi * f, 0.0, kFs, c) ==
                pme::Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, 1.0) == pme::Status::Ok);
        seg.q()[i] = rng.uniform(-1.0, 1.0) * 1e-3;
        linearEnergy += 0.5 * c.omegaHat2 * seg.q()[i] * seg.q()[i];
    }
}

std::vector<double> randomVector(Rng& rng, double lo, double hi) {
    std::vector<double> v(kModes);
    for (double& x : v) {
        x = rng.uniform(lo, hi);
    }
    return v;
}

double dot(const std::vector<double>& c, const double* z) {
    double s = 0.0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        s += c[i] * z[i];
    }
    return s;
}

struct DashpotCase {
    const char* name;
    std::size_t separate;  // dashpots not along the gradient
    bool aligned;          // one dashpot along the gradient (scaled copy of c)
    bool stateDependent;   // R varies with the state
};

}  // namespace

TEST_CASE("A3-T2 quartic potential across many modes conserves energy within 1e-10") {
    Rng rng(7U);
    SavSegment<double> seg;
    double eLin = 0.0;
    buildModes(seg, rng, eLin);
    const std::vector<double> c = randomVector(rng, -1.0, 1.0);
    const double s0 = dot(c, seg.q());
    const double k = 4.0 * 0.2 * eLin / (s0 * s0 * s0 * s0);
    QuarticPotential<double> v(k, c);
    const std::array<const Potential<double>*, 1> pots{&v};
    seg.initialisePsi(pots);
    REQUIRE(0.25 * k * s0 * s0 * s0 * s0 > 1e6 * seg.params().eps0);  // V large against eps0

    pme::nl::savStep<double>(seg, pots, {}, {});
    const double e0 = pme::nl::savEnergy(seg);
    double drift = 0.0;
    for (int n = 1; n < 100000; ++n) {
        pme::nl::savStep<double>(seg, pots, {}, {});
        drift = std::max(drift, std::abs(pme::nl::savEnergy(seg) - e0) / e0);
    }
    std::printf("A3-T2 quartic: E0 = %.6e, max relative drift over 1e5 steps = %.3e\n", e0, drift);
    CHECK(drift <= 1e-10);
}

TEST_CASE("A3-T2 tension-modulation potential across many modes conserves energy within 1e-10") {
    Rng rng(11U);
    SavSegment<double> seg;
    double eLin = 0.0;
    buildModes(seg, rng, eLin);
    const std::vector<double> w = randomVector(rng, 0.1, 1.0);
    double s0 = 0.0;
    for (std::size_t i = 0; i < kModes; ++i) {
        s0 += w[i] * seg.q()[i] * seg.q()[i];
    }
    const double k = 4.0 * 0.2 * eLin / (s0 * s0);
    TensionPotential<double> v(k, w);
    const std::array<const Potential<double>*, 1> pots{&v};
    seg.initialisePsi(pots);

    pme::nl::savStep<double>(seg, pots, {}, {});
    const double e0 = pme::nl::savEnergy(seg);
    double drift = 0.0;
    for (int n = 1; n < 100000; ++n) {
        pme::nl::savStep<double>(seg, pots, {}, {});
        drift = std::max(drift, std::abs(pme::nl::savEnergy(seg) - e0) / e0);
    }
    std::printf("A3-T2 tension: E0 = %.6e, max relative drift over 1e5 steps = %.3e\n", e0, drift);
    CHECK(drift <= 1e-10);
}

TEST_CASE("A3-T3 any dashpot makes energy non-increasing and the balance closes") {
    const std::array<DashpotCase, 6> cases{{{"r=1 aligned, constant R", 0, true, false},
                                            {"r=1 aligned, state-dependent R", 0, true, true},
                                            {"r=2 one separate dashpot", 1, false, false},
                                            {"r=2 aligned plus one separate", 1, true, true},
                                            {"r=4 three separate dashpots", 3, false, false},
                                            {"r=4 three separate, state-dependent R", 3, false, true}}};
    for (const DashpotCase& cs : cases) {
        CAPTURE(cs.name);
        Rng rng(31U);
        SavSegment<double> seg;
        double eLin = 0.0;
        buildModes(seg, rng, eLin);
        const std::vector<double> c = randomVector(rng, -1.0, 1.0);
        const double s0 = dot(c, seg.q());
        const double k = 4.0 * 0.2 * eLin / (s0 * s0 * s0 * s0);
        QuarticPotential<double> v(k, c);
        const std::array<const Potential<double>*, 1> pots{&v};
        seg.initialisePsi(pots);

        std::vector<std::vector<double>> dirs;
        std::vector<double> baseR;
        std::vector<bool> aligned;
        if (cs.aligned) {
            std::vector<double> d = c;
            for (double& x : d) {
                x *= 0.7;
            }
            dirs.push_back(d);
            baseR.push_back(0.4);
            aligned.push_back(true);
        }
        for (std::size_t j = 0; j < cs.separate; ++j) {
            dirs.push_back(randomVector(rng, -1.0, 1.0));
            baseR.push_back(0.1 * static_cast<double>(j + 1));
            aligned.push_back(false);
        }
        std::vector<Dashpot<double>> dash(dirs.size());
        for (std::size_t j = 0; j < dirs.size(); ++j) {
            dash[j].c = std::span<const double>(dirs[j]);
            dash[j].alongGradient = aligned[j];
        }
        const auto setR = [&]() {
            for (std::size_t j = 0; j < dirs.size(); ++j) {
                const double cz = dot(dirs[j], seg.q());
                dash[j].R =
                    cs.stateDependent ? baseR[j] * (1.0 + std::abs(cz) / std::abs(s0)) : baseR[j];
            }
        };

        setR();
        pme::nl::savStep<double>(seg, pots, dash, {});
        const double e0 = pme::nl::savEnergy(seg);
        double prev = e0;
        double worstIncrease = -1e300;
        double worstBalance = 0.0;
        for (int n = 1; n < 20000; ++n) {
            setR();
            pme::nl::savStep<double>(seg, pots, dash, {});
            const double e = pme::nl::savEnergy(seg);
            const double diss = pme::nl::savDissipation<double>(seg, dash);
            worstIncrease = std::max(worstIncrease, (e - prev) / e0);
            worstBalance = std::max(worstBalance, std::abs(e - prev + diss) / e0);
            REQUIRE(diss >= 0.0);
            prev = e;
        }
        std::printf("A3-T3 %-40s worst step increase %.3e, worst balance error %.3e, E end/E0 %.4f\n",
                    cs.name, worstIncrease, worstBalance, prev / e0);
        CHECK(worstIncrease <= 1e-13);
        CHECK(worstBalance <= 1e-12);
        CHECK(prev < 0.99 * e0);  // the dashpot actually removed energy
    }
}
