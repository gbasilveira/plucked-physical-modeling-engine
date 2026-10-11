// SPDX-License-Identifier: MIT
// Covers: A3-R1, A3-R4, A3-R5, A3-T4
// Segment configuration (A3-R1), the skip rule, the reset-at-separation rule and the
// persistent-contact relaxation (A3-R4, A3-T4) and the diagnostic counters (A3-R5).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "pme/osc/mode_bank.hpp"
#include "test_support.hpp"

namespace {

using pme::nl::Dashpot;
using pme::nl::Potential;
using pme::nl::SavSegment;
using pme::nl::test::QuarticPotential;
using pme::nl::test::Rng;
using pme::nl::test::WallPotential;

constexpr double kFs = 48000.0;

}  // namespace

TEST_CASE("A3-R1 configure rejects more than 3 dashpots and invalid arguments") {
    SavSegment<float> seg;
    CHECK(seg.configure(8, 1, 4, kFs) == pme::Status::InvalidArgument);
    CHECK(seg.size() == 0U);
    CHECK(seg.configure(8, 1, 3, 0.0) == pme::Status::InvalidArgument);
    CHECK(seg.configure(0, 0, 1, kFs) == pme::Status::InvalidArgument);
    pme::nl::SavParams bad;
    bad.eps0 = 0.0;
    CHECK(seg.configure(8, 1, 1, kFs, bad) == pme::Status::InvalidArgument);
    CHECK(seg.size() == 0U);
    CHECK(seg.configure(8, 1, 3, kFs) == pme::Status::Ok);
    CHECK(seg.size() == 9U);
    CHECK(seg.modes() == 8U);
    CHECK(seg.lumped() == 1U);
    CHECK(seg.setCoordinate(9, pme::osc::OscCoeffsF64{}, 1.0) == pme::Status::InvalidArgument);
    CHECK(seg.setLumped(0, -1.0) == pme::Status::InvalidArgument);
}

TEST_CASE("A3-R1 a lumped coordinate has beta 1, kappa 0 and gamma dt squared; padding stays zero") {
    const std::size_t nModes = 18;
    SavSegment<float> seg;
    REQUIRE(seg.configure(nModes, 3, 3, kFs) == pme::Status::Ok);
    CHECK(seg.paddedSize() == 32U);
    for (std::size_t i = 0; i < nModes; ++i) {
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(2.0 * std::numbers::pi * (200.0 + 30.0 * static_cast<double>(i)), 1.0,
                                               kFs, c) == pme::Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, 0.002) == pme::Status::Ok);
    }
    for (std::size_t i = nModes; i < seg.size(); ++i) {
        REQUIRE(seg.setLumped(i, 0.01) == pme::Status::Ok);
        CHECK(seg.beta()[i] == 1.0F);
        CHECK(seg.kappa()[i] == 0.0F);
        CHECK(seg.gamma()[i] == doctest::Approx(1.0 / (kFs * kFs)).epsilon(1e-6));
        CHECK(seg.omegaHat2()[i] == 0.0);
    }
    const WallPotential<float> wall(nModes, 1e-3, 1.0, 1e6, 2.0);
    const std::array<const Potential<float>*, 1> pots{&wall};
    std::vector<float> dir(seg.size(), 0.0F);
    dir[nModes] = 1.0F;
    std::array<Dashpot<float>, 1> dash{{{std::span<const float>(dir), 0.5, true}}};
    seg.q()[nModes] = 1.5e-3F;  // inside the wall
    seg.q()[0] = 1e-4F;
    seg.initialisePsi(pots);
    for (int s = 0; s < 200; ++s) {
        pme::nl::savStep<float>(seg, pots, dash, {});
    }
    for (std::size_t i = seg.size(); i < seg.paddedSize(); ++i) {
        CHECK(seg.q()[i] == 0.0F);
        CHECK(seg.u()[i] == 0.0F);
        CHECK(seg.qPrev()[i] == 0.0F);
        CHECK(seg.uPrev()[i] == 0.0F);
        CHECK(seg.beta()[i] == 0.0F);
        CHECK(seg.kappa()[i] == 0.0F);
        CHECK(seg.gamma()[i] == 0.0F);
    }
}

