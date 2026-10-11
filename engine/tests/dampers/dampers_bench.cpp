// SPDX-License-Identifier: MIT
// Covers: A6-R1, A6-R2
// Coefficient refresh of 80 modes per sub-block (diagonal damper) and one rank-one damper step on
// 80 modes (float32). Nightly (perf). No baseline file until V2 and decision D-4.
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "fixtures.hpp"
#include "pme/bench/bench.hpp"
#include "pme/dampers/controller.hpp"
#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/rank_one.hpp"

namespace {

using namespace pme;
using namespace pme::dmp;

constexpr int kSubBlock = 16;

// A string with about 80 modes: f1 = 190 Hz up to 16 kHz.
StringModes eightyModes() { return pme::dmp_test::compile(pme::dmp_test::plainSteel(0.65, 190.0)); }

struct Refresh {
    StringModes m{eightyModes()};
    DiagonalDamper d;
    osc::ModeBank bank;
    std::vector<osc::CoeffRamp> ramps;
    std::vector<float> force;
    double e{0.0};
    double dir{0.02};
    Refresh() {
        static_cast<void>(d.configure(m, pme::dmp_test::examplePad()));
        bank.resize(m.omega.size());
        static_cast<void>(d.initialise(0.0, bank, 0));
        ramps.resize(m.omega.size());
        force.assign(m.omega.size(), 0.0F);
    }
    // One sub-block while e moves: plan (compile + ramp plan per mode), 16 ramp writes, commit.
    void block() {
        e += dir;
        if (e >= 1.0 || e <= 0.0) {
            dir = -dir;
        }
        if (d.plan(e, kSubBlock, ramps)) {
            for (int s = 0; s < kSubBlock; ++s) {
                osc::applyRampsAt(bank, 0, ramps, s);
            }
            d.commit(bank, 0);
        }
        pme::bench::do_not_optimize(bank.beta()[0]);
    }
};

struct Step {
    StringModes m{eightyModes()};
    nl::SavSegment<float> seg;
    RankOneDamper<float> damper;
    std::vector<float> force;
    Step() {
        const std::size_t n = m.omega.size();
        static_cast<void>(seg.configure(n, 0, 1, m.fs));
        for (std::size_t i = 0; i < n; ++i) {
            osc::OscCoeffsF64 c;
            static_cast<void>(osc::compileOscillatorF64(m.omega[i], m.sigma[i], m.fs, c));
            static_cast<void>(seg.setCoordinate(i, c, m.rhoA));
            seg.q()[i] = 1e-5F;
        }
        static_cast<void>(damper.configure(m, pme::dmp_test::examplePad(), n));
        force.assign(n, 0.0F);
    }
    void run() {
        const std::array<const nl::Potential<float>*, 0> none{};
        for (int s = 0; s < 64; ++s) {
            std::fill(force.begin(), force.end(), 0.0F);
            damper.addSpringForce(1.0, seg, force);
            const std::array<nl::Dashpot<float>, 1> dash{damper.dashpot(1.0)};
            nl::savStep<float>(seg, none, dash, force);
        }
        pme::bench::do_not_optimize(seg.q()[0]);
    }
};

}  // namespace

PME_BENCHMARK(damper_refresh_80_modes_per_subblock, "modes", 80.0) {
    static Refresh fx;
    fx.block();
}

PME_BENCHMARK(damper_rank_one_step_80_modes_x64, "mode-samples", 80.0 * 64.0) {
    static Step fx;
    fx.run();
}

PME_BENCHMARK(damper_controller_advance_moving_keys, "keys", 88.0) {
    static DamperController c = [] {
        DamperController ctl;
        static_cast<void>(ctl.configure(pme::dmp_test::kFs, 0.35, 0.65));
        for (int k = 21; k < 109; ++k) {
            static_cast<void>(ctl.setKey(k, pme::dmp_test::examplePad()));
        }
        return ctl;
    }();
    static int phase = 0;
    if (phase++ % 4000 == 0) {
        c.setSustain((phase / 4000) % 2 == 0 ? 0.0 : 1.0);
    }
    pme::bench::do_not_optimize(c.advance());
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
