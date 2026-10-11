// SPDX-License-Identifier: MIT
// Covers: B1-R4
// No exception escapes the loader; hostile input returns InvalidArgument; number formatting.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "pme/loader/instrument_file.hpp"
#include "reader.hpp"
#include "test_support.hpp"

namespace {

using pme::loader::InstrumentFile;
using pme::loader::LoadError;

bool hasCode(const std::vector<LoadError>& errors, const std::string& path,
             const std::string& code) {
    for (const auto& e : errors) {
        if (e.path == path && e.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("B1-R4 ten megabytes of garbage return InvalidArgument with a parse error") {
    std::string garbage;
    garbage.reserve(10'000'000);
    while (garbage.size() < 10'000'000) {
        garbage += "\x01\x7f}{][";
    }
    InstrumentFile out;
    std::vector<LoadError> errors;
    CHECK_NOTHROW(static_cast<void>(pme::loader::loadInstrument(garbage, out, errors)));
    CHECK(pme::loader::loadInstrument(garbage, out, errors) == pme::Status::InvalidArgument);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].path.empty());
    CHECK(errors[0].code == "parse");
}

TEST_CASE("B1-R4 a 10000-deep nested array returns a structure error") {
    const std::string deep = std::string(10000, '[') + std::string(10000, ']');
    InstrumentFile out;
    std::vector<LoadError> errors;
    CHECK(pme::loader::loadInstrument(deep, out, errors) == pme::Status::InvalidArgument);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].code == "structure");
    const std::string deepObject = [] {
        std::string s;
        for (int i = 0; i < 5000; ++i) {
            s += "{\"a\":";
        }
        return s + "1" + std::string(5000, '}');
    }();
    CHECK(pme::loader::loadInstrument(deepObject, out, errors) == pme::Status::InvalidArgument);
    CHECK(hasCode(errors, "", "structure"));
}

TEST_CASE("B1-R4 brackets inside strings do not count towards the nesting depth") {
    std::string text =
        R"({"format": "pme-instrument", "schemaVersion": 1, "instrument": {}, "x": ")";
    text += std::string(500, '[') + R"(\"[[[")" + "}";
    InstrumentFile out;
    std::vector<LoadError> errors;
    CHECK(pme::loader::loadInstrument(text, out, errors) == pme::Status::InvalidArgument);
    CHECK_FALSE(hasCode(errors, "", "structure"));
}

TEST_CASE("B1-R4 non-JSON input and non-objects are reported at the root") {
    InstrumentFile out;
    std::vector<LoadError> errors;
    for (const char* text : {"", "   ", "{", "{} x", "NaN", "[Infinity]", "{\"a\": 1e999}",
                             "\xEF\xBB\xBF{}", "\xFF\xFE"}) {
        CAPTURE(text);
        CHECK(pme::loader::loadInstrument(text, out, errors) == pme::Status::InvalidArgument);
        CHECK(hasCode(errors, "", "parse"));
    }
    CHECK(pme::loader::loadInstrument("[1, 2]", out, errors) == pme::Status::InvalidArgument);
    CHECK(hasCode(errors, "", "type"));
    CHECK(pme::loader::loadInstrument("42", out, errors) == pme::Status::InvalidArgument);
    CHECK(hasCode(errors, "", "type"));
}

TEST_CASE("B1-R4 loading a file that does not exist reports a parse error") {
    InstrumentFile out;
    std::vector<LoadError> errors;
    CHECK(pme::loader::loadInstrumentFile("/no/such/dir/x.json", true, out, errors) ==
          pme::Status::InvalidArgument);
    CHECK(hasCode(errors, "", "parse"));
}

