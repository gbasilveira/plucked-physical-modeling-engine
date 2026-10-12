// SPDX-License-Identifier: MIT
// Covers: A4-R2
// Three segments of up to 60 modes (both polarisations) on one bridge point with a 16-mode modal
// set, float32 states, 48 kHz; work unit "samples". Nightly (perf). No baseline file (D-4).
#include <cstddef>

#include "pme/bench/bench.hpp"
#include "test_support.hpp"

namespace {

constexpr int kSamplesPerCall = 64;

struct Fixture {
    pme::bridge_test::Rig<float> rig;
    Fixture() { static_cast<void>(pme::bridge_test::buildThreeSegmentRig(rig)); }
};

}  // namespace

PME_BENCHMARK(bridge_point_3_segments_16_modes, "samples", kSamplesPerCall) {
    static Fixture fx;
    for (int s = 0; s < kSamplesPerCall; ++s) {
        fx.rig.step();
    }
    pme::bench::do_not_optimize(fx.rig.point.wV());
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
