// SPDX-License-Identifier: MIT
// Covers: A1-R3
// Scalar difference-form update of 64, 512 and 4096 modes; work unit "mode-samples". Nightly
// (perf).
#include <cstddef>
#include <numbers>
#include <vector>

#include "pme/bench/bench.hpp"
#include "pme/osc.hpp"
#include "pme/osc/mode_bank.hpp"

namespace {

constexpr double kFs = 48000.0;
constexpr int kSamplesPerCall = 64;

struct Fixture {
    pme::osc::ModeBank bank;
    std::vector<float> force;
    explicit Fixture(std::size_t n) : force(n, 0.001F) {
        bank.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            pme::OscCoeffs c;
            const double f = 50.0 + 15000.0 * static_cast<double>(i) / static_cast<double>(n);
            if (pme::compileOscillator(2.0 * std::numbers::pi * f, 1.0, kFs, c) ==
                pme::Status::Ok) {
                bank.setMode(i, c);
            }
            bank.q()[i] = 1.0F;
        }
    }
};

void run(Fixture& fx) {
    for (int s = 0; s < kSamplesPerCall; ++s) {
        fx.bank.step(fx.force);
    }
    pme::bench::do_not_optimize(fx.bank.q()[0]);
}

}  // namespace

PME_BENCHMARK(osc_step_64_modes, "mode-samples", 64.0 * kSamplesPerCall) {
    static Fixture fx(64);
    run(fx);
}

PME_BENCHMARK(osc_step_512_modes, "mode-samples", 512.0 * kSamplesPerCall) {
    static Fixture fx(512);
    run(fx);
}

PME_BENCHMARK(osc_step_4096_modes, "mode-samples", 4096.0 * kSamplesPerCall) {
    static Fixture fx(4096);
    run(fx);
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
