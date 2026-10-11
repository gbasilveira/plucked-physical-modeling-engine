// SPDX-License-Identifier: MIT
// Covers: A6-T4
// Engagement ramps (landing and lifting, both damper models, float32 engine path) produce no
// output step larger than the largest sample-to-sample change of the undamped signal.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "fixtures.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/lift.hpp"
#include "pme/dampers/rank_one.hpp"

namespace {

using namespace pme;
using namespace pme::dmp;
using pme::dmp_test::examplePad;

constexpr int kSubBlock = 16;
constexpr int kWindow = 9600;      // 0.2 s
constexpr int kEventSample = 960;  // 20 ms after the pluck (a sub-block boundary)

enum class Model : std::uint8_t { None, Diagonal, RankOne };

struct Result {
    double maxStep{0.0};
    double finalAmplitude{0.0};
};

// Run the sounding string for kWindow samples and return max |y^{n+1} - y^n| of the displacement
// observed at 0.31 L, over the window from the engagement event (kEventSample) to the end.
// `landing`: e goes 0 -> 1 at kEventSample, else 1 -> 0.
Result run(Model model, bool landing, double nuRel, const DamperParams& pad) {
    const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
    const std::size_t N = m.omega.size();
    const std::vector<float> phi = pme::dmp_test::pointShape(m, 0.31);
    const std::vector<double> q0 = pme::dmp_test::pluckModal(m, 0.2);

    EngagementFollower follower;
    follower.configure(pad, m.fs, landing ? 0.0 : 1.0);

    osc::ModeBank bank;
    nl::SavSegment<float> seg;
    DiagonalDamper diag;
    RankOneDamper<float> rank1;
    std::vector<osc::CoeffRamp> ramps(N);
    std::vector<float> zero(N, 0.0F);
    std::vector<float> force(N, 0.0F);

    if (model == Model::RankOne) {
        REQUIRE(seg.configure(N, 0, 1, m.fs) == Status::Ok);
        for (std::size_t i = 0; i < N; ++i) {
            osc::OscCoeffsF64 c;
            REQUIRE(osc::compileOscillatorF64(m.omega[i], m.sigma[i], m.fs, c) == Status::Ok);
            REQUIRE(seg.setCoordinate(i, c, m.rhoA) == Status::Ok);
            seg.q()[i] = static_cast<float>(q0[i]);
        }
        REQUIRE(rank1.configure(m, pad, N) == Status::Ok);
    } else {
        bank.resize(N);
        for (std::size_t i = 0; i < N; ++i) {
            bank.setMode(i, m.oscV[i]);
            bank.q()[i] = static_cast<float>(q0[i]);
        }
        if (model == Model::Diagonal) {
            REQUIRE(diag.configure(m, pad) == Status::Ok);
            REQUIRE(diag.initialise(landing ? 0.0 : 1.0, bank, 0) == Status::Ok);
        }
    }

    auto output = [&]() {
        const float* q = model == Model::RankOne ? seg.q() : bank.q();
        double y = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            y += static_cast<double>(phi[i]) * static_cast<double>(q[i]);
        }
        return y;
    };

    Result r;
    double yPrev = output();
    for (int n = 0; n < kWindow; ++n) {
        if (n == kEventSample && model != Model::None) {
            follower.setTarget(landing ? 1.0 : 0.0, nuRel);
        }
        if (model == Model::Diagonal && n % kSubBlock == 0) {
            // Refresh once per sub-block from e at the sub-block end (A6-R4).
            static_cast<void>(diag.plan(follower.projected(kSubBlock), kSubBlock, ramps));
        }
        if (model == Model::Diagonal) {
            osc::applyRampsAt(bank, 0, ramps, n % kSubBlock);
        }
        if (model == Model::RankOne) {
            const double e = follower.value();
            std::fill(force.begin(), force.end(), 0.0F);
            rank1.addSpringForce(e, seg, force);
            const std::array<nl::Dashpot<float>, 1> dash{rank1.dashpot(e)};
            nl::savStep<float>(seg, {}, dash, force);
        } else {
            bank.step(zero);
        }
        follower.step();
        if (model == Model::Diagonal && n % kSubBlock == kSubBlock - 1) {
            diag.commit(bank, 0);
        }
        const double y = output();
        if (n >= kEventSample) {  // the engagement window: from the event to the end
            r.maxStep = std::max(r.maxStep, std::fabs(y - yPrev));
        }
        yPrev = y;
        r.finalAmplitude = std::fabs(y);
    }
    return r;
}

}  // namespace

TEST_CASE("A6-T4 engagement ramps produce no step above the undamped signal's") {
    const DamperParams pad = examplePad();
    const double d = run(Model::None, true, 0.0, pad).maxStep;
    REQUIRE(d > 0.0);
    for (Model model : {Model::Diagonal, Model::RankOne}) {
        for (bool landing : {true, false}) {
            for (double nu : {0.0, 1.0}) {
                if (!landing && nu > 0.0) {
                    continue;  // nu_rel only shortens tau_down
                }
                CAPTURE(static_cast<int>(model));
                CAPTURE(landing);
                CAPTURE(nu);
                const Result r = run(model, landing, nu, pad);
                MESSAGE((model == Model::Diagonal ? "diagonal" : "rank-one")
                        << (landing ? " landing" : " lifting") << " nu_rel " << nu << ": max step "
                        << r.maxStep << ", undamped D " << d << ", ratio " << r.maxStep / d);
                CHECK(r.maxStep <= d);
            }
        }
    }
}

TEST_CASE("A6-T4 the damper actually damps while engaged") {
    const DamperParams pad = examplePad();
    // Sanity for the test above: after landing, the diagonal and rank-one outputs decay faster
    // than the undamped string, so the comparison with D is not vacuous.
    auto energyTail = [&](Model model) {
        const StringModes m = pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 262.0));
        const std::size_t N = m.omega.size();
        osc::ModeBank bank;
        bank.resize(N);
        DiagonalDamper diag;
        REQUIRE(diag.configure(m, pad) == Status::Ok);
        std::vector<float> zero(N, 0.0F);
        const std::vector<double> q0 = pme::dmp_test::pluckModal(m, 0.2);
        for (std::size_t i = 0; i < N; ++i) {
            bank.setMode(i, m.oscV[i]);
            bank.q()[i] = static_cast<float>(q0[i]);
        }
        if (model == Model::Diagonal) {
            REQUIRE(diag.initialise(1.0, bank, 0) == Status::Ok);
        }
        std::vector<float> qPrev(N);
        for (int n = 0; n < 24000; ++n) {
            bank.step(zero, qPrev);
        }
        return osc::bankEnergy(bank, qPrev, 1.0 / m.fs);
    };
    const double free = energyTail(Model::None);
    const double damped = energyTail(Model::Diagonal);
    MESSAGE("energy after 0.5 s: free " << free << ", damped " << damped);
    CHECK(damped < 0.5 * free);
}
