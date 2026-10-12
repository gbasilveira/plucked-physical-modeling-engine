// SPDX-License-Identifier: MIT
// Covers: A7-R1, A7-R3
// One segment with 60 transverse modes per polarisation, tension modulation and 8 or 16
// longitudinal modes, float32 states, 64 samples per call, rigid bridge; work unit "samples".
// Nightly (perf). No baseline file (D-4).
#include <cstddef>

#include "pme/bench/bench.hpp"
#include "test_support.hpp"

namespace {

constexpr int kSamplesPerCall = 64;

struct Fixture {
    pme::geometric_test::Built<float> built;
    explicit Fixture(int longitudinalModes) {
        // 60 modes: L = 0.65 m, f1 = 262 Hz, fMax = 60.5 f1; 96 kHz so the longitudinal modes
        // (fundamental about 3.9 kHz) stay below the bandwidth limit up to M_L = 16.
        pme::string::GeometricConfig cfg;
        cfg.tensionModulation = true;
        cfg.longitudinalModes = longitudinalModes;
        cfg.kappaL = 0.026;
        cfg.fMax = 80000.0;
        pme::geometric_test::build(built, pme::geometric_test::plainSteel(0.65, 262.0), cfg,
                                   262.0 * 60.5, 96000.0);
        if (built.status == pme::Status::Ok) {
            pme::geometric_test::trianglePluck<float>(built.modes, 4.0e-3, 0.12 * built.modes.L,
                                                      0.3, built.seg.qV(), built.seg.qH());
            built.seg.initialisePsi();
        }
    }
};

}  // namespace

PME_BENCHMARK(geometric_60_modes_tension_8_longitudinal, "samples", kSamplesPerCall) {
    static Fixture fx(8);
    for (int s = 0; s < kSamplesPerCall; ++s) {
        fx.built.seg.advance(0.0F, 0.0F);
    }
    pme::bench::do_not_optimize(fx.built.seg.qV()[0]);
}

PME_BENCHMARK(geometric_60_modes_tension_16_longitudinal, "samples", kSamplesPerCall) {
    static Fixture fx(16);
    for (int s = 0; s < kSamplesPerCall; ++s) {
        fx.built.seg.advance(0.0F, 0.0F);
    }
    pme::bench::do_not_optimize(fx.built.seg.qV()[0]);
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
