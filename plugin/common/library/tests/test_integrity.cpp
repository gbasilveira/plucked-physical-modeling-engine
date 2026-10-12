// SPDX-License-Identifier: MIT
// D5-R5 (blob and cache integrity by hash) and D5-T4 (corruption is detected and reported).
#include <doctest/doctest.h>

#include <string>

#include "pme/plugin/library/cache.hpp"
#include "pme/plugin/library/instruments.hpp"
#include "test_support.hpp"

using namespace d5test;  // NOLINT(google-build-using-namespace)

namespace {

/// Import the all-fields fixture (two blobs) and return its library id.
std::string importBlobInstrument(const lib::Locations& loc, const fs::path& scratch) {
    const fs::path source = installBlobInstrument(scratch);
    lib::Report report;
    lib::ImportResult result;
    REQUIRE(lib::importInstrument(loc, source, result, report) == pme::Status::Ok);
    return result.id;
}

fs::path storedBlob(const lib::Locations& loc, const std::string& id, const char* name) {
    return loc.userInstruments() / id.substr(5) / "blobs" / name;
}

pme::BodyModes sampleBody() {
    pme::BodyModes b;
    b.numModes = 2;
    b.numPoints = 2;
    b.numChannels = 1;
    b.omega = {10.0, 20.0};
    b.sigma = {1.0, 2.0};
    b.phi = {0.1, 0.2, 0.3, 0.4};
    b.alpha = {1.0, 2.0};
    b.beta = {3.0, 4.0};
    b.channelDelay = {0.01};
    b.residualMobility = {0.5, 0.25};
    return b;
}

bool equalBodies(const pme::BodyModes& a, const pme::BodyModes& b) {
    return a.numModes == b.numModes && a.numPoints == b.numPoints &&
           a.numChannels == b.numChannels && a.omega == b.omega && a.sigma == b.sigma &&
           a.phi == b.phi && a.alpha == b.alpha && a.beta == b.beta &&
           a.channelDelay == b.channelDelay && a.residualMobility == b.residualMobility;
}

/// A cache holding one valid body entry; returns its key.
lib::CacheKey fillOne(const lib::Locations& loc, const std::string& tag) {
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, tag);
    lib::Report report;
    pme::BodyModes body;
    REQUIRE(lib::getOrCompute(
                loc, key, body,
                [](pme::BodyModes& out) {
                    out = sampleBody();
                    return pme::Status::Ok;
                },
                report) == pme::Status::Ok);
    return key;
}

}  // namespace

TEST_CASE("D5-T4 a corrupted blob is detected and reported") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const std::string id = importBlobInstrument(loc, tmp.path() / "src");

    pme::loader::InstrumentFile ok;
    lib::Report okReport;
    REQUIRE(lib::loadLibraryInstrument(loc, id, ok, okReport) == pme::Status::Ok);
    REQUIRE(ok.blobs.size() == 2);
    REQUIRE(ok.blobs[0].path == "blobs/modal-small.bin");

    SUBCASE("a: one flipped byte in a library blob") {
        const fs::path blob = storedBlob(loc, id, "modal-small.bin");
        flipByte(blob, 3);
        pme::loader::InstrumentFile out;
        out.desc.name = "unchanged";
        lib::Report report;
        CHECK(lib::loadLibraryInstrument(loc, id, out, report) == pme::Status::InvalidArgument);
        REQUIRE(report.errors.size() >= 1);
        CHECK(report.errors[0].code == "blob");
        CHECK(report.errors[0].path.starts_with("/blobs/0"));
        CHECK(report.errors[0].message.find("modal-small.bin") != std::string::npos);
        CHECK(out.desc.name == "unchanged");  // nothing is returned
        CHECK(out.blobs.empty());
        for (const auto& e : report.errors) {
            CHECK(e.code == "blob");
        }
    }
    SUBCASE("a: a flipped byte in the second blob names the second manifest entry") {
        flipByte(storedBlob(loc, id, "modal-second.bin"), 0);
        pme::loader::InstrumentFile out;
        lib::Report report;
        CHECK(lib::loadLibraryInstrument(loc, id, out, report) == pme::Status::InvalidArgument);
        REQUIRE(report.errors.size() >= 1);
        CHECK(report.errors[0].path.starts_with("/blobs/1"));
        CHECK(report.errors[0].message.find("modal-second.bin") != std::string::npos);
    }
    SUBCASE("b: a deleted blob") {
        fs::remove(storedBlob(loc, id, "modal-small.bin"));
        pme::loader::InstrumentFile out;
        out.desc.name = "unchanged";
        lib::Report report;
        CHECK(lib::loadLibraryInstrument(loc, id, out, report) == pme::Status::InvalidArgument);
        REQUIRE(report.errors.size() >= 1);
        CHECK(report.errors[0].code == "blob");
        CHECK(report.errors[0].path.starts_with("/blobs/0"));
        CHECK(report.errors[0].message.find("modal-small.bin") != std::string::npos);
        CHECK(out.desc.name == "unchanged");
    }
    SUBCASE("export refuses a corrupted blob too") {
        flipByte(storedBlob(loc, id, "modal-small.bin"), 5);
        lib::Report report;
        fs::path written;
        CHECK(lib::exportInstrument(loc, id, tmp.path() / "out", written, report) ==
              pme::Status::InvalidArgument);
        CHECK(hasCode(report.errors, lib::code::kBlob));
        CHECK_FALSE(fs::exists(tmp.path() / "out" / id.substr(5) / "instrument.json"));
    }
    SUBCASE("c: damaged cache entries are reported deleted and recomputed") {
        enum class Damage { Payload, Truncated, Key };
        for (const Damage damage : {Damage::Payload, Damage::Truncated, Damage::Key}) {
            const std::string tag = "damage-" + std::to_string(static_cast<int>(damage));
            const lib::CacheKey key = fillOne(loc, tag);
            const fs::path file = lib::cacheEntryPath(loc, key);
            switch (damage) {
            case Damage::Payload:
                flipByte(file, 88 + 5);
                break;
            case Damage::Truncated:
                fs::resize_file(file, 100);
                break;
            case Damage::Key:
                flipByte(file, 16 + 7);  // the stored key
                break;
            }
            int calls = 0;
            pme::BodyModes result;
            lib::Report report;
            bool hit = true;
            REQUIRE(lib::getOrCompute(
                        loc, key, result,
                        [&](pme::BodyModes& out) {
                            ++calls;
                            out = sampleBody();
                            return pme::Status::Ok;
                        },
                        report, &hit) == pme::Status::Ok);
            CHECK(calls == 1);
            CHECK_FALSE(hit);
            CHECK(equalBodies(result, sampleBody()));
            bool reported = false;
            for (const auto& d : report.diagnostics) {
                if (d.code == "integrity") {
                    reported = true;
                    CHECK(d.path == lib::pathToUtf8(file));
                }
            }
            CHECK(reported);
            // The bad file was deleted and replaced by a valid one.
            std::string payload;
            lib::Report check;
            CHECK(lib::cacheRead(loc, key, lib::CacheKind::Body, payload, check));
            CHECK(check.diagnostics.empty());
        }
    }
}

