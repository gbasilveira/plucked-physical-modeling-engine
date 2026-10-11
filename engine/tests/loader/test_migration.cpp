// SPDX-License-Identifier: MIT
// Covers: B1-R2
// B1-T3, C++ half: a version 0 fixture migrates to the current version; fixtures without a version
// 0 source fail with `migration`; a future version fails with `version`.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "pme/loader/instrument_file.hpp"
#include "test_support.hpp"

namespace {

const std::filesystem::path kV0 = std::filesystem::path(PME_B1_FIXTURES) / "v0";

}  // namespace

TEST_CASE("B1-T3 a version 0 piano migrates to the expected version 1 file") {
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(kV0 / "piano-v0.json", true, loaded, errors) ==
            pme::Status::Ok);
    CHECK(loaded.sourceVersion == 0);
    std::string written;
    REQUIRE(pme::loader::writeInstrument(loaded, written) == pme::Status::Ok);
    CHECK(written == b1test::readFile(kV0 / "piano-v0.expected.json"));

    pme::loader::InstrumentFile current;
    REQUIRE(pme::loader::loadInstrumentFile(kV0 / "piano-v0.expected.json", true, current,
                                            errors) == pme::Status::Ok);
    CHECK(current.sourceVersion == 1);
}

TEST_CASE("B1-T3 migration fills the new members as specified") {
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(kV0 / "piano-v0.json", true, loaded, errors) ==
            pme::Status::Ok);
    const auto& pulse = std::get<pme::PluckPulse>(b1test::must(loaded.desc.groups[2].exciter).e);
    CHECK(pulse.peakForceMax == 5.0);
    CHECK(pulse.strengthGamma == 1.0);
    for (const auto& g : loaded.desc.groups) {
        for (const auto& s : g.segments) {
            CHECK(s.unisonDetuneCents == 0.0);
        }
    }
    const auto& plate = loaded.desc.body.plate;
    CHECK(plate.outline.empty());
    CHECK(plate.thicknessGrid.empty());
    CHECK(plate.bridges.empty());
    CHECK(plate.thicknessNx == 0);
    CHECK(plate.rimRotationalStiffness == 0.0);
}

TEST_CASE("B1-T3 a version 0 plectrum or finger fails with migration at the exciter") {
    const auto table = nlohmann::json::parse(b1test::readFile(kV0 / "expected.json"));
    for (const auto& item : table.items()) {
        CAPTURE(item.key());
        pme::loader::InstrumentFile loaded;
        std::vector<pme::loader::LoadError> errors;
        CHECK(pme::loader::loadInstrumentFile(kV0 / item.key(), true, loaded, errors) ==
              pme::Status::InvalidArgument);
        std::vector<std::pair<std::string, std::string>> got;
        got.reserve(errors.size());
        for (const auto& e : errors) {
            got.emplace_back(e.path, e.code);
        }
        std::vector<std::pair<std::string, std::string>> want;
        want.reserve(item.value().size());
        for (const auto& pair : item.value()) {
            want.emplace_back(pair.at(0).get<std::string>(), pair.at(1).get<std::string>());
        }
        CHECK(got == want);
    }
}

TEST_CASE("B1-T3 a version 0 plectrum with position and angle supplied migrates") {
    auto doc = nlohmann::json::parse(b1test::readFile(kV0 / "plectrum-v0.json"));
    doc["instrument"]["groups"][2]["exciter"]["plectrum"]["position"] = 0.2;
    doc["instrument"]["groups"][2]["exciter"]["plectrum"]["angle"] = 0.0;
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrument(doc.dump(), loaded, errors) == pme::Status::Ok);
    const auto& plectrum = std::get<pme::Plectrum>(b1test::must(loaded.desc.groups[2].exciter).e);
    CHECK(plectrum.driverSpeedMax == 0.5);
    CHECK(plectrum.strengthGamma == 1.0);
}

TEST_CASE("B1-T3 a version 0 finger with the new members supplied migrates") {
    auto doc = nlohmann::json::parse(b1test::readFile(kV0 / "finger-v0.json"));
    auto& finger = doc["instrument"]["groups"][2]["exciter"]["finger"];
    finger["driverStiffness"] = 1500.0;
    finger["position"] = 0.3;
    finger["angle"] = 0.0;
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrument(doc.dump(), loaded, errors) == pme::Status::Ok);
    const auto& f = std::get<pme::Finger>(b1test::must(loaded.desc.groups[2].exciter).e);
    CHECK(f.tipMass == 0.002);
    CHECK(f.contactK == 1.0e5);
    CHECK(f.contactAlpha == 1.5);
    CHECK(f.driverSpeedMax == 1.0);
    CHECK(f.driverStiffness == 1500.0);
}

TEST_CASE("B1-T3 a schema version above the current one fails with version") {
    const auto file = std::filesystem::path(PME_B1_FIXTURES) / "malformed" / "version-future.json";
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    CHECK(pme::loader::loadInstrumentFile(file, true, loaded, errors) ==
          pme::Status::InvalidArgument);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].path == "/schemaVersion");
    CHECK(errors[0].code == "version");
}

TEST_CASE("B1-T3 a schema version that is not an integer fails with version") {
    for (const char* version : {"1.5", "\"1\"", "-1", "true", "null"}) {
        CAPTURE(version);
        const std::string text = std::string(R"({"format": "pme-instrument", "schemaVersion": )") +
                                 version + R"(, "instrument": {}})";
        pme::loader::InstrumentFile loaded;
        std::vector<pme::loader::LoadError> errors;
        CHECK(pme::loader::loadInstrument(text, loaded, errors) == pme::Status::InvalidArgument);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].code == "version");
    }
}
