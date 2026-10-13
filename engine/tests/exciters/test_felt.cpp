// SPDX-License-Identifier: MIT
// Covers: A5-R3
// Felt dissipation: Hunt-Crossley energy balance for one, two and three strings, the solve rank
// (r = 1 for one string, r = 4 for a trichord), the dashpot budget with dampers, and the Stulov
// hereditary balance including the work of the explicit force.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

namespace {

struct Balance {
    double e0{0.0};
    double eEnd{0.0};
    double worstError{0.0};     // max |E^{n+1} - E^n + dissipation - work| / E0
    double worstIncrease{0.0};  // max (E^{n+1} - E^n - work) / E0 (positive = energy created)
    double totalWork{0.0};      // hereditary work over the walk / E0
    double resetLoss{0.0};      // energy removed by the reset-at-separation rule / E0
    std::size_t maxRank{0};
    std::size_t contactSteps{0};
};

std::size_t rankOf(double v, std::span<const nl::Dashpot<double>> dashpots) {
    std::size_t r = v > 0.0 ? 1 : 0;
    for (const auto& d : dashpots) {
        if (d.R > 0.0 && !(v > 0.0 && d.alongGradient)) {
            ++r;
        }
    }
    return r;
}

// Strike a note with `nu` and walk `steps` samples, checking the energy balance of every step.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): nu, shift
Balance walk(StruckNote<double>& note, double nu, double shift, int steps) {
    Balance b;
    REQUIRE(note.launch(nu, shift) == Status::Ok);
    b.e0 = note.energy();
    double ePrev = b.e0;
    double releasedPrev = 0.0;
    for (int n = 0; n < steps; ++n) {
        const std::span<const double> z(note.segment().q(), note.segment().size());
        const double v = note.potential().value(z);
        const std::uint32_t resets = note.segment().counters().resets;
        note.step();
        const double e = note.energy();
        const double diss = nl::savDissipation<double>(note.segment(), note.felt().dashpots());
        const double work = note.felt().hereditaryWork(note.segment());
        b.totalWork += work / b.e0;
        const double released = note.releasedEnergy();
        ePrev -= released - releasedPrev;  // the release carries the hammer's energy away
        releasedPrev = released;
        if (note.segment().counters().resets == resets) {  // a reset only removes energy
            const double err = std::abs(e - ePrev + diss - work) / b.e0;
            b.worstError = std::max(b.worstError, err);
            b.worstIncrease = std::max(b.worstIncrease, (e - ePrev - work) / b.e0);
        } else {
            b.resetLoss += (ePrev - e + work - diss) / b.e0;
        }
        b.maxRank = std::max(b.maxRank, rankOf(v, note.felt().dashpots()));
        b.contactSteps += v > 0.0 ? 1 : 0;
        ePrev = e;
    }
    b.eEnd = ePrev;
    return b;
}

}  // namespace

TEST_CASE("A5-R3 Hunt-Crossley energy balance closes per step for one two and three strings") {
    for (const std::size_t ns : {std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        const std::vector<StringModes> modes = unison(ns, true);
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        const Balance b = walk(note, 0.8, 0.0, 800);
        MESSAGE(ns << " string(s): contact steps " << b.contactSteps << " worst balance error "
                   << sci(b.worstError) << " worst increase " << sci(b.worstIncrease)
                   << " E_end/E_0 " << sci(b.eEnd / b.e0, 6));
        CHECK(b.contactSteps > 10);
        CHECK(b.worstError <= 1e-12);
        CHECK(b.worstIncrease <= 1e-12);  // non-increasing within roundoff
        CHECK(b.eEnd < b.e0);             // the felt dissipated energy
    }
}

TEST_CASE("A5-R3 the solve rank is 1 for one string and 4 for a trichord") {
    {
        const std::vector<StringModes> modes = unison(1, true);
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        const Balance b = walk(note, 0.8, 0.0, 400);
        CHECK(b.maxRank == 1);  // the dashpot is flagged alongGradient
        CHECK(note.felt().dashpotCount() == 1);
    }
    {
        const std::vector<StringModes> modes = unison(3, true);
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        const Balance b = walk(note, 0.8, 0.0, 400);
        CHECK(b.maxRank == 4);  // 1 SAV + 3 felt dashpots: A3's maximum
        CHECK(note.felt().dashpotCount() == 3);
    }
    {
        // Una corda at s = 1: the leaving string has weight 0, so one bichord string carries the
        // contact and its dashpot is again parallel to g.
        const std::vector<StringModes> modes = unison(2, true);
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, pianoHammer()), kFs, nullptr) == Status::Ok);
        const Balance b = walk(note, 0.8, 1.0, 400);
        CHECK(b.maxRank == 1);
        CHECK(b.worstError <= 1e-12);
    }
}

