// SPDX-License-Identifier: MIT
// Covers: A6-R1
// Overdamping check for every damper of instruments/examples/generic-grand-88 at full engagement.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "pme/dampers/diagonal.hpp"
#include "pme/dampers/rank_one.hpp"
#include "pme/loader/instrument_file.hpp"
#include "pme/string/string_model.hpp"

TEST_CASE("A6-R1 no damper of generic-grand-88 overdamps a mode at e = 1") {
    using namespace pme;
    loader::InstrumentFile file;
    std::vector<loader::LoadError> errors;
    const std::filesystem::path path =
        std::filesystem::path(PME_A6_EXAMPLES) / "generic-grand-88" / "instrument.json";
    REQUIRE(loader::loadInstrumentFile(path, false, file, errors) == Status::Ok);

    constexpr double kFs = 48000.0;
    constexpr double kFMax = 16000.0;
    int dampers = 0;
    int rejected = 0;
    double worst = 0.0;
    int worstKey = -1;
    std::size_t worstMode = 0;
    std::size_t fewestModes = 1000000;
    for (const NoteGroupDesc& group : file.desc.groups) {
        for (const SegmentDesc& seg : group.segments) {
            if (!seg.damper.has_value() || !seg.damper->hasDamper) {
                continue;
            }
            StringParams sp = seg.string;
            if (group.key >= 0) {
                sp.f1Target = string::tuningF1(file.desc.tuning, group.key, seg.unisonDetuneCents);
            }
            StringModes modes;
            REQUIRE(compileString(sp, kFs, kFMax, {}, modes) == Status::Ok);
            // Diagonal analysis for every damper, whatever its configured mode.
            const dmp::OverdampingReport r = dmp::DiagonalDamper::analyse(modes, *seg.damper);
            ++dampers;
            if (r.status != Status::Ok) {
                ++rejected;
                MESSAGE("key " << group.key << ": mode " << (r.rejectedMode + 1)
                               << " rejected at e " << r.rejectedE);
            }
            if (r.worstRatio > worst) {
                worst = r.worstRatio;
                worstKey = group.key;
                worstMode = r.worstMode + 1;
            }
            fewestModes = std::min(fewestModes, modes.omega.size());
            dmp::DiagonalDamper d;
            CHECK(d.configure(modes, *seg.damper) == Status::Ok);
            dmp::RankOneDamper<float> rank1;
            CHECK(rank1.configure(modes, *seg.damper, modes.omega.size()) == Status::Ok);
        }
    }
    MESSAGE("dampers checked " << dampers << ", rejected " << rejected
                               << ", largest sigma_d/omega_d " << worst << " (key " << worstKey
                               << ", mode " << worstMode << "), fewest modes " << fewestModes);
    CHECK(dampers > 0);
    CHECK(rejected == 0);
    CHECK(worst < 1.0);
}
