// SPDX-License-Identifier: MIT
// D5-R2 (preset format), D5-R3 (preset import and export), D5-T1 (round trips), D5-T2 (migration).
#include <doctest/doctest.h>

#include <limits>
#include <string>

#include "pme/plugin/library/instruments.hpp"
#include "pme/plugin/library/preset.hpp"
#include "test_support.hpp"

using namespace d5test;  // NOLINT(google-build-using-namespace)

namespace {

const std::string kHash =
    "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

lib::Preset samplePreset() {
    lib::Preset p;
    p.name = "Bright grand";
    p.instrumentId = "factory/generic-grand-88";
    p.instrumentHash = kHash;
    // 2001 output.gain (continuous), 4101 damper.halfPedalStart (structural), 7001 (structural).
    p.params = {{2001, -6.5}, {4101, 0.4}, {7001, 32.0}};
    return p;
}

std::string serialize(const lib::Preset& p) {
    std::string text;
    lib::Report report;
    REQUIRE(lib::serializePreset(p, text, report) == pme::Status::Ok);
    return text;
}

fs::path v0Fixture() { return fs::path(PME_D5_DATA) / "preset-v0.pmepreset"; }

}  // namespace

TEST_CASE("D5-R2 canonical preset text is compact sorted and ends with a newline") {
    lib::Preset p = samplePreset();
    p.params = {{2001, -6.5}, {4101, 0.4}};
    CHECK(serialize(p) ==
          "{\"format\":\"pme-preset\",\"instrument\":{\"hash\":\"" + kHash +
              "\",\"id\":\"factory/generic-grand-88\"},\"name\":\"Bright grand\","
              "\"params\":{\"2001\":-6.5,\"4101\":0.4},\"presetSchemaVersion\":1}\n");
    p.instrumentHash.clear();
    const std::string noHash = serialize(p);
    CHECK(noHash.find("hash") == std::string::npos);
    // Values equal to the default are not written.
    p.params = {{2001, 0.0}, {4101, 0.35}};
    CHECK(serialize(p).find("\"params\":{}") != std::string::npos);
}

TEST_CASE("D5-T1 presets round-trip") {
    TempDir tmp;
    const lib::Preset original = samplePreset();

    SUBCASE("a: save, load and save again are byte-identical") {
        const std::string first = serialize(original);
        lib::Preset loaded;
        int version = -1;
        lib::Report report;
        REQUIRE(lib::parsePreset(first, "stem", loaded, version, report) == pme::Status::Ok);
        CHECK(version == 1);
        CHECK(loaded == original);
        CHECK(serialize(loaded) == first);
        CHECK(report.diagnostics.empty());

        const fs::path file = tmp.path() / "bright.pmepreset";
        REQUIRE(lib::savePresetFile(file, original, report) == pme::Status::Ok);
        lib::Preset fromFile;
        REQUIRE(lib::loadPresetFile(file, fromFile, version, report) == pme::Status::Ok);
        const fs::path file2 = tmp.path() / "again.pmepreset";
        REQUIRE(lib::savePresetFile(file2, fromFile, report) == pme::Status::Ok);
        CHECK(slurp(file) == slurp(file2));
        CHECK(slurp(file) == first);
    }

    SUBCASE("b: export, import into a fresh library and export is byte-identical (preset)") {
        TempDir other;
        const lib::Locations a = tmp.locations();
        const lib::Locations b = other.locations();
        const fs::path source = tmp.path() / "source.pmepreset";
        lib::Report report;
        REQUIRE(lib::savePresetFile(source, original, report) == pme::Status::Ok);
        std::string idA;
        REQUIRE(lib::importPreset(a, source, idA, report) == pme::Status::Ok);
        CHECK(idA == "user/bright-grand");
        const fs::path out1 = tmp.path() / "out1.pmepreset";
        REQUIRE(lib::exportPreset(a, idA, out1, report) == pme::Status::Ok);
        std::string idB;
        REQUIRE(lib::importPreset(b, out1, idB, report) == pme::Status::Ok);
        const fs::path out2 = other.path() / "out2.pmepreset";
        REQUIRE(lib::exportPreset(b, idB, out2, report) == pme::Status::Ok);
        CHECK(slurp(out1) == slurp(out2));
        CHECK(slurp(out1) == serialize(original));
    }

    SUBCASE("c: preset to PluginState to preset is identical") {
        const pme::plugin::StateData state = lib::presetToState(original);
        CHECK(state.instrument == "factory/generic-grand-88");
        REQUIRE(state.params.size() == 3);
        // The D1 state format carries it unchanged.
        const std::string text = pme::plugin::serializeState(state);
        pme::plugin::StateData parsed;
        REQUIRE(pme::plugin::parseState(text, parsed) == pme::plugin::StateParse::Ok);
        const lib::Preset back =
            lib::presetFromState(parsed, original.name, original.instrumentHash);
        CHECK(back == original);
        CHECK(serialize(back) == serialize(original));
    }

    SUBCASE("d: unknown ids are ignored and out-of-range values are clamped, each with a diagnostic") {
        const std::string text =
            "{\"format\":\"pme-preset\",\"instrument\":{\"id\":\"user/x\"},\"name\":\"n\","
            "\"params\":{\"1010\":0.5,\"2001\":100,\"3001\":-3,\"4101\":0.4,\"99999\":1},"
            "\"presetSchemaVersion\":1}";
        lib::Preset p;
        int version = 0;
        lib::Report report;
        REQUIRE(lib::parsePreset(text, "stem", p, version, report) == pme::Status::Ok);
        // 2001 clamps to its maximum 24 dB; 3001 clamps to 0 which is not its default (1.0).
        REQUIRE(p.params.size() == 3);
        CHECK(p.params[0] == std::pair<pme::ParamId, double>{2001, 24.0});
        CHECK(p.params[1] == std::pair<pme::ParamId, double>{3001, 0.0});
        CHECK(p.params[2] == std::pair<pme::ParamId, double>{4101, 0.4});
        int unknown = 0;
        int clamped = 0;
        for (const auto& d : report.diagnostics) {
            if (d.code == "unknown-param") {
                ++unknown;
                CHECK((d.path == "/params/1010" || d.path == "/params/99999"));
            }
            if (d.code == "clamped") {
                ++clamped;
            }
        }
        CHECK(unknown == 2);
        CHECK(clamped == 2);
    }
}