TEST_CASE("A5-R3 the hammer's dashpots use the whole budget of a trichord") {
    HammerParams hc = pianoHammer();
    CHECK(nl::kMaxDashpots == 3);
    CHECK(hammerDashpotCount(hc, 1) == 1);
    CHECK(hammerDashpotCount(hc, 2) == 2);
    CHECK(hammerDashpotCount(hc, 3) == 3);
    // A rank-one damper needs one dashpot slot (SPEC-NL, SPEC-DMP): none is left for a trichord.
    CHECK(hammerDashpotBudgetLeft(hc, 3) == 0);
    CHECK(hammerDashpotBudgetLeft(hc, 2) == 1);
    CHECK(hammerDashpotBudgetLeft(hc, 1) == 2);
    HammerParams st = pianoHammer();
    st.law = FeltLaw::Stulov;
    st.stulovEps = 0.2;
    st.stulovTau = 5.0e-4;
    CHECK(hammerDashpotCount(st, 3) == 0);
    CHECK(hammerDashpotBudgetLeft(st, 3) == 3);
    // The same arithmetic through a configured note.
    const std::vector<StringModes> modes = unison(3, true);
    StruckNote<double> hcNote;
    REQUIRE(hcNote.configure(describe(modes, hc), kFs, nullptr) == Status::Ok);
    CHECK(hcNote.felt().dashpotCount() + hammerDashpotBudgetLeft(hc, 3) == nl::kMaxDashpots);
    StruckNote<double> stNote;
    REQUIRE(stNote.configure(describe(modes, st), kFs, nullptr) == Status::Ok);
    CHECK(stNote.felt().dashpotCount() == 0);
}

TEST_CASE("A5-R3 Stulov hereditary balance including the work of the explicit force closes") {
    HammerParams h = pianoHammer();
    h.law = FeltLaw::Stulov;
    h.stulovEps = 0.3;
    h.stulovTau = 5.0e-4;
    h.muHC = 0.0;
    for (const std::size_t ns : {std::size_t{1}, std::size_t{3}}) {
        const std::vector<StringModes> modes = unison(ns, true);
        StruckNote<double> note;
        REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
        const Balance b = walk(note, 0.8, 0.0, 800);
        MESSAGE("Stulov " << ns << " string(s): contact steps " << b.contactSteps
                          << " worst balance error " << sci(b.worstError)
                          << " hereditary work/E_0 = " << sci(b.totalWork, 6)
                          << " (E_end + released - E_0)/E_0 = "
                          << sci((b.eEnd + note.releasedEnergy() - b.e0) / b.e0, 6));
        CHECK(b.resetLoss >= -1e-14);  // SPEC-NL: the reset only removes energy
        CHECK(note.resetEnergyRemoved() / b.e0 == doctest::Approx(b.resetLoss).epsilon(1e-6));
        CHECK(std::abs((b.eEnd + note.releasedEnergy() - b.e0) / b.e0 - b.totalWork +
                       b.resetLoss) <= 1e-12);
        CHECK(b.contactSteps > 10);
        CHECK(b.worstError <= 1e-12);
        CHECK(std::isfinite(b.eEnd));
        CHECK(note.felt().hereditary(0) >= 0.0);
    }
}

TEST_CASE("A5-R3 Stulov with no hereditary amplitude reduces to the elastic felt") {
    HammerParams elastic = pianoHammer();
    elastic.muHC = 0.0;
    HammerParams stulov = elastic;
    stulov.law = FeltLaw::Stulov;
    stulov.stulovEps = 0.0;
    stulov.stulovTau = 1.0e-3;
    const std::vector<StringModes> modes = unison(1, true);
    StruckNote<double> a;
    StruckNote<double> b;
    REQUIRE(a.configure(describe(modes, elastic), kFs, nullptr) == Status::Ok);
    REQUIRE(b.configure(describe(modes, stulov), kFs, nullptr) == Status::Ok);
    REQUIRE(a.launch(0.6, 0.0) == Status::Ok);
    REQUIRE(b.launch(0.6, 0.0) == Status::Ok);
    for (int n = 0; n < 300; ++n) {
        a.step();
        b.step();
    }
    for (std::size_t i = 0; i < a.segment().size(); ++i) {
        CHECK(a.segment().q()[i] == b.segment().q()[i]);
    }
}