TEST_CASE("A3-R4 the skip rule is bit-identical to the A1 free update and counts skipped solves") {
    const std::size_t n = 32;
    SavSegment<float> seg;
    REQUIRE(seg.configure(n, 0, 3, kFs) == pme::Status::Ok);
    pme::osc::ModeBank bank;
    bank.resize(n);
    Rng rng(5U);
    for (std::size_t i = 0; i < n; ++i) {
        const double omega = 2.0 * std::numbers::pi * (100.0 + 400.0 * static_cast<double>(i));
        pme::OscCoeffs c32;
        pme::osc::OscCoeffsF64 c64;
        REQUIRE(pme::compileOscillator(omega, 3.0, kFs, c32) == pme::Status::Ok);
        REQUIRE(pme::osc::compileOscillatorF64(omega, 3.0, kFs, c64) == pme::Status::Ok);
        REQUIRE(seg.setCoordinate(i, c64, 1.0) == pme::Status::Ok);
        bank.setMode(i, c32);
        const auto x = static_cast<float>(rng.uniform(-1.0, 1.0));
        seg.q()[i] = x;
        bank.q()[i] = x;
    }
    const WallPotential<float> wall(0, 100.0, 1.0, 1e6, 2.0);  // far away: V = 0
    const std::array<const Potential<float>*, 1> pots{&wall};
    std::vector<float> force(n);
    constexpr int steps = 100;
    for (int s = 0; s < steps; ++s) {
        for (float& f : force) {
            f = static_cast<float>(rng.uniform(-1.0, 1.0));
        }
        pme::nl::savStep<float>(seg, pots, {}, force);
        bank.step(force);
        for (std::size_t i = 0; i < n; ++i) {
            REQUIRE(seg.q()[i] == bank.q()[i]);
            REQUIRE(seg.u()[i] == bank.u()[i]);
        }
    }
    CHECK(seg.counters().solvesSkipped == static_cast<std::uint32_t>(steps));
    CHECK(seg.counters().resets == 0U);
    CHECK(seg.counters().driftCorrections == 0U);
}

TEST_CASE("A3-T4 the reset rule fires once per separation and never raises energy") {
    constexpr double m = 5e-3;
    constexpr double wallPos = 0.01;
    SavSegment<double> seg;
    REQUIRE(seg.configure(0 + 1, 0, 1, kFs) == pme::Status::Ok);  // one lumped coordinate
    REQUIRE(seg.setLumped(0, m) == pme::Status::Ok);
    // A mass between two one-sided walls bounces back and forth: V = K/(p+1) [z - L]_+^{p+1}
    // + K/(p+1) [-z - L]_+^{p+1}.
    const WallPotential<double> upper(0, wallPos, 1.0, 1e7, 2.0);
    const WallPotential<double> lower(0, -wallPos, -1.0, 1e7, 2.0);
    const std::array<const Potential<double>*, 2> pots{&upper, &lower};
    const double v0 = 2.0;
    seg.q()[0] = 0.0;
    seg.u()[0] = v0 / kFs;
    seg.initialisePsi(pots);
    const double psi0 = std::sqrt(2.0 * seg.params().eps0);

    pme::nl::savStep<double>(seg, pots, {}, {});
    double vPrev = 0.0;  // V at the previous step's z^n
    double ePrev = pme::nl::savEnergy(seg);
    const double e0 = ePrev;
    bool inContact = false;
    int contacts = 0;
    int expectedResets = 0;
    double worstRise = -1e300;
    int steps = 0;
    while (contacts < 20 && steps < 400000) {
        ++steps;
        const double vNow = upper.value(std::span<const double>(seg.q(), 1)) +
                            lower.value(std::span<const double>(seg.q(), 1));
        const std::uint32_t before = seg.counters().resets;
        pme::nl::savStep<double>(seg, pots, {}, {});
        const std::uint32_t after = seg.counters().resets;
        const double e = pme::nl::savEnergy(seg);
        if (vNow > 0.0) {
            inContact = true;
            CHECK(after == before);  // the reset never fires during contact
        } else if (vPrev > 0.0) {
            CHECK(after == before);  // nor on the first sample after the last contact sample
        } else if (inContact) {
            // First step with V^n = V^{n-1} = 0 after a contact: the reset fires exactly once.
            ++contacts;
            ++expectedResets;
            inContact = false;
            CHECK(after == before + 1U);
            CHECK(seg.psi() == doctest::Approx(psi0).epsilon(1e-9));
            worstRise = std::max(worstRise, (e - ePrev) / e0);
        } else {
            CHECK(after == before);
        }
        vPrev = vNow;
        ePrev = e;
    }
    std::printf("A3-T4 contacts %d, reset counter %u, worst energy rise at a reset step %.3e\n",
                contacts, seg.counters().resets, worstRise);
    CHECK(contacts == 20);
    CHECK(seg.counters().resets == static_cast<std::uint32_t>(expectedResets));
    CHECK(worstRise <= 1e-12);
    // Lossless walls: the energy never exceeds E0; the resets remove only the psi^2/2 that
    // psi carried at separation.
    CHECK(ePrev <= e0 * (1.0 + 1e-12));
    CHECK(ePrev >= e0 * (1.0 - 1e-3));
}