TEST_CASE("D5-T2 an old preset migrates") {
    SUBCASE("a: a version-0 preset loads as version 1 and saving writes version 1") {
        lib::Preset p;
        int version = -1;
        lib::Report report;
        REQUIRE(lib::loadPresetFile(v0Fixture(), p, version, report) == pme::Status::Ok);
        CHECK(version == 0);
        CHECK(p.name == "preset-v0");
        CHECK(p.instrumentId == "factory/generic-grand-88");
        CHECK(p.instrumentHash.empty());
        REQUIRE(p.params.size() == 3);
        CHECK(p.params[0] == std::pair<pme::ParamId, double>{2001, -6.5});
        CHECK(p.params[1] == std::pair<pme::ParamId, double>{4101, 0.4});
        CHECK(p.params[2] == std::pair<pme::ParamId, double>{7001, 32.0});
        // The unknown dotted name is dropped with a diagnostic.
        REQUIRE(report.diagnostics.size() == 1);
        CHECK(report.diagnostics[0].code == "unknown-param");
        CHECK(report.diagnostics[0].path == "/params/no.such.param");

        const std::string text = serialize(p);
        CHECK(text.find("\"presetSchemaVersion\":1") != std::string::npos);
        CHECK(text.find("\"output.gain\"") == std::string::npos);
        lib::Preset again;
        lib::Report report2;
        REQUIRE(lib::parsePreset(text, "x", again, version, report2) == pme::Status::Ok);
        CHECK(version == 1);
        CHECK(again == p);
    }

    SUBCASE("a: a version-0 preset with a name keeps it and a bad v0 shape is an error") {
        lib::Preset p;
        int version = 0;
        lib::Report report;
        REQUIRE(lib::parsePreset("{\"format\":\"pme-preset\",\"presetSchemaVersion\":0,"
                                 "\"instrument\":\"user/a\",\"name\":\"Old\",\"params\":{}}",
                                 "stem", p, version, report) == pme::Status::Ok);
        CHECK(p.name == "Old");
        lib::Report bad;
        lib::Preset unchanged;
        unchanged.name = "unchanged";
        CHECK(lib::parsePreset("{\"format\":\"pme-preset\",\"presetSchemaVersion\":0,"
                               "\"instrument\":{\"id\":\"user/a\"},\"params\":{}}",
                               "stem", unchanged, version, bad) == pme::Status::InvalidArgument);
        CHECK(hasCode(bad.errors, lib::code::kType));
        CHECK(unchanged.name == "unchanged");
    }

    SUBCASE("b: a newer version fails with version and loads nothing") {
        TempDir tmp;
        const lib::Locations loc = tmp.locations();
        const fs::path source = tmp.path() / "future.pmepreset";
        spit(source,
             "{\"format\":\"pme-preset\",\"instrument\":{\"id\":\"user/x\"},\"name\":\"n\","
             "\"params\":{},\"presetSchemaVersion\":2}\n");
        lib::Preset p;
        p.name = "unchanged";
        int version = 0;
        lib::Report report;
        CHECK(lib::loadPresetFile(source, p, version, report) == pme::Status::InvalidArgument);
        REQUIRE(report.errors.size() == 1);
        CHECK(report.errors[0].code == "version");
        CHECK(report.errors[0].path == "/presetSchemaVersion");
        CHECK(p.name == "unchanged");

        lib::Report importReport;
        std::string id;
        CHECK(lib::importPreset(loc, source, id, importReport) == pme::Status::InvalidArgument);
        CHECK(hasCode(importReport.errors, lib::code::kVersion));
        CHECK(id.empty());
        CHECK_FALSE(fs::exists(loc.userPresets()));
        CHECK(lib::listPresets(loc).empty());
    }
}