// SPEC-HAM Stulov: the felt force is K (delta^p - eps h), so the hereditary term removes energy
// (a bounce loses energy) and the felt never pulls. Regression for a sign error that made the
// term stiffen the felt and add energy (rebound/incoming 1.36 instead of 0.60 on a rigid wall).
TEST_CASE("A5-R3 Stulov hereditary term dissipates and the felt force is never negative") {
    for (const std::size_t ns : {std::size_t{1}, std::size_t{3}}) {
        for (const double eps : {0.0, 0.3}) {
            HammerParams h = pianoHammer();
            h.law = FeltLaw::Stulov;
            h.stulovEps = eps;
            h.stulovTau = 5.0e-4;
            h.muHC = 0.0;
            const std::vector<StringModes> modes = unison(ns, true);
            StruckNote<double> note;
            REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
            REQUIRE(note.launch(0.8, 0.0) == Status::Ok);
            const double e0 = note.energy();
            double work = 0.0;
            double minForce = 0.0;
            for (int n = 0; n < 800; ++n) {
                const std::span<const double> z(note.segment().q(), note.segment().size());
                std::array<double, 3> dp{};
                for (std::size_t s = 0; s < ns; ++s) {
                    const double d = note.potential().penetration(z, s);
                    dp[s] = d > 0.0 ? std::pow(d, note.potential().map().exponent[s]) : -1.0;
                }
                note.step();
                work += note.felt().hereditaryWork(note.segment());
                for (std::size_t s = 0; s < ns; ++s) {
                    if (dp[s] >= 0.0) {
                        const double wk =
                            note.potential().map().weight[s] * note.potential().map().stiffness[s];
                        const double total = wk * dp[s] - note.felt().hereditaryForce(s);
                        minForce = std::min(minForce, total / std::max(wk * dp[s], 1e-300));
                        minForce = std::min(minForce, note.felt().hereditaryForce(s));
                    }
                }
            }
            MESSAGE("Stulov " << ns << " string(s), eps " << eps
                              << ": hereditary work/E_0 = " << sci(work / e0)
                              << ", min felt force/(K delta^p) = " << sci(minForce));
            if (eps > 0.0) {
                CHECK(work / e0 < 0.0);  // dissipates: rebound energy < incoming energy
            } else {
                CHECK(std::abs(work / e0) <= 1e-14);
            }
            CHECK(minForce >= -1e-12);  // felt cannot pull
        }
    }
}

namespace {

// Rigid-wall bounce of an 8 g mass with a Stulov felt: K = 4e9, p = 2.5, tau = 0.5 ms, 3 m/s.
// The hereditary force follows SPEC-HAM: F = +K min(eps h, delta^p) on x_H, so that the felt
// force is K max(0, delta^p - eps h). Returns rebound kinetic energy / incoming kinetic energy.
double rigidWallStulovBounce(double eps, double& minFeltForce) {
    const double fs = 48000.0;
    const double dt = 1.0 / fs;
    const double K = 4.0e9;
    const double p = 2.5;
    const double v = 3.0;
    const double a = std::exp(-dt / 5.0e-4);
    nl::SavSegment<double> seg;
    REQUIRE(seg.configure(0, 1, 0, fs) == Status::Ok);
    REQUIRE(seg.setLumped(0, 8.0e-3) == Status::Ok);
    const RigidFeltPotential<double> pot(K, p, 0);
    const std::array<const nl::Potential<double>*, 1> pots{&pot};
    seg.q()[0] = -v * dt;
    seg.u()[0] = v * dt;
    double h = 0.0;
    minFeltForce = 0.0;
    std::array<double, 1> f{};
    for (int n = 0; n < 20000; ++n) {
        const double d = seg.q()[0];
        const double dp = d > 0.0 ? std::pow(d, p) : 0.0;
        h = a * h + (1.0 - a) * dp;
        const double hered = d > 0.0 ? K * std::min(eps * h, dp) : 0.0;
        minFeltForce = std::min(minFeltForce, K * dp - hered);
        f[0] = hered;
        nl::savStep<double>(seg, pots, {}, std::span<const double>(f));
        if (n > 50 && seg.q()[0] < -v * dt * 50.0) {
            break;
        }
    }
    const double vOut = seg.u()[0] / dt;
    return (vOut * vOut) / (v * v);
}

}  // namespace

TEST_CASE("A5-R3 Stulov rigid-wall bounce loses energy with eps > 0 and conserves it at eps = 0") {
    double minForce = 0.0;
    const double conserved = rigidWallStulovBounce(0.0, minForce);
    CHECK(conserved == doctest::Approx(1.0).epsilon(1e-3));
    CHECK(minForce >= 0.0);
    const double lossy = rigidWallStulovBounce(0.3, minForce);
    MESSAGE("rebound KE / incoming KE: eps 0 " << sci(conserved, 4) << ", eps 0.3 "
                                               << sci(lossy, 4));
    // With the no-pull clamp (SPEC-HAM): 0.7187 at 48 kHz, converging to 0.717; the unclamped
    // force K (delta^p - eps h), which can pull at unloading, would give 0.604. The wrong sign,
    // K (delta^p + eps h), gave 1.358.
    CHECK(lossy < 1.0);
    CHECK(lossy == doctest::Approx(0.7187).epsilon(0.01));
    CHECK(minForce >= 0.0);  // felt cannot pull
}
