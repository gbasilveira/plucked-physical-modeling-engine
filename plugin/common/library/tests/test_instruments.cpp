// SPDX-License-Identifier: MIT
// D5-R3 (instrument import and export, migration through B1), D5-T1(b) and D5-T2(c).
#include <doctest/doctest.h>

#include <string>

#include "pme/loader/instrument_file.hpp"
#include "pme/plugin/library/instruments.hpp"
#include "test_support.hpp"

using namespace d5test;  // NOLINT(google-build-using-namespace)

namespace {

std::string canonicalText(const fs::path& file) {
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(file, true, loaded, errors) == pme::Status::Ok);
    std::string text;
    REQUIRE(pme::loader::writeInstrument(loaded, text) == pme::Status::Ok);
    return text;
}

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
}

std::size_t countDirs(const fs::path& dir) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        n += e.is_directory() ? 1U : 0U;
    }
    return n;
}

}  // namespace

TEST_CASE("D5-R3 reduceName keeps lower-case letters digits and single dashes") {
    CHECK(lib::reduceName("Generic Grand 88") == "generic-grand-88");
    CHECK(lib::reduceName("  --A__b--  ") == "a-b");
    CHECK(lib::reduceName("Flügel") == "fl-gel");
    CHECK(lib::reduceName("") == "instrument");
    CHECK(lib::reduceName("***") == "instrument");
    const std::string longName(200, 'a');
    CHECK(lib::reduceName(longName).size() == 64);
    CHECK(lib::reduceName(std::string(63, 'a') + " b").size() == 63);  // no trailing dash
}

TEST_CASE("D5-R3 import stores canonical text and verified blobs and is idempotent") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const fs::path source = installBlobInstrument(tmp.path() / "src");
    lib::Report report;
    lib::ImportResult first;
    REQUIRE(lib::importInstrument(loc, source, first, report) == pme::Status::Ok);
    CHECK_FALSE(first.alreadyPresent);
    CHECK(first.sourceVersion == 1);

    const fs::path stored = loc.userInstruments() / first.id.substr(5);
    CHECK(slurp(stored / "instrument.json") == canonicalText(source));
    CHECK(slurp(stored / "blobs" / "modal-small.bin") ==
          slurp(source.parent_path() / "blobs" / "modal-small.bin"));
    CHECK(slurp(stored / "blobs" / "modal-second.bin") ==
          slurp(source.parent_path() / "blobs" / "modal-second.bin"));
    // No temporary directory is left behind.
    CHECK(countDirs(loc.userInstruments()) == 1);

    lib::ImportResult second;
    REQUIRE(lib::importInstrument(loc, source, second, report) == pme::Status::Ok);
    CHECK(second.alreadyPresent);
    CHECK(second.id == first.id);
    CHECK(countDirs(loc.userInstruments()) == 1);

    pme::loader::InstrumentFile loaded;
    REQUIRE(lib::loadLibraryInstrument(loc, first.id, loaded, report) == pme::Status::Ok);
    CHECK(loaded.blobs.size() == 2);
}

TEST_CASE("D5-R3 a name clash with different content gets a numeric suffix") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const fs::path a = installExample("test-unison-pair", tmp.path() / "a");
    const fs::path b = installExample("test-unison-pair", tmp.path() / "b");
    std::string text = slurp(b);
    replaceOnce(text, "\"L\": 0.64", "\"L\": 0.65");
    spit(b, text);
    const fs::path c = installExample("test-unison-pair", tmp.path() / "c");
    text = slurp(c);
    replaceOnce(text, "\"L\": 0.64", "\"L\": 0.66");
    spit(c, text);

    lib::Report report;
    lib::ImportResult ra;
    lib::ImportResult rb;
    lib::ImportResult rc;
    lib::ImportResult rb2;
    REQUIRE(lib::importInstrument(loc, a, ra, report) == pme::Status::Ok);
    REQUIRE(lib::importInstrument(loc, b, rb, report) == pme::Status::Ok);
    REQUIRE(lib::importInstrument(loc, c, rc, report) == pme::Status::Ok);
    REQUIRE(lib::importInstrument(loc, b, rb2, report) == pme::Status::Ok);
    CHECK(ra.id == "user/test-unison-pair");
    CHECK(rb.id == "user/test-unison-pair-2");
    CHECK(rc.id == "user/test-unison-pair-3");
    CHECK(rb2.id == rb.id);
    CHECK(rb2.alreadyPresent);
    CHECK(countDirs(loc.userInstruments()) == 3);
}

