// SPDX-License-Identifier: MIT
// Covers: A1-R3, A1-R4, A1-R5, A1-R6
// Real-time safety of the audio-thread code of the osc module: under the rt-safety preset any
// allocation or blocking call inside the scope aborts the process.
#include <doctest/doctest.h>

#include <array>
#include <numbers>
#include <vector>

#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/ramp.hpp"
#include "pme/rtcheck/rtcheck.hpp"

TEST_CASE("A1-R3 the update, derived quantities, energy and ramps allocate nothing") {
    constexpr std::size_t n = 64;
    constexpr double fs = 48000.0;
    pme::osc::ModeBank bank;
    bank.resize(n);
    pme::OscCoeffs c0;
    pme::OscCoeffs c1;
    REQUIRE(pme::compileOscillator(2.0 * std::numbers::pi * 220.0, 2.0, fs, c0) == pme::Status::Ok);
    REQUIRE(pme::compileOscillator(2.0 * std::numbers::pi * 230.0, 2.0, fs, c1) == pme::Status::Ok);
    for (std::size_t i = 0; i < n; ++i) {
        bank.setMode(i, c0);
        bank.q()[i] = 1.0F;
    }
    std::vector<float> force(n, 0.1F);
    std::vector<float> qPrev(n);
    std::vector<float> uPrev(n);
    std::array<pme::osc::CoeffRamp, 4> ramps{};
    for (auto& r : ramps) {
        r = pme::osc::planRamp(c0, 2.0 * std::numbers::pi * 220.0, 2.0, c1,
                               2.0 * std::numbers::pi * 230.0, 2.0, 16);
    }
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int s = 0; s < 16; ++s) {
            pme::osc::applyRampsAt(bank, 0, ramps, s);
            bank.step(force, qPrev, uPrev);
            sink += pme::osc::bankEnergy(bank, qPrev, 1.0 / fs);
            sink += pme::osc::bankEnergyBalance(bank, force, uPrev, 1.0 / fs);
            sink += pme::osc::centredVelocity(bank.u()[0], uPrev[0], 1.0 / fs);
            sink += pme::osc::centredAcceleration(bank.u()[0], uPrev[0], 1.0 / fs);
            sink += pme::osc::halfStepVelocity(bank.u()[0], 1.0 / fs);
        }
    }
    CHECK(sink != 0.0);
}