TEST_CASE("D5-R2 malformed presets report B1-style errors") {
    const auto errorsOf = [](const std::string& text) {
        lib::Preset p;
        int version = 0;
        lib::Report report;
        CHECK(lib::parsePreset(text, "stem", p, version, report) == pme::Status::InvalidArgument);
        return report.errors;
    };
    CHECK(hasCode(errorsOf("not json"), "parse"));
    CHECK(hasCode(errorsOf("[]"), "parse"));
    CHECK(hasCode(errorsOf("{}"), "missing"));
    CHECK(hasCode(errorsOf("{\"format\":\"other\",\"presetSchemaVersion\":1}"), "enum"));
    CHECK(hasCode(errorsOf("{\"format\":\"pme-preset\"}"), "missing"));
    CHECK(hasCode(errorsOf("{\"format\":\"pme-preset\",\"presetSchemaVersion\":\"1\"}"), "version"));
    CHECK(hasCode(errorsOf("{\"format\":\"pme-preset\",\"presetSchemaVersion\":-1}"), "version"));
    const std::string head = "{\"format\":\"pme-preset\",\"presetSchemaVersion\":1,";
    CHECK(hasCode(errorsOf(head + "\"params\":{}}"), "missing"));  // instrument
    CHECK(hasCode(errorsOf(head + "\"instrument\":\"user/a\"}"), "type"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"../a\"}}"), "invalid-id"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"user/a\",\"hash\":\"abc\"}}"), "type"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"user/a\",\"x\":1}}"), "unknown_key"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"user/a\"},\"extra\":1}"), "unknown_key"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"user/a\"},\"params\":{\"x\":1}}"),
                  "type"));
    CHECK(hasCode(errorsOf(head + "\"instrument\":{\"id\":\"user/a\"},\"params\":{\"2001\":\"1\"}}"),
                  "type"));
    CHECK(hasCode(
        errorsOf(head + "\"instrument\":{\"id\":\"user/a\"},\"params\":{\"2001\":1,\"02001\":2}}"),
        "duplicate"));
    // Non-finite values cannot be written either.
    lib::Preset p = samplePreset();
    p.params = {{2001, std::numeric_limits<double>::infinity()}};
    std::string text;
    lib::Report report;
    CHECK(lib::serializePreset(p, text, report) == pme::Status::InvalidArgument);
    CHECK(hasCode(report.errors, lib::code::kRange));
}