TEST_CASE("D5-R3 a failed import leaves no partial directory") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();

    SUBCASE("missing blob") {
        const fs::path source = installBlobInstrument(tmp.path() / "src");
        fs::remove(source.parent_path() / "blobs" / "modal-second.bin");
        lib::Report report;
        lib::ImportResult result;
        CHECK(lib::importInstrument(loc, source, result, report) == pme::Status::InvalidArgument);
        CHECK(hasCode(report.errors, lib::code::kBlob));
        CHECK(result.id.empty());
    }
    SUBCASE("corrupt blob") {
        const fs::path source = installBlobInstrument(tmp.path() / "src");
        flipByte(source.parent_path() / "blobs" / "modal-small.bin", 3);
        lib::Report report;
        lib::ImportResult result;
        CHECK(lib::importInstrument(loc, source, result, report) == pme::Status::InvalidArgument);
        CHECK(hasCode(report.errors, lib::code::kBlob));
    }
    SUBCASE("invalid document") {
        const fs::path source = tmp.path() / "bad" / "instrument.json";
        spit(source, "{\"format\": \"pme-instrument\"}");
        lib::Report report;
        lib::ImportResult result;
        CHECK(lib::importInstrument(loc, source, result, report) == pme::Status::InvalidArgument);
        REQUIRE_FALSE(report.errors.empty());
        CHECK_FALSE(report.errors[0].code.empty());  // B1's error, passed through
    }
    SUBCASE("missing file") {
        lib::Report report;
        lib::ImportResult result;
        CHECK(lib::importInstrument(loc, tmp.path() / "none.json", result, report) ==
              pme::Status::InvalidArgument);
        CHECK_FALSE(report.errors.empty());
    }
    // Whatever failed, the library holds no instrument directory (and no staging directory).
    if (fs::exists(loc.userInstruments())) {
        CHECK(countDirs(loc.userInstruments()) == 0);
    }
}

TEST_CASE("D5-T1 an instrument with a blob exports and re-imports to the same hash") {
    TempDir tmp;
    TempDir other;
    const lib::Locations a = tmp.locations();
    const lib::Locations b = other.locations();
    const fs::path source = installBlobInstrument(tmp.path() / "src");
    lib::Report report;
    lib::ImportResult ia;
    REQUIRE(lib::importInstrument(a, source, ia, report) == pme::Status::Ok);

    fs::path export1;
    REQUIRE(lib::exportInstrument(a, ia.id, tmp.path() / "out1", export1, report) ==
            pme::Status::Ok);
    lib::ImportResult ib;
    REQUIRE(lib::importInstrument(b, export1, ib, report) == pme::Status::Ok);
    CHECK(ib.id == ia.id);
    fs::path export2;
    REQUIRE(lib::exportInstrument(b, ib.id, other.path() / "out2", export2, report) ==
            pme::Status::Ok);

    CHECK(slurp(export1) == slurp(export2));
    CHECK(slurp(export1) == canonicalText(source));
    for (const char* blob : {"modal-small.bin", "modal-second.bin"}) {
        CHECK(slurp(export1.parent_path() / "blobs" / blob) ==
              slurp(export2.parent_path() / "blobs" / blob));
    }
    pme::loader::InstrumentFile fa;
    pme::loader::InstrumentFile fb;
    REQUIRE(lib::loadLibraryInstrument(a, ia.id, fa, report) == pme::Status::Ok);
    REQUIRE(lib::loadLibraryInstrument(b, ib.id, fb, report) == pme::Status::Ok);
    std::string ha;
    std::string hb;
    REQUIRE(lib::instrumentHash(fa, ha, report) == pme::Status::Ok);
    REQUIRE(lib::instrumentHash(fb, hb, report) == pme::Status::Ok);
    CHECK(ha == hb);
    CHECK(ha.size() == 71);
    CHECK(ha.starts_with("sha256:"));

    // Exporting twice to the same place is allowed and gives the same bytes.
    fs::path again;
    REQUIRE(lib::exportInstrument(a, ia.id, tmp.path() / "out1", again, report) == pme::Status::Ok);
    CHECK(again == export1);
    CHECK(slurp(again) == slurp(export2));
}

