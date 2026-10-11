// SPDX-License-Identifier: MIT
// Covers: A3-R2, A3-R3, A3-R5
// Real-time safety of savStep, float32 instantiation: under the rt-safety preset any allocation
// or blocking call inside the scope aborts the process.
#include <doctest/doctest.h>

#include <array>
#include <numbers>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "pme/rtcheck/rtcheck.hpp"
#include "test_support.hpp"

TEST_CASE("A3-R2 savStep with r = 1 and r = 4 allocates nothing") {
    using namespace pme::nl;
    constexpr std::size_t n = 65;
    constexpr double fs = 48000.0;
    SavSegment<float> seg;
    REQUIRE(seg.configure(n - 1, 1, 3, fs) == pme::Status::Ok);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(
                    2.0 * std::numbers::pi * (100.0 + 60.0 * static_cast<double>(i)), 1.0, fs, c) ==
                pme::Status::Ok);
        REQUIRE(seg.setCoordinate(i, c, 1.0) == pme::Status::Ok);
        seg.q()[i] = 1e-4F * static_cast<float>(i % 5);
    }
    REQUIRE(seg.setLumped(n - 1, 0.01) == pme::Status::Ok);
    seg.q()[n - 1] = 2e-3F;

    test::Rng rng(3U);
    std::vector<double> dir(n, 0.0);
    dir[n - 1] = 1.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        dir[i] = -0.01 * rng.uniform();
    }
    const test::QuarticPotential<float> quartic(1e9, dir);
    const std::array<const Potential<float>*, 1> pots{&quartic};
    std::array<std::vector<float>, 3> dirs;
    for (auto& d : dirs) {
        d.resize(n);
        for (float& x : d) {
            x = static_cast<float>(rng.uniform(-1.0, 1.0));
        }
    }
    std::array<Dashpot<float>, 1> r1{{{std::span<const float>(dirs[0]), 0.1, false}}};
    r1[0].R = 0.0;  // r = 1: SAV only
    std::array<Dashpot<float>, 3> r4{{{std::span<const float>(dirs[0]), 0.1, false},
                                      {std::span<const float>(dirs[1]), 0.2, false},
                                      {std::span<const float>(dirs[2]), 0.3, false}}};
    std::vector<float> force(n, 0.01F);
    seg.initialisePsi(pots);
    double sink = 0.0;
    {
        pme::rtcheck::ScopedRealtimeThread realtime;
        for (int s = 0; s < 64; ++s) {
            savStep<float>(seg, pots, r1, force);
            sink += savEnergy(seg);
        }
        for (int s = 0; s < 64; ++s) {
            savStep<float>(seg, pots, r4, force);
            sink += savEnergy(seg) + savDissipation<float>(seg, r4);
        }
        const std::array<const Potential<float>*, 0> none{};
        r4[0].R = 0.0;
        r4[1].R = 0.0;
        r4[2].R = 0.0;
        savStep<float>(seg, none, r4, force);  // skip path
    }
    CHECK(sink != 0.0);
}
