// SPDX-License-Identifier: MIT
// Covers: B1-R1, B1-R3, B1-R4, B1-R6
// B1-T1, C++ half: load each committed file, write it back and compare byte for byte with the
// file Python wrote; check every member of the all-fields instrument against hand-written expected
// values (this proves the C++ structures mirror the file).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "pme/loader/instrument_file.hpp"
#include "test_support.hpp"

namespace {

using nlohmann::json;
using pme::loader::InstrumentFile;
using pme::loader::LoadError;

std::string describeErrors(const std::vector<LoadError>& errors) {
    std::string text;
    for (const LoadError& e : errors) {
        text += e.path + " " + e.code + " " + e.message + "\n";
    }
    return text;
}

// ---- hand-written expected tree of every struct (independent of the generated reader) ----------

json expected(const pme::Point& p) { return {{"x", p.x}, {"y", p.y}, {"z", p.z}}; }

const char* name(pme::Boundary v) { return v == pme::Boundary::Pinned ? "Pinned" : "Clamped"; }
const char* name(pme::BridgeEnd v) { return v == pme::BridgeEnd::AtL ? "AtL" : "At0"; }
const char* name(pme::FeltLaw v) { return v == pme::FeltLaw::HuntCrossley ? "HuntCrossley" : "Stulov"; }
const char* name(pme::DamperMode v) { return v == pme::DamperMode::Diagonal ? "Diagonal" : "RankOne"; }
const char* name(pme::BarrierKind v) {
    switch (v) {
        case pme::BarrierKind::Custom:
            return "Custom";
        case pme::BarrierKind::Fret:
            return "Fret";
        case pme::BarrierKind::Fretboard:
            return "Fretboard";
        case pme::BarrierKind::Jawari:
            return "Jawari";
        case pme::BarrierKind::Thread:
            return "Thread";
        case pme::BarrierKind::Sawari:
            return "Sawari";
    }
    return "?";
}
const char* name(pme::BodySource v) {
    switch (v) {
        case pme::BodySource::PlateModel:
            return "PlateModel";
        case pme::BodySource::ModalTable:
            return "ModalTable";
        case pme::BodySource::MeasuredMobilityFit:
            return "MeasuredMobilityFit";
    }
    return "?";
}

json expected(const pme::StringParams& s) {
    return {{"L", s.L},
            {"dCore", s.dCore},
            {"rhoCore", s.rhoCore},
            {"ECore", s.ECore},
            {"dOuter", s.dOuter},
            {"rhoWind", s.rhoWind},
            {"packing", s.packing},
            {"epsWind", s.epsWind},
            {"f1Target", s.f1Target},
            {"b1", s.b1},
            {"b2", s.b2},
            {"b3", s.b3},
            {"sigmaOverride", s.sigmaOverride},
            {"boundary", name(s.boundary)},
            {"detuneH", s.detuneH},
            {"bridgeEnd", name(s.bridgeEnd)}};
}

json expected(const pme::HammerParams& h) {
    return {{"mass", h.mass},         {"K", h.K},
            {"p", h.p},               {"muHC", h.muHC},
            {"width", h.width},       {"strikeRatio", h.strikeRatio},
            {"law", name(h.law)},     {"stulovEps", h.stulovEps},
            {"stulovTau", h.stulovTau}, {"vMin", h.vMin},
            {"vMax", h.vMax},         {"gammaV", h.gammaV},
            {"dCheck", h.dCheck}};
}

json expected(const pme::UnaCordaParams& u) {
    return {{"s0", u.s0},
            {"s1", u.s1},
            {"kappaUC", u.kappaUC},
            {"deltaPUC", u.deltaPUC},
            {"rhoUpright", u.rhoUpright},
            {"leavingString", u.leavingString}};
}

json expected(const pme::PluckPulse& p) {
    return {{"duration", p.duration}, {"peakForceMax", p.peakForceMax}, {"position", p.position},
            {"width", p.width},       {"angle", p.angle},               {"strengthGamma", p.strengthGamma}};
}

json expected(const pme::Plectrum& p) {
    return {{"tipMass", p.tipMass},
            {"driverStiffness", p.driverStiffness},
            {"driverSpeedMax", p.driverSpeedMax},
            {"slipHeight", p.slipHeight},
            {"contactK", p.contactK},
            {"contactAlpha", p.contactAlpha},
            {"position", p.position},
            {"angle", p.angle},
            {"strengthGamma", p.strengthGamma}};
}

json expected(const pme::Finger& f) {
    return {{"tipMass", f.tipMass},
            {"driverStiffness", f.driverStiffness},
            {"driverSpeedMax", f.driverSpeedMax},
            {"slipHeight", f.slipHeight},
            {"contactK", f.contactK},
            {"contactAlpha", f.contactAlpha},
            {"muFlesh", f.muFlesh},
            {"width", f.width},
            {"position", f.position},
            {"angle", f.angle},
            {"strengthGamma", f.strengthGamma}};
}

json expected(const pme::InitialDisplacement& d) {
    return {{"amplitudeMax", d.amplitudeMax},
            {"position", d.position},
            {"angle", d.angle},
            {"strengthGamma", d.strengthGamma}};
}

json expected(const pme::ExciterParams& e) {
    json j = json::object();
    std::visit(
        [&j](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, pme::HammerParams>) {
                j["hammer"] = expected(v);
            } else if constexpr (std::is_same_v<T, pme::PluckPulse>) {
                j["pluckPulse"] = expected(v);
            } else if constexpr (std::is_same_v<T, pme::Plectrum>) {
                j["plectrum"] = expected(v);
            } else if constexpr (std::is_same_v<T, pme::Finger>) {
                j["finger"] = expected(v);
            } else {
                j["initialDisplacement"] = expected(v);
            }
        },
        e.e);
    return j;
}