TEST_CASE("B1-R4 optional members take the header defaults") {
    // Strip the optional members from the monochord and load it.
    std::string text = b1test::readFile(std::filesystem::path(PME_B1_EXAMPLES) / "test-monochord" /
                                        "instrument.json");
    auto doc = nlohmann::json::parse(text);
    auto& group = doc["instrument"]["groups"][0];
    group["segments"][0].erase("longitudinalModes");
    group["segments"][0].erase("kappaL");
    group.erase("unaCorda");
    doc["instrument"].erase("defaults");
    doc.erase("blobs");
    InstrumentFile out;
    std::vector<LoadError> errors;
    REQUIRE(pme::loader::loadInstrument(doc.dump(), out, errors) == pme::Status::Ok);
    CHECK(out.desc.groups[0].segments[0].longitudinalModes == 8);
    CHECK(out.desc.groups[0].segments[0].kappaL == 0.026);
    CHECK(out.desc.groups[0].unaCorda.s1 == 0.9);
    CHECK(out.desc.defaults.localBridgeFSplit == 2000.0);
    CHECK(out.desc.tuning.referencePitch == 440.0);
    CHECK(out.blobs.empty());
}

TEST_CASE("B1-R4 writeInstrument rejects a number that is not finite") {
    InstrumentFile loaded;
    std::vector<LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(
                std::filesystem::path(PME_B1_EXAMPLES) / "test-monochord" / "instrument.json", true,
                loaded, errors) == pme::Status::Ok);
    loaded.desc.groups[0].segments[0].string.L = std::numeric_limits<double>::quiet_NaN();
    std::string text = "unchanged";
    CHECK(pme::loader::writeInstrument(loaded, text) == pme::Status::InvalidArgument);
    CHECK(text == "unchanged");
    loaded.desc.groups[0].segments[0].string.L = std::numeric_limits<double>::infinity();
    CHECK(pme::loader::writeInstrument(loaded, text) == pme::Status::InvalidArgument);
}

TEST_CASE("B1-R4 canonical number layout follows nlohmann and round-trips") {
    using pme::loader::detail::formatDouble;
    const std::vector<std::pair<double, std::string>> cases = {
        {0.0, "0.0"},
        {-0.0, "-0.0"},
        {1.0, "1.0"},
        {100.0, "100.0"},
        {0.1, "0.1"},
        {0.0001, "0.0001"},
        {0.00001, "1e-05"},
        {1.5e-9, "1.5e-09"},
        {2e-9, "2e-09"},
        {123456.789, "123456.789"},
        {1e14, "100000000000000.0"},
        {1e15, "1e+15"},
        {1.5e300, "1.5e+300"},
        {5e-324, "5e-324"},
        {200e9, "200000000000.0"},
        {-2.5, "-2.5"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
    };
    for (const auto& [value, text] : cases) {
        CAPTURE(text);
        CHECK(formatDouble(value) == text);
        CHECK(std::strtod(text.c_str(), nullptr) == value);
    }
}

TEST_CASE("B1-R4 numbers that need all seventeen digits survive a write and read") {
    // A fixed pseudo-random sequence of doubles written through the tuning curve.
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    std::vector<double> values;
    for (int i = 0; i < 400; ++i) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        const double v = static_cast<double>(state >> 11U) / 9007199254740992.0 * 1000.0 - 500.0;
        values.push_back(v);
    }
    InstrumentFile loaded;
    std::vector<LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(
                std::filesystem::path(PME_B1_EXAMPLES) / "test-monochord" / "instrument.json", true,
                loaded, errors) == pme::Status::Ok);
    loaded.desc.tuning.stretchCents = values;
    std::string text;
    REQUIRE(pme::loader::writeInstrument(loaded, text) == pme::Status::Ok);
    InstrumentFile again;
    REQUIRE(pme::loader::loadInstrument(text, again, errors) == pme::Status::Ok);
    REQUIRE(again.desc.tuning.stretchCents.size() == values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        CHECK(again.desc.tuning.stretchCents[i] == values[i]);
    }
}

TEST_CASE("B1-R4 wrong types and missing members and unknown keys at the root are reported") {
    InstrumentFile out;
    std::vector<LoadError> errors;
    CHECK(pme::loader::loadInstrument(R"({"format": 3, "instrument": [], "blobs": {}, "x": 1})",
                                      out, errors) == pme::Status::InvalidArgument);
    CHECK(hasCode(errors, "/format", "type"));
    CHECK(hasCode(errors, "/instrument", "type"));
    CHECK(hasCode(errors, "/blobs", "type"));
    CHECK(hasCode(errors, "/x", "unknown_key"));
    CHECK(hasCode(errors, "/schemaVersion", "missing"));
}