TEST_CASE("D5-R3 preset import is idempotent and a name clash gets a suffix") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    lib::Report report;
    lib::Preset a = samplePreset();
    const fs::path fileA = tmp.path() / "a.pmepreset";
    REQUIRE(lib::savePresetFile(fileA, a, report) == pme::Status::Ok);
    std::string id1;
    std::string id2;
    REQUIRE(lib::importPreset(loc, fileA, id1, report) == pme::Status::Ok);
    REQUIRE(lib::importPreset(loc, fileA, id2, report) == pme::Status::Ok);
    CHECK(id1 == "user/bright-grand");
    CHECK(id2 == id1);

    lib::Preset b = a;
    b.params = {{2001, -3.0}};  // same name, different content
    const fs::path fileB = tmp.path() / "b.pmepreset";
    REQUIRE(lib::savePresetFile(fileB, b, report) == pme::Status::Ok);
    std::string id3;
    REQUIRE(lib::importPreset(loc, fileB, id3, report) == pme::Status::Ok);
    CHECK(id3 == "user/bright-grand-2");

    lib::Preset c = samplePreset();
    c.name = "Flügel Ünïcode ♪";
    const fs::path fileC = tmp.path() / "c.pmepreset";
    REQUIRE(lib::savePresetFile(fileC, c, report) == pme::Status::Ok);
    std::string id4;
    REQUIRE(lib::importPreset(loc, fileC, id4, report) == pme::Status::Ok);
    CHECK(id4 == "user/fl-gel-n-code");
    lib::Preset loaded;
    int version = 0;
    fs::path stored;
    REQUIRE(lib::resolvePreset(loc, id4, stored, report) == pme::Status::Ok);
    REQUIRE(lib::loadPresetFile(stored, loaded, version, report) == pme::Status::Ok);
    CHECK(loaded.name == c.name);  // the display name keeps its characters

    // A bad file does not break the listing.
    spit(loc.userPresets() / "broken.pmepreset", "{");
    const auto list = lib::listPresets(loc);
    REQUIRE(list.size() == 4);
    int broken = 0;
    for (const auto& e : list) {
        if (e.id == "user/broken") {
            ++broken;
            CHECK_FALSE(e.errors.empty());
            CHECK(e.schemaVersion == -1);
        } else {
            CHECK(e.errors.empty());
            CHECK(e.schemaVersion == 1);
            CHECK(e.source == lib::Source::User);
        }
    }
    CHECK(broken == 1);
}

TEST_CASE("D5-R3 a preset whose instrument does not resolve imports and then fails to load") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    lib::Report report;
    const fs::path file = tmp.path() / "dangling.pmepreset";
    REQUIRE(lib::savePresetFile(file, samplePreset(), report) == pme::Status::Ok);
    std::string id;
    REQUIRE(lib::importPreset(loc, file, id, report) == pme::Status::Ok);
    CHECK(hasCode(report.diagnostics, lib::code::kNotFound));

    lib::Preset preset;
    pme::loader::InstrumentFile instrument;
    lib::Report loadReport;
    CHECK(lib::loadLibraryPreset(loc, id, preset, instrument, loadReport) ==
          pme::Status::InvalidArgument);
    CHECK(hasCode(loadReport.errors, lib::code::kNotFound));
}

TEST_CASE("D5-R2 a changed instrument is a diagnostic and not an error") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const fs::path src = installExample("test-unison-pair", tmp.path() / "src");
    lib::Report report;
    lib::ImportResult imported;
    REQUIRE(lib::importInstrument(loc, src, imported, report) == pme::Status::Ok);

    pme::loader::InstrumentFile instrument;
    REQUIRE(lib::loadLibraryInstrument(loc, imported.id, instrument, report) == pme::Status::Ok);
    lib::Preset preset = samplePreset();
    preset.instrumentId = imported.id;
    REQUIRE(lib::instrumentHash(instrument, preset.instrumentHash, report) == pme::Status::Ok);
    const fs::path file = tmp.path() / "p.pmepreset";
    REQUIRE(lib::savePresetFile(file, preset, report) == pme::Status::Ok);
    std::string presetId;
    lib::Report importReport;
    REQUIRE(lib::importPreset(loc, file, presetId, importReport) == pme::Status::Ok);
    CHECK(importReport.diagnostics.empty());

    lib::Preset loaded;
    pme::loader::InstrumentFile loadedInstrument;
    lib::Report ok;
    REQUIRE(lib::loadLibraryPreset(loc, presetId, loaded, loadedInstrument, ok) ==
            pme::Status::Ok);
    CHECK(ok.diagnostics.empty());
    CHECK(loaded == preset);
    CHECK(loadedInstrument.desc.name == instrument.desc.name);

    // Edit the stored instrument (a different but valid name) and load again.
    const fs::path stored = loc.userInstruments() / "test-unison-pair" / "instrument.json";
    std::string text = slurp(stored);
    const auto at = text.find("\"name\": \"test-unison-pair\"");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string("\"name\": \"test-unison-pair\"").size(), "\"name\": \"edited\"");
    spit(stored, text);
    lib::Report changed;
    REQUIRE(lib::loadLibraryPreset(loc, presetId, loaded, loadedInstrument, changed) ==
            pme::Status::Ok);
    CHECK(hasCode(changed.diagnostics, lib::code::kInstrumentChanged));
    CHECK(loadedInstrument.desc.name == "edited");
}
