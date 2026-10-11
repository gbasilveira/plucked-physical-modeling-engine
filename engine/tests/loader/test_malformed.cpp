// SPDX-License-Identifier: MIT
// Covers: B1-R3, B1-R5
// B1-T2, C++ half: every malformed fixture fails with exactly the (path, code) pairs listed in
// compiler/tests/data/b1/malformed/expected.json, the same pairs the Python loader reports.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "pme/loader/instrument_file.hpp"
#include "test_support.hpp"

namespace {

using Pair = std::pair<std::string, std::string>;

std::vector<Pair> pairsOf(const std::vector<pme::loader::LoadError>& errors) {
    std::vector<Pair> out;
    for (const auto& e : errors) {
        out.emplace_back(e.path, e.code);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("B1-T2 malformed files fail with the expected paths and codes") {
    const std::filesystem::path dir = std::filesystem::path(PME_B1_FIXTURES) / "malformed";
    const auto table = nlohmann::json::parse(b1test::readFile(dir / "expected.json"));
    REQUIRE(table.size() >= 40);
    for (const auto& item : table.items()) {
        const std::string& name = item.key();
        CAPTURE(name);
        std::vector<Pair> want;
        for (const auto& pair : item.value()) {
            want.emplace_back(pair.at(0).get<std::string>(), pair.at(1).get<std::string>());
        }
        std::sort(want.begin(), want.end());

        pme::loader::InstrumentFile out;
        out.desc.name = "unchanged";
        std::vector<pme::loader::LoadError> errors;
        const pme::Status status =
            pme::loader::loadInstrumentFile(dir / name, /*verifyBlobs=*/true, out, errors);
        CHECK(status == pme::Status::InvalidArgument);
        CHECK(pairsOf(errors) == want);
        CHECK(out.desc.name == "unchanged");
        for (const auto& e : errors) {
            CHECK_FALSE(e.message.empty());
        }
    }
}

TEST_CASE("B1-R5 errors are all reported at once and sorted") {
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    const auto file = std::filesystem::path(PME_B1_FIXTURES) / "malformed" / "three-errors.json";
    CHECK(pme::loader::loadInstrumentFile(file, true, out, errors) == pme::Status::InvalidArgument);
    REQUIRE(errors.size() == 3);
    CHECK(std::is_sorted(errors.begin(), errors.end(), [](const auto& a, const auto& b) {
        return a.path < b.path;
    }));
}

TEST_CASE("B1-R5 paths escape tilde and slash as JSON Pointer requires") {
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    const std::string text =
        R"({"format": "pme-instrument", "schemaVersion": 1, "instrument": {}, "a/b~c": 1})";
    CHECK(pme::loader::loadInstrument(text, out, errors) == pme::Status::InvalidArgument);
    const auto pairs = pairsOf(errors);
    CHECK(std::find(pairs.begin(), pairs.end(), Pair{"/a~1b~0c", "unknown_key"}) != pairs.end());
}

TEST_CASE("B1-R5 a value of the wrong type reports only type at its path") {
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    const std::string text =
        R"({"format": "pme-instrument", "schemaVersion": 1, "instrument": "x", "blobs": []})";
    CHECK(pme::loader::loadInstrument(text, out, errors) == pme::Status::InvalidArgument);
    CHECK(pairsOf(errors) == std::vector<Pair>{{"/instrument", "type"}});
}

TEST_CASE("B1-R5 at most one hundred errors are reported") {
    std::string text = R"({"format": "pme-instrument", "schemaVersion": 1, "instrument": {})";
    for (int i = 0; i < 250; ++i) {
        text += ", \"k" + std::to_string(i) + "\": 1";
    }
    text += "}";
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    CHECK(pme::loader::loadInstrument(text, out, errors) == pme::Status::InvalidArgument);
    CHECK(errors.size() == 100);
}

TEST_CASE("B1-R3 a missing blob file is an error only when blobs are verified") {
    const auto file =
        std::filesystem::path(PME_B1_FIXTURES) / "malformed" / "blob-missing-file.json";
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    CHECK(pme::loader::loadInstrumentFile(file, false, out, errors) == pme::Status::Ok);
    CHECK(errors.empty());
    CHECK(pme::loader::loadInstrumentFile(file, true, out, errors) == pme::Status::InvalidArgument);
}

TEST_CASE("B1-R3 the fixture blob matches its manifest in the loaded file") {
    const auto file = std::filesystem::path(PME_B1_FIXTURES) / "all-fields.json";
    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(file, true, out, errors) == pme::Status::Ok);
    REQUIRE(out.blobs.size() == 2);
    const auto& first = out.blobs[0];
    CHECK(first.path == "blobs/modal-small.bin");
    CHECK(first.tool == "pme-plate-compiler");
    REQUIRE(first.arrays.size() == 3);
    CHECK(first.arrays[0].dtype == "float64");
    CHECK(first.arrays[0].shape == std::vector<std::int64_t>{4, 3});
    CHECK(first.arrays[1].offset == 96);
    CHECK(first.arrays[2].dtype == "uint32");
    CHECK(first.arrays[2].offset == 136);
    const std::string bytes = b1test::readFile(std::filesystem::path(PME_B1_FIXTURES) / first.path);
    CHECK(bytes.size() == 168);
}

TEST_CASE("B1-R3 a wrong hash is a blob error and a changed file fails verification") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "pme_b1_blob_test";
    fs::create_directories(tmp);
    fs::copy_file(fs::path(PME_B1_FIXTURES) / "malformed" / "modal-small.bin",
                  tmp / "modal-small.bin", fs::copy_options::overwrite_existing);
    // A valid document first (the malformed range-key fixture with a legal key).
    auto doc = nlohmann::json::parse(
        b1test::readFile(fs::path(PME_B1_FIXTURES) / "malformed" / "range-key.json"));
    doc["instrument"]["groups"][0]["key"] = 45;
    const fs::path instrument = tmp / "instrument.json";
    {
        std::ofstream out(instrument, std::ios::binary);
        out << doc.dump(2);
    }
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    CHECK(pme::loader::loadInstrumentFile(instrument, true, loaded, errors) == pme::Status::Ok);
    {
        std::string bytes = b1test::readFile(tmp / "modal-small.bin");
        bytes[3] = static_cast<char>(bytes[3] ^ 1);
        std::ofstream out(tmp / "modal-small.bin", std::ios::binary);
        out << bytes;
    }
    CHECK(pme::loader::loadInstrumentFile(instrument, true, loaded, errors) ==
          pme::Status::InvalidArgument);
    CHECK(pairsOf(errors) == std::vector<Pair>{{"/blobs/0/hash", "blob"}});
    CHECK(pme::loader::loadInstrumentFile(instrument, false, loaded, errors) == pme::Status::Ok);
    fs::remove_all(tmp);
}