json expected(const pme::DamperParams& d) {
    return {{"xRatio", d.xRatio},
            {"width", d.width},
            {"Kd", d.Kd},
            {"Rd", d.Rd},
            {"tau0", d.tau0},
            {"kRelease", d.kRelease},
            {"tauUp", d.tauUp},
            {"etaD", d.etaD},
            {"hasDamper", d.hasDamper},
            {"mode", name(d.mode)}};
}

json expected(const pme::BarrierParams& b) {
    return {{"kind", name(b.kind)}, {"x", b.x},       {"b", b.b},         {"w", b.w},
            {"side", b.side},       {"K", b.K},       {"alpha", b.alpha}, {"muB", b.muB}};
}

json expected(const pme::SegmentDesc& s) {
    json j = {{"string", expected(s.string)},
              {"bridgePoint", s.bridgePoint},
              {"polarisationAngle", s.polarisationAngle},
              {"barriers", json::array()}};
    for (const auto& b : s.barriers) {
        j["barriers"].push_back(expected(b));
    }
    if (s.damper) {
        j["damper"] = expected(*s.damper);
    }
    j["muteRatio"] = s.muteRatio;
    j["tensionModulation"] = s.tensionModulation;
    j["longitudinalModes"] = s.longitudinalModes;
    j["kappaL"] = s.kappaL;
    j["kappaT"] = s.kappaT;
    j["unisonDetuneCents"] = s.unisonDetuneCents;
    return j;
}

json expected(const pme::NoteGroupDesc& g) {
    json j = {{"key", g.key}, {"segments", json::array()}};
    for (const auto& s : g.segments) {
        j["segments"].push_back(expected(s));
    }
    if (g.exciter) {
        j["exciter"] = expected(*g.exciter);
    }
    j["unaCorda"] = expected(g.unaCorda);
    return j;
}

json expected(const pme::BridgePointDesc& b) {
    return {{"position", expected(b.position)}, {"thetaB", b.thetaB}, {"conductanceH", b.conductanceH}};
}

json expected(const pme::RibDesc& r) {
    return {{"start", expected(r.start)}, {"end", expected(r.end)}, {"width", r.width},
            {"height", r.height},         {"youngs", r.youngs},     {"density", r.density}};
}

json expected(const pme::BridgeBeamDesc& b) {
    json j = {{"path", json::array()}};
    for (const auto& p : b.path) {
        j["path"].push_back(expected(p));
    }
    j["width"] = b.width;
    j["height"] = b.height;
    j["youngs"] = b.youngs;
    j["density"] = b.density;
    return j;
}