TEST_CASE("D5-R5 blob hashes are checked on every library load") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const std::string id = importBlobInstrument(loc, tmp.path() / "src");
    const fs::path json = loc.userInstruments() / id.substr(5) / "instrument.json";

    // A manifest hash that no longer matches the (intact) blob file is also an error.
    std::string text = slurp(json);
    const std::string hash = "sha256:9904ef62b0fd276a5a6e92e307d59c1ba1e6db066fbf3dd30f27b5f0b2944f3a";
    std::string changed = hash;
    changed[7] = changed[7] == '9' ? '8' : '9';
    std::size_t replaced = 0;
    for (auto at = text.find(hash); at != std::string::npos; at = text.find(hash, at + 1)) {
        text.replace(at, hash.size(), changed);  // the manifest hash and body.modalTableHash
        ++replaced;
    }
    REQUIRE(replaced == 2);
    spit(json, text);
    pme::loader::InstrumentFile out;
    lib::Report report;
    CHECK(lib::loadLibraryInstrument(loc, id, out, report) == pme::Status::InvalidArgument);
    CHECK(hasCode(report.errors, lib::code::kBlob));
}

TEST_CASE("D5-R5 every defect of a cache entry is rejected") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const auto reject = [&](const char* label, const auto& damage) {
        const lib::CacheKey key = fillOne(loc, label);
        const fs::path file = lib::cacheEntryPath(loc, key);
        damage(file);
        std::string payload;
        lib::Report report;
        CHECK_MESSAGE(!lib::cacheRead(loc, key, lib::CacheKind::Body, payload, report), label);
        CHECK_MESSAGE(hasCode(report.diagnostics, lib::code::kIntegrity), label);
        CHECK_MESSAGE(!fs::exists(file), label);
    };
    reject("magic", [](const fs::path& f) { flipByte(f, 0); });
    reject("container version", [](const fs::path& f) { flipByte(f, 8); });
    reject("kind", [](const fs::path& f) { flipByte(f, 12); });
    reject("stored key", [](const fs::path& f) { flipByte(f, 20); });
    reject("payload length", [](const fs::path& f) { flipByte(f, 48); });
    reject("payload digest", [](const fs::path& f) { flipByte(f, 60); });
    reject("payload byte", [](const fs::path& f) { flipByte(f, 90); });
    reject("empty file", [](const fs::path& f) { spit(f, ""); });
    reject("trailing bytes", [](const fs::path& f) { spit(f, slurp(f) + "x"); });

    // An entry written for another kind is not returned as this kind.
    const lib::CacheKey key = fillOne(loc, "kind-mismatch");
    std::string payload;
    lib::Report report;
    CHECK_FALSE(lib::cacheRead(loc, key, lib::CacheKind::LocalBridge, payload, report));
    CHECK(hasCode(report.diagnostics, lib::code::kIntegrity));
}

TEST_CASE("D5-R5 a payload with inconsistent lengths is discarded and recomputed") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, "inconsistent");
    pme::BodyModes bad = sampleBody();
    bad.phi.pop_back();  // phi.size() != numModes * numPoints, yet the container is intact
    std::string payload;
    lib::encodePayload(bad, payload);
    lib::Report report;
    REQUIRE(lib::cacheWrite(loc, key, lib::CacheKind::Body, payload, report) == pme::Status::Ok);

    int calls = 0;
    pme::BodyModes result;
    REQUIRE(lib::getOrCompute(
                loc, key, result,
                [&](pme::BodyModes& out) {
                    ++calls;
                    out = sampleBody();
                    return pme::Status::Ok;
                },
                report) == pme::Status::Ok);
    CHECK(calls == 1);
    CHECK(equalBodies(result, sampleBody()));
    CHECK(hasCode(report.diagnostics, lib::code::kIntegrity));
}
