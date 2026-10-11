// SPDX-License-Identifier: MIT
// Covers: A3-R2, A3-R3
// savStep (float32) for n_z = 65 and 257 with r = 1 and r = 4, and the skip path; work unit
// "coordinate-samples". Nightly (perf).
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <numbers>
#include <vector>

#include "pme/bench/bench.hpp"
#include "pme/nonlinear/sav.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::nl;

constexpr double kFs = 48000.0;
constexpr int kSamplesPerCall = 64;

struct Fixture {
    SavSegment<float> seg;
    std::unique_ptr<test::QuarticPotential<float>> quartic;
    std::vector<std::vector<float>> dirs;
    std::array<Dashpot<float>, 3> dash{};
    std::array<const Potential<float>*, 1> pots{};
    std::vector<float> force;
    std::size_t n;

    Fixture(std::size_t nz, bool contact) : force(nz, 0.001F), n(nz) {
        static_cast<void>(seg.configure(nz - 1, 1, 3, kFs));
        test::Rng rng(1U);
        for (std::size_t i = 0; i + 1 < nz; ++i) {
            pme::osc::OscCoeffsF64 c;
            static_cast<void>(pme::osc::compileOscillatorF64(
                2.0 * std::numbers::pi *
                    (60.0 + 8000.0 * static_cast<double>(i) / static_cast<double>(nz)),
                1.0, kFs, c));
            static_cast<void>(seg.setCoordinate(i, c, 1.0));
            seg.q()[i] = 1e-5F;
        }
        static_cast<void>(seg.setLumped(nz - 1, 0.01));
        std::vector<double> dir(nz, 0.0);
        dir[nz - 1] = 1.0;
        for (std::size_t i = 0; i + 1 < nz; ++i) {
            dir[i] = -1e-3 * rng.uniform();
        }
        // Contact: the lumped coordinate sits inside a quartic well; no contact: far from it.
        quartic = std::make_unique<test::QuarticPotential<float>>(1e8, dir);
        if (contact) {
            seg.q()[nz - 1] = 1e-2F;
        } else {
            for (std::size_t i = 0; i < nz; ++i) {
                seg.q()[i] = 0.0F;  // V = 0 exactly: the skip rule applies
            }
            std::fill(force.begin(), force.end(), 0.0F);
        }
        pots[0] = quartic.get();
        dirs.resize(3);
        for (std::size_t j = 0; j < 3; ++j) {
            dirs[j].resize(nz);
            for (float& x : dirs[j]) {
                x = static_cast<float>(rng.uniform(-1.0, 1.0));
            }
            dash[j].c = std::span<const float>(dirs[j]);
            dash[j].R = 0.0;
        }
        seg.initialisePsi(pots);
    }

    void setDashpots(std::size_t count) {
        for (std::size_t j = 0; j < 3; ++j) {
            dash[j].R = j < count ? 0.05 : 0.0;
        }
    }

    void run(std::span<const Potential<float>* const> p) {
        for (int s = 0; s < kSamplesPerCall; ++s) {
            savStep<float>(seg, p, dash, force);
        }
        pme::bench::do_not_optimize(seg.q()[0]);
    }
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): size and rank
Fixture& fixture(std::size_t nz, std::size_t dashpots, bool contact) {
    static std::vector<std::unique_ptr<Fixture>> all;
    all.push_back(std::make_unique<Fixture>(nz, contact));
    all.back()->setDashpots(dashpots);
    return *all.back();
}

}  // namespace

PME_BENCHMARK(sav_step_65_r1, "coordinate-samples", 65.0 * kSamplesPerCall) {
    static Fixture& fx = fixture(65, 0, true);
    fx.run(fx.pots);
}

PME_BENCHMARK(sav_step_65_r4, "coordinate-samples", 65.0 * kSamplesPerCall) {
    static Fixture& fx = fixture(65, 3, true);
    fx.run(fx.pots);
}

PME_BENCHMARK(sav_step_257_r1, "coordinate-samples", 257.0 * kSamplesPerCall) {
    static Fixture& fx = fixture(257, 0, true);
    fx.run(fx.pots);
}

PME_BENCHMARK(sav_step_257_r4, "coordinate-samples", 257.0 * kSamplesPerCall) {
    static Fixture& fx = fixture(257, 3, true);
    fx.run(fx.pots);
}

PME_BENCHMARK(sav_step_257_skip, "coordinate-samples", 257.0 * kSamplesPerCall) {
    static Fixture& fx = fixture(257, 0, false);
    fx.run(fx.pots);
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