json expected(const pme::PlateParams& p) {
    json j = {{"lengthX", p.lengthX}, {"lengthY", p.lengthY},       {"thickness", p.thickness},
              {"grainAngle", p.grainAngle}, {"youngsL", p.youngsL}, {"youngsR", p.youngsR},
              {"shearLR", p.shearLR},   {"poissonLR", p.poissonLR}, {"poissonRL", p.poissonRL},
              {"density", p.density},   {"moisture", p.moisture},   {"dEdMcL", p.dEdMcL},
              {"dEdMcR", p.dEdMcR},     {"dGdMc", p.dGdMc},         {"dRhodMc", p.dRhodMc},
              {"lossFactor", p.lossFactor}, {"rimStiffness", p.rimStiffness}, {"fMax", p.fMax},
              {"ribs", json::array()}, {"outline", json::array()}};
    for (const auto& r : p.ribs) {
        j["ribs"].push_back(expected(r));
    }
    for (const auto& v : p.outline) {
        j["outline"].push_back(expected(v));
    }
    j["thicknessNx"] = p.thicknessNx;
    j["thicknessNy"] = p.thicknessNy;
    j["thicknessGrid"] = p.thicknessGrid;
    j["bridges"] = json::array();
    for (const auto& b : p.bridges) {
        j["bridges"].push_back(expected(b));
    }
    j["rimRotationalStiffness"] = p.rimRotationalStiffness;
    return j;
}

json expected(const pme::BodyParams& b) {
    return {{"source", name(b.source)}, {"plate", expected(b.plate)}, {"modalTableHash", b.modalTableHash}};
}

json expected(const pme::InstrumentDesc& d) {
    json j = {{"name", d.name}, {"groups", json::array()}, {"bridgePoints", json::array()}};
    for (const auto& g : d.groups) {
        j["groups"].push_back(expected(g));
    }
    for (const auto& b : d.bridgePoints) {
        j["bridgePoints"].push_back(expected(b));
    }
    j["body"] = expected(d.body);
    j["channels"] = json::array();
    for (const auto& c : d.channels) {
        j["channels"].push_back({{"name", c.name}, {"position", expected(c.position)}});
    }
    j["tuning"] = {{"referencePitch", d.tuning.referencePitch}, {"stretchCents", d.tuning.stretchCents}};
    j["defaults"] = {{"halfPedalP0", d.defaults.halfPedalP0},
                     {"halfPedalP1", d.defaults.halfPedalP1},
                     {"sympatheticGain", d.defaults.sympatheticGain},
                     {"sympatheticLowPass", d.defaults.sympatheticLowPass},
                     {"localBridgeModes", d.defaults.localBridgeModes},
                     {"localBridgeFSplit", d.defaults.localBridgeFSplit}};
    return j;
}

}  // namespace

TEST_CASE("B1-T1 every committed file reads back and writes byte-identical") {
    const std::vector<std::filesystem::path> files = {
        std::filesystem::path(PME_B1_EXAMPLES) / "test-monochord" / "instrument.json",
        std::filesystem::path(PME_B1_EXAMPLES) / "test-unison-pair" / "instrument.json",
        std::filesystem::path(PME_B1_EXAMPLES) / "generic-grand-88" / "instrument.json",
        std::filesystem::path(PME_B1_EXAMPLES) / "test-guitar-6" / "instrument.json",
        std::filesystem::path(PME_B1_FIXTURES) / "all-fields.json",
        std::filesystem::path(PME_B1_FIXTURES) / "body-measured.json",
        std::filesystem::path(PME_B1_FIXTURES) / "numbers.json",
    };
    for (const auto& file : files) {
        CAPTURE(file.string());
        InstrumentFile loaded;
        std::vector<LoadError> errors;
        REQUIRE_MESSAGE(pme::loader::loadInstrumentFile(file, true, loaded, errors) == pme::Status::Ok,
                        describeErrors(errors));
        CHECK(errors.empty());
        CHECK(loaded.sourceVersion == 1);
        std::string written;
        REQUIRE(pme::loader::writeInstrument(loaded, written) == pme::Status::Ok);
        CHECK(written == b1test::readFile(file));
    }
}

