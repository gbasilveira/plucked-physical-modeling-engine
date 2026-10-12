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
    bool contactMode;
    bridge::BridgePoint<float> point;
    StruckNote<float> note;

    // `contact`: hold the hammer in contact (it is re-launched when it has left); otherwise the
    // hammer has left and the segment takes the skip path.
    Fixture(bool contact, bool withBridge) : contactMode(contact) {
        modes = unison(3, false, kFs, 16000.0);
        // fMax 16 kHz keeps 61 modes per string: 3 x 61 + 1 = 184 coordinates.
        StruckNoteDesc d = describe(modes, pianoHammer());
        static_cast<void>(configureBridge(point));
        static_cast<void>(note.configure(d, kFs, withBridge ? &point : nullptr));
        static_cast<void>(note.launch(0.6, 0.0));
        if (contact) {
            for (int i = 0; i < 40; ++i) {
                note.step();  // the strings move and the hammer is in contact
            }
        } else {
            for (int i = 0; i < 5000 && note.inFlight(); ++i) {
                note.step();  // run until the hammer has left: ringing strings, skip path
            }
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
};

}  // namespace

PME_BENCHMARK(hammer_trichord_contact_bridge, "coordinate-samples", 184.0 * kSamplesPerCall) {
    static Fixture fx(true, true);
    fx.run();
}

PME_BENCHMARK(hammer_trichord_contact_no_bridge, "coordinate-samples", 184.0 * kSamplesPerCall) {
    static Fixture fx(true, false);
    fx.run();
}

PME_BENCHMARK(hammer_trichord_skip_bridge, "coordinate-samples", 184.0 * kSamplesPerCall) {
    static Fixture fx(false, true);
    fx.run();
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
