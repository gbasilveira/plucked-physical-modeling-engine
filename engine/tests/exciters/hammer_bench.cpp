// SPDX-License-Identifier: MIT
// Covers: A5-R2, A5-R3
// A trichord strike (3 x 60 modes plus the hammer, r = 4) in float32, 64 samples per call, with
// and without a bridge point, and the skip path out of contact. Work unit "coordinate-samples".
// Nightly (perf); no baseline file until V2 and decision D-4.
#include <cstddef>
#include <memory>
#include <vector>

#include "pme/bench/bench.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/exciters/hammer.hpp"
#include "test_support.hpp"

namespace {

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

constexpr int kSamplesPerCall = 64;

struct Fixture {
    std::vector<StringModes> modes;
    bridge::BridgePoint<float> point;
    StruckNote<float> note;
    double coordinates{0.0};

    // `contact`: hold the hammer in contact (it is re-launched when it has left); otherwise the
    // hammer is never launched and the segment takes the skip path.
    Fixture(bool contact, bool withBridge) {
        modes = unison(3, false, kFs, 16000.0);
        // 60 modes per string: raise fMax until every string keeps at least 60 modes.
        StruckNoteDesc d = describe(modes, pianoHammer());
        static_cast<void>(configureBridge(point));
        static_cast<void>(note.configure(d, kFs, withBridge ? &point : nullptr));
        coordinates = static_cast<double>(note.segment().size());
        contactMode = contact;
        if (contact) {
            static_cast<void>(note.launch(0.6, 0.0));
        }
        // Prime a few samples so that the strings move.
        for (int i = 0; i < 40; ++i) {
            note.step();
        }
    }

    void run() {
        for (int s = 0; s < kSamplesPerCall; ++s) {
            if (contactMode && !note.inFlight()) {
                static_cast<void>(note.launch(0.6, 0.0));  // keep the hammer striking
            }
            note.step();
        }
        pme::bench::do_not_optimize(note.segment().q()[0]);
    }

    bool contactMode{false};
};

}  // namespace

PME_BENCHMARK(hammer_trichord_contact_bridge, "coordinate-samples", 181.0 * kSamplesPerCall) {
    static Fixture fx(true, true);
    fx.run();
}

PME_BENCHMARK(hammer_trichord_contact_no_bridge, "coordinate-samples", 181.0 * kSamplesPerCall) {
    static Fixture fx(true, false);
    fx.run();
}

PME_BENCHMARK(hammer_trichord_skip_bridge, "coordinate-samples", 181.0 * kSamplesPerCall) {
    static Fixture fx(false, true);
    fx.run();
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