TEST_CASE("B1-T1 all-fields.json matches the hand-written expected value of every member") {
    const std::filesystem::path file = std::filesystem::path(PME_B1_FIXTURES) / "all-fields.json";
    InstrumentFile loaded;
    std::vector<LoadError> errors;
    REQUIRE_MESSAGE(pme::loader::loadInstrumentFile(file, true, loaded, errors) == pme::Status::Ok,
                    describeErrors(errors));
    const json document = json::parse(b1test::readFile(file));
    const json want = document.at("instrument");
    const json got = expected(loaded.desc);
    // Compare group by group first so that a failure names the member.
    REQUIRE(got.at("groups").size() == want.at("groups").size());
    for (std::size_t g = 0; g < want.at("groups").size(); ++g) {
        CAPTURE(g);
        CHECK(got.at("groups")[g] == want.at("groups")[g]);
    }
    CHECK(got.at("bridgePoints") == want.at("bridgePoints"));
    CHECK(got.at("body") == want.at("body"));
    CHECK(got.at("channels") == want.at("channels"));
    CHECK(got.at("tuning") == want.at("tuning"));
    CHECK(got.at("defaults") == want.at("defaults"));
    CHECK(got == want);
    // Spot checks of values that are easy to get wrong.
    CHECK(loaded.desc.name == "all-fields");
    CHECK(loaded.desc.groups.size() == 6);
    CHECK(loaded.desc.groups[0].segments[1].string.bridgeEnd == pme::BridgeEnd::At0);
    CHECK(loaded.desc.groups[0].segments[0].barriers.size() == 6);
    CHECK_FALSE(loaded.desc.groups[5].exciter.has_value());
    CHECK(loaded.desc.body.source == pme::BodySource::ModalTable);
    CHECK(std::holds_alternative<pme::Finger>(loaded.desc.groups[3].exciter->e));
    CHECK(std::holds_alternative<pme::InitialDisplacement>(loaded.desc.groups[4].exciter->e));
    REQUIRE(loaded.blobs.size() == 2);
    CHECK(loaded.blobs[1].tool == "pme-fit");
}

TEST_CASE("B1-R6 the four example instruments load without errors") {
    for (const char* name :
         {"test-monochord", "test-unison-pair", "generic-grand-88", "test-guitar-6"}) {
        CAPTURE(name);
        InstrumentFile loaded;
        std::vector<LoadError> errors;
        const auto file = std::filesystem::path(PME_B1_EXAMPLES) / name / "instrument.json";
        REQUIRE_MESSAGE(pme::loader::loadInstrumentFile(file, true, loaded, errors) == pme::Status::Ok,
                        describeErrors(errors));
        CHECK(loaded.desc.name == name);
        CHECK(loaded.blobs.empty());
    }
}

TEST_CASE("B1-R6 generic-grand-88 has 88 groups with the stated string counts") {
    InstrumentFile loaded;
    std::vector<LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(
                std::filesystem::path(PME_B1_EXAMPLES) / "generic-grand-88" / "instrument.json",
                true, loaded, errors) == pme::Status::Ok);
    REQUIRE(loaded.desc.groups.size() == 88);
    for (const auto& g : loaded.desc.groups) {
        const std::size_t want = g.key <= 28 ? 1U : (g.key <= 40 ? 2U : 3U);
        CHECK(g.segments.size() == want);
        CHECK(g.segments[0].damper.has_value());
        CHECK(g.segments[0].damper->hasDamper == (g.key <= 88));
    }
    CHECK(loaded.desc.bridgePoints.size() == 88);
}

TEST_CASE("B1-R4 the canonical writer is stable under a second pass") {
    InstrumentFile loaded;
    std::vector<LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(
                std::filesystem::path(PME_B1_FIXTURES) / "all-fields.json", false, loaded, errors) ==
            pme::Status::Ok);
    std::string once;
    std::string twice;
    REQUIRE(pme::loader::writeInstrument(loaded, once) == pme::Status::Ok);
    InstrumentFile again;
    REQUIRE(pme::loader::loadInstrument(once, again, errors) == pme::Status::Ok);
    REQUIRE(pme::loader::writeInstrument(again, twice) == pme::Status::Ok);
    CHECK(once == twice);
    CHECK(once.back() == '\n');
    CHECK(once.find('\r') == std::string::npos);
}