TEST_CASE("D5-T2 version-0 instrument import migrates through B1") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const fs::path v0 = tmp.path() / "v0";
    copyTree(b1Fixtures() / "v0", v0);
    lib::Report report;
    lib::ImportResult result;
    REQUIRE(lib::importInstrument(loc, v0 / "piano-v0.json", result, report) == pme::Status::Ok);
    CHECK(result.sourceVersion == 0);

    const fs::path stored = loc.userInstruments() / result.id.substr(5) / "instrument.json";
    // The stored text is B1's canonical version-1 text of the migrated instrument.
    CHECK(slurp(stored) == slurp(v0 / "piano-v0.expected.json"));
    CHECK(slurp(stored).find("\"schemaVersion\": 1") != std::string::npos);
    pme::loader::InstrumentFile loaded;
    REQUIRE(lib::loadLibraryInstrument(loc, result.id, loaded, report) == pme::Status::Ok);
    CHECK(loaded.sourceVersion == 1);
}

TEST_CASE("D5-R3 non-ASCII instrument names and directories work") {
    TempDir tmp(u8"tëst-ñ-用户");
    const lib::Locations loc = tmp.locations();
    const fs::path source = installExample("test-unison-pair", tmp.path() / u8"quelle-é");
    std::string text = slurp(source);
    replaceOnce(text, "\"name\": \"test-unison-pair\"", "\"name\": \"Flügel Ünï\"");
    spit(source, text);

    lib::Report report;
    lib::ImportResult result;
    REQUIRE(lib::importInstrument(loc, source, result, report) == pme::Status::Ok);
    CHECK(result.id == "user/fl-gel-n");
    pme::loader::InstrumentFile loaded;
    REQUIRE(lib::loadLibraryInstrument(loc, result.id, loaded, report) == pme::Status::Ok);
    CHECK(loaded.desc.name == "Flügel Ünï");

    fs::path exported;
    REQUIRE(lib::exportInstrument(loc, result.id, tmp.path() / u8"ausgabe-ß", exported, report) ==
            pme::Status::Ok);
    CHECK(slurp(exported) == slurp(loc.userInstruments() / "fl-gel-n" / "instrument.json"));

    const auto list = lib::listInstruments(loc);
    REQUIRE(list.size() == 1);
    CHECK(list[0].id == "user/fl-gel-n");
    CHECK(list[0].name == "Flügel Ünï");
}

TEST_CASE("D5-R3 listInstruments covers factory and user and survives a bad entry") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    installExample("test-monochord", loc.factoryInstruments() / "mono");
    installExample("test-unison-pair", loc.userInstruments() / "pair");
    const fs::path v0 = tmp.path() / "v0";
    copyTree(b1Fixtures() / "v0", v0);
    fs::create_directories(loc.userInstruments() / "oldpiano");
    fs::copy_file(v0 / "piano-v0.json", loc.userInstruments() / "oldpiano" / "instrument.json");
    spit(loc.userInstruments() / "broken" / "instrument.json", "{");
    fs::create_directories(loc.userInstruments() / "empty");
    fs::create_directories(loc.userInstruments() / ".hidden");

    const auto list = lib::listInstruments(loc);
    REQUIRE(list.size() == 5);
    CHECK(list[0].id == "factory/mono");
    CHECK(list[0].source == lib::Source::Factory);
    CHECK(list[0].schemaVersion == 1);
    CHECK(list[0].errors.empty());
    CHECK(list[1].id == "user/broken");
    CHECK_FALSE(list[1].errors.empty());
    CHECK(list[1].schemaVersion == -1);
    CHECK(list[2].id == "user/empty");
    CHECK(list[2].errors[0].code == "not-found");
    CHECK(list[3].id == "user/oldpiano");
    CHECK(list[3].schemaVersion == 0);  // found on disk
    CHECK(list[3].errors.empty());
    CHECK(list[4].id == "user/pair");
    CHECK(list[4].name == "test-unison-pair");
}