TEST_CASE("A3-R4 persistent contact relaxes psi only after 50 ms and never raises energy") {
    constexpr double m = 0.01;
    constexpr double k = 1e5;
    constexpr double force = 10.0;
    SavSegment<double> seg;
    REQUIRE(seg.configure(1, 0, 1, kFs) == pme::Status::Ok);
    REQUIRE(seg.setLumped(0, m) == pme::Status::Ok);
    const WallPotential<double> wall(0, 0.0, 1.0, k, 1.0);  // V = K/2 [z]_+^2
    const std::array<const Potential<double>*, 1> pots{&wall};
    const std::array<double, 1> c{1.0};
    std::array<Dashpot<double>, 1> dash{{{std::span<const double>(c), 3.0, true}}};
    const std::array<double, 1> f{force};

    const double zEq = force / k;
    seg.q()[0] = zEq;
    seg.initialisePsi(pots);
    const double psiStar0 = seg.psi();
    seg.setPsi(1.05 * psiStar0);  // a drifted psi, above sqrt(2(V + eps0))

    // Effective energy: the constant force does work F (z^{n+1} + z^n) / 2.
    const auto eff = [&]() {
        return pme::nl::savEnergy(seg) - force * 0.5 * (seg.q()[0] + seg.qPrev()[0]);
    };
    const int contactSamplesNeeded = static_cast<int>(0.05 * kFs);
    double ePrev = 0.0;
    double worstRise = -1e300;
    int firstCorrection = -1;
    for (int s = 0; s < static_cast<int>(0.2 * kFs); ++s) {
        pme::nl::savStep<double>(seg, pots, dash, f);
        const double e = eff();
        if (s > 0) {
            worstRise = std::max(worstRise, e - ePrev);
        }
        ePrev = e;
        if (firstCorrection < 0 && seg.counters().driftCorrections > 0U) {
            firstCorrection = s;
        }
        REQUIRE(seg.q()[0] > 0.0);  // the contact is held for the whole 200 ms
    }
    std::printf("A3-R4 first drift correction at sample %d (threshold %d), corrections %u, worst "
                "energy rise %.3e, final psi/psi* %.6f\n",
                firstCorrection, contactSamplesNeeded, seg.counters().driftCorrections, worstRise,
                seg.psi() / std::sqrt(2.0 * (wall.value(std::span<const double>(seg.q(), 1)) +
                                             seg.params().eps0)));
    CHECK(firstCorrection > contactSamplesNeeded - 2);  // not before 50 ms of contact
    CHECK(firstCorrection <= contactSamplesNeeded + 2);
    CHECK(seg.counters().driftCorrections > 1000U);
    CHECK(worstRise <= 1e-15);
    const double vEnd = wall.value(std::span<const double>(seg.q(), 1));
    CHECK(seg.psi() == doctest::Approx(std::sqrt(2.0 * (vEnd + seg.params().eps0))).epsilon(2e-3));
}

TEST_CASE("A3-R4 the relaxation can be disabled per segment") {
    SavSegment<double> seg;
    pme::nl::SavParams p;
    p.driftRelaxation = false;
    REQUIRE(seg.configure(1, 0, 1, kFs, p) == pme::Status::Ok);
    REQUIRE(seg.setLumped(0, 0.01) == pme::Status::Ok);
    const WallPotential<double> wall(0, 0.0, 1.0, 1e5, 1.0);
    const std::array<const Potential<double>*, 1> pots{&wall};
    const std::array<double, 1> c{1.0};
    std::array<Dashpot<double>, 1> dash{{{std::span<const double>(c), 3.0, true}}};
    const std::array<double, 1> f{10.0};
    seg.q()[0] = 1e-4;
    seg.initialisePsi(pots);
    seg.setPsi(1.05 * seg.psi());
    for (int s = 0; s < 6000; ++s) {
        pme::nl::savStep<double>(seg, pots, dash, f);
    }
    CHECK(seg.counters().driftCorrections == 0U);
    CHECK(seg.contactSamples() > 2400U);
}

TEST_CASE("A3-R2 several potentials add and share one psi; a dashpot with no contact still acts") {
    SavSegment<double> seg;
    REQUIRE(seg.configure(2, 0, 2, kFs) == pme::Status::Ok);
    for (std::size_t i = 0; i < 2; ++i) {
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(2.0 * std::numbers::pi * (300.0 + 100.0 * static_cast<double>(i)), 0.0,
                                               kFs, c) == pme::Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, 1.0) == pme::Status::Ok);
    }
    seg.q()[0] = 1e-3;
    seg.q()[1] = -5e-4;
    // V = 0 (no contact) but a damper-like dashpot with R > 0: not skipped, energy decreases.
    const WallPotential<double> farWall(0, 10.0, 1.0, 1e6, 2.0);
    const std::array<const Potential<double>*, 1> pots{&farWall};
    const std::array<double, 2> c{1.0, 1.0};
    std::array<Dashpot<double>, 1> dash{{{std::span<const double>(c), 2.0, false}}};
    pme::nl::savStep<double>(seg, pots, dash, {});
    const double e0 = pme::nl::savEnergy(seg);
    for (int s = 0; s < 500; ++s) {
        pme::nl::savStep<double>(seg, pots, dash, {});
    }
    CHECK(seg.counters().solvesSkipped == 0U);
    CHECK(pme::nl::savEnergy(seg) < e0);
}
