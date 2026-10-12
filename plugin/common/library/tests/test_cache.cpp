// SPDX-License-Identifier: MIT
// D5-R4 (hash-keyed cache of compiled body data) and D5-T3 (a structural edit invalidates exactly
// the affected entries). The producers are counting fakes: compileBody and buildLocalBridge are
// stubs until B2, B3 and C2.
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "pme/engine.hpp"
#include "pme/loader/instrument_file.hpp"
#include "pme/plugin/library/cache.hpp"
#include "test_support.hpp"

using namespace d5test;  // NOLINT(google-build-using-namespace)

namespace {

pme::BodyModes fakeBody(int numPoints) {
    pme::BodyModes b;
    b.numModes = 3;
    b.numPoints = numPoints;
    b.numChannels = 2;
    b.omega = {100.0, 200.5, 300.25};
    b.sigma = {1.0, 2.0, 3.0};
    for (int k = 0; k < 3; ++k) {
        for (int p = 0; p < numPoints; ++p) {
            b.phi.push_back(0.1 * k + 0.01 * p);
        }
    }
    b.alpha = {1, 2, 3, 4, 5, 6};
    b.beta = {6, 5, 4, 3, 2, 1};
    b.channelDelay = {0.001, 0.002};
    b.residualMobility.assign(static_cast<std::size_t>(numPoints), 0.5);
    return b;
}

pme::LocalBridge fakeLocal(int point, int maxModes, double fSplit) {
    pme::LocalBridge l;
    l.point = point;
    l.modeIndex = {0, 2};
    l.omega = {100.0, 300.25};
    l.sigma = {1.0, 3.0};
    l.phi = {0.1 * point, -0.2};
    l.conductanceHf = static_cast<double>(maxModes);
    l.fSplit = fSplit;
    return l;
}

bool same(const pme::BodyModes& a, const pme::BodyModes& b) {
    return a.numModes == b.numModes && a.numPoints == b.numPoints &&
           a.numChannels == b.numChannels && a.omega == b.omega && a.sigma == b.sigma &&
           a.phi == b.phi && a.alpha == b.alpha && a.beta == b.beta &&
           a.channelDelay == b.channelDelay && a.residualMobility == b.residualMobility;
}

bool same(const pme::LocalBridge& a, const pme::LocalBridge& b) {
    return a.point == b.point && a.modeIndex == b.modeIndex && a.omega == b.omega &&
           a.sigma == b.sigma && a.phi == b.phi && a.conductanceHf == b.conductanceHf &&
           a.fSplit == b.fSplit;
}

pme::InstrumentDesc loadDesc(const fs::path& dir) {
    const fs::path file = installBlobInstrument(dir);
    pme::loader::InstrumentFile loaded;
    std::vector<pme::loader::LoadError> errors;
    REQUIRE(pme::loader::loadInstrumentFile(file, true, loaded, errors) == pme::Status::Ok);
    return loaded.desc;
}

struct Counts {
    int body{};
    int local{};
};

/// One "instrument load": derive the keys, then get the body and every local bridge through the
/// cache with counting fake producers. The engine configuration travels alongside (as D6 will pass
/// it) but is not an input of the keys.
Counts loadThroughCache(const lib::Locations& loc, const pme::InstrumentDesc& desc,
                        const lib::ParamOverrides& overrides, std::string_view producerVersion,
                        const pme::EngineConfig& config) {
    (void)config;
    lib::BodyCacheKeys keys;
    lib::Report report;
    REQUIRE(lib::bodyCacheKeys(desc, overrides, producerVersion, keys, report) == pme::Status::Ok);
    Counts counts;
    const int points = static_cast<int>(desc.bridgePoints.size());
    pme::BodyModes body;
    REQUIRE(lib::getOrCompute(
                loc, keys.body, body,
                [&](pme::BodyModes& out) {
                    ++counts.body;
                    out = fakeBody(points);
                    return pme::Status::Ok;
                },
                report) == pme::Status::Ok);
    REQUIRE(keys.localBridges.size() == desc.bridgePoints.size());
    for (int p = 0; p < points; ++p) {
        pme::LocalBridge local;
        REQUIRE(lib::getOrCompute(
                    loc, keys.localBridges[static_cast<std::size_t>(p)], local,
                    [&](pme::LocalBridge& out) {
                        ++counts.local;
                        out = fakeLocal(p, keys.maxModes, keys.fSplit);
                        return pme::Status::Ok;
                    },
                    report) == pme::Status::Ok);
        CHECK(same(local, fakeLocal(p, keys.maxModes, keys.fSplit)));
    }
    CHECK(same(body, fakeBody(points)));
    return counts;
}

pme::EngineConfig config(double fs) {
    pme::EngineConfig c;
    c.fs = fs;
    c.maxBlock = 512;
    c.subBlock = 32;
    c.channels = 2;
    c.threads = 1;
    c.fMaxStrings = 16000.0;
    return c;
}

constexpr std::string_view kVersion = "engine-1.0";

}  // namespace

TEST_CASE("D5-T3 a structural edit invalidates exactly the affected cache entries") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const pme::InstrumentDesc start = loadDesc(tmp.path() / "src");
    REQUIRE(start.bridgePoints.size() >= 2);
    const int points = static_cast<int>(start.bridgePoints.size());
    const lib::ParamOverrides noOverrides;

    // Fill the cache: one body entry and one local-bridge entry per point.
    const Counts fill = loadThroughCache(loc, start, noOverrides, kVersion, config(48000.0));
    CHECK(fill.body == 1);
    CHECK(fill.local == points);
    const std::set<std::string> filled = fileNames(loc.cacheBody());
    REQUIRE(filled.size() == static_cast<std::size_t>(1 + points));

    // Each SUBCASE is one row of the dependency table: the same start, one edit.
    const auto check = [&](const pme::InstrumentDesc& edited, const lib::ParamOverrides& overrides,
                           std::string_view version, const pme::EngineConfig& cfg, int body,
                           int local) {
        const Counts counts = loadThroughCache(loc, edited, overrides, version, cfg);
        CHECK(counts.body == body);
        CHECK(counts.local == local);
        const std::set<std::string> after = fileNames(loc.cacheBody());
        // No entry is deleted; exactly the recomputed ones are new files.
        for (const auto& name : filled) {
            CHECK(after.count(name) == 1);
        }
        CHECK(after.size() == filled.size() + static_cast<std::size_t>(body + local));
    };

    SUBCASE("none: a second load recomputes nothing") {
        check(start, noOverrides, kVersion, config(48000.0), 0, 0);
    }
    SUBCASE("the sample rate does not enter any key") {
        check(start, noOverrides, kVersion, config(96000.0), 0, 0);
    }
    SUBCASE("a string tension") {
        pme::InstrumentDesc edited = start;
        edited.groups.at(0).segments.at(0).string.f1Target *= 1.01;
        edited.groups.at(0).segments.at(0).string.L *= 1.1;
        check(edited, noOverrides, kVersion, config(48000.0), 0, 0);
    }
    SUBCASE("a hammer mass") {
        pme::InstrumentDesc edited = start;
        auto& exciter = edited.groups.at(0).exciter;
        if (exciter.has_value()) {
            std::get<pme::HammerParams>(exciter->e).mass *= 2.0;
        } else {
            FAIL("group 0 has no exciter");
        }
        check(edited, noOverrides, kVersion, config(48000.0), 0, 0);
    }
    SUBCASE("the tuning") {
        pme::InstrumentDesc edited = start;
        edited.tuning.referencePitch = 442.0;
        edited.tuning.stretchCents.push_back(1.5);
        check(edited, noOverrides, kVersion, config(48000.0), 0, 0);
    }
    SUBCASE("strings.fMax and the continuous parameters") {
        check(start, {{6003, 18000.0}}, kVersion, config(48000.0), 0, 0);
        check(start, {{2001, -6.0}, {3001, 0.5}, {4101, 0.4}, {6003, 12000.0}}, kVersion,
              config(48000.0), 0, 0);
    }
    SUBCASE("the body.localBridgeModes override (7001)") {
        check(start, {{7001, 32.0}}, kVersion, config(48000.0), 0, points);
    }
    SUBCASE("Defaults::localBridgeFSplit") {
        pme::InstrumentDesc edited = start;
        edited.defaults.localBridgeFSplit += 500.0;
        check(edited, noOverrides, kVersion, config(48000.0), 0, points);
    }
    SUBCASE("Defaults::localBridgeModes") {
        pme::InstrumentDesc edited = start;
        edited.defaults.localBridgeModes = 16;
        check(edited, noOverrides, kVersion, config(48000.0), 0, points);
    }
    SUBCASE("plate thickness") {
        pme::InstrumentDesc edited = start;
        edited.body.plate.thickness += 0.001;
        check(edited, noOverrides, kVersion, config(48000.0), 1, points);
    }
    SUBCASE("the modalTableHash of a ModalTable body") {
        REQUIRE(start.body.source == pme::BodySource::ModalTable);
        pme::InstrumentDesc edited = start;
        edited.body.modalTableHash.back() = edited.body.modalTableHash.back() == '0' ? '1' : '0';
        check(edited, noOverrides, kVersion, config(48000.0), 1, points);
    }
    SUBCASE("one bridge point's position") {
        pme::InstrumentDesc edited = start;
        edited.bridgePoints.at(1).position.x += 0.01;
        check(edited, noOverrides, kVersion, config(48000.0), 1, points);
    }
    SUBCASE("a channel position") {
        pme::InstrumentDesc edited = start;
        edited.channels.at(0).position.z += 0.1;
        check(edited, noOverrides, kVersion, config(48000.0), 1, points);
    }
    SUBCASE("a different producer version recomputes everything") {
        check(start, noOverrides, "engine-1.1", config(48000.0), 1, points);
    }
}

TEST_CASE("D5-R4 keys are deterministic and independent of the unused bridge-point members") {
    TempDir tmp;
    const pme::InstrumentDesc desc = loadDesc(tmp.path() / "src");
    lib::BodyCacheKeys a;
    lib::BodyCacheKeys b;
    lib::Report report;
    REQUIRE(lib::bodyCacheKeys(desc, {}, kVersion, a, report) == pme::Status::Ok);
    REQUIRE(lib::bodyCacheKeys(desc, {}, kVersion, b, report) == pme::Status::Ok);
    CHECK(a.body == b.body);
    CHECK(a.localBridges == b.localBridges);
    CHECK(a.maxModes == desc.defaults.localBridgeModes);
    CHECK(a.fSplit == desc.defaults.localBridgeFSplit);
    // thetaB and conductanceH of a bridge point are not arguments of compileBody.
    pme::InstrumentDesc edited = desc;
    edited.bridgePoints.at(0).thetaB += 0.5;
    edited.bridgePoints.at(0).conductanceH *= 2.0;
    REQUIRE(lib::bodyCacheKeys(edited, {}, kVersion, b, report) == pme::Status::Ok);
    CHECK(a.body == b.body);
    CHECK(a.localBridges == b.localBridges);
    // All keys are distinct, and the local-bridge keys differ by point.
    std::set<std::string> hex;
    hex.insert(lib::toHex(a.body));
    for (const auto& k : a.localBridges) {
        hex.insert(lib::toHex(k));
    }
    CHECK(hex.size() == 1 + a.localBridges.size());
    // The 7001 override is clamped to its registry range.
    REQUIRE(lib::bodyCacheKeys(desc, {{7001, 1000.0}}, kVersion, b, report) == pme::Status::Ok);
    CHECK(b.maxModes == 64);
    CHECK(b.localBridges != a.localBridges);
}

TEST_CASE("D5-R4 the key encoding is stable") {
    // Golden values, computed independently with the byte layout of docs/dev/library.md (a
    // Python re-implementation of the encoding), guard the explicit field-by-field encoding
    // documented in docs/dev/library.md. If this fails after a deliberate change, bump
    // kCacheFormatVersion and update the value.
    const pme::BodyParams params;
    const std::vector<pme::Point> points = {{1.0, 2.0, 3.0}};
    const std::vector<pme::Point> channels = {{0.5, -0.5, 0.25}, {0.0, 0.0, 0.0}};
    const lib::CacheKey key = lib::bodyKey(params, points, channels, "golden-1");
    const lib::CacheKey local = lib::localBridgeKey(key, 0, 64, 2000.0);
    CHECK(lib::toHex(key) == "2d026126e83faf3a10ca786f0ab6a8ee34c3976ed7411b44319f5c03e949866f");
    CHECK(lib::toHex(local) == "8d8517a4db318cf4579bf3bd77e1531065a89b22d1cba994bdb0861a9636aa2b");
}

TEST_CASE("D5-R4 payloads round-trip and reject inconsistent lengths") {
    const pme::BodyModes body = fakeBody(2);
    std::string bytes;
    lib::encodePayload(body, bytes);
    pme::BodyModes back;
    REQUIRE(lib::decodePayload(bytes, back));
    CHECK(same(back, body));

    pme::BodyModes bad = body;
    bad.phi.pop_back();  // phi.size() != numModes * numPoints
    std::string badBytes;
    lib::encodePayload(bad, badBytes);
    pme::BodyModes untouched;
    untouched.numModes = 99;
    CHECK_FALSE(lib::decodePayload(badBytes, untouched));
    CHECK(untouched.numModes == 99);
    CHECK_FALSE(lib::decodePayload(bytes.substr(0, bytes.size() - 1), untouched));
    CHECK_FALSE(lib::decodePayload(bytes + "x", untouched));

    const pme::LocalBridge local = fakeLocal(1, 64, 2000.0);
    std::string localBytes;
    lib::encodePayload(local, localBytes);
    pme::LocalBridge localBack;
    REQUIRE(lib::decodePayload(localBytes, localBack));
    CHECK(same(localBack, local));
    pme::LocalBridge inconsistent = local;
    inconsistent.omega.push_back(1.0);
    std::string inconsistentBytes;
    lib::encodePayload(inconsistent, inconsistentBytes);
    CHECK_FALSE(lib::decodePayload(inconsistentBytes, localBack));
}

TEST_CASE("D5-R4 entries use the documented container") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, "v");
    lib::Report report;
    pme::BodyModes body;
    REQUIRE(lib::getOrCompute(
                loc, key, body,
                [](pme::BodyModes& out) {
                    out = fakeBody(2);
                    return pme::Status::Ok;
                },
                report) == pme::Status::Ok);
    const fs::path file = loc.cacheBody() / (lib::toHex(key) + ".bin");
    REQUIRE(fs::exists(file));
    const std::string bytes = slurp(file);
    std::string payload;
    lib::encodePayload(fakeBody(2), payload);
    CHECK(bytes.size() == 88 + payload.size());
    CHECK(bytes.substr(0, 8) == "PMECACHE");
    CHECK(bytes.substr(88) == payload);
    CHECK(static_cast<unsigned char>(bytes[8]) == lib::kCacheFormatVersion);
    CHECK(static_cast<unsigned char>(bytes[12]) == 1);  // kind: body
    CHECK(bytes.substr(16, 32).size() == 32);
    CHECK(file.parent_path() == loc.cache / "body");
}

TEST_CASE("D5-R4 a producer that fails stores nothing") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, "v");
    for (const auto status :
         {pme::Status::NotImplemented, pme::Status::InvalidArgument, pme::Status::Rejected}) {
        lib::Report report;
        pme::BodyModes body;
        body.numModes = 42;
        int calls = 0;
        CHECK(lib::getOrCompute(
                  loc, key, body,
                  [&](pme::BodyModes&) {
                      ++calls;
                      return status;
                  },
                  report) == status);
        CHECK(calls == 1);
        CHECK(body.numModes == 42);  // unchanged
        CHECK(fileNames(loc.cacheBody()).empty());
    }
}

TEST_CASE("D5-R4 concurrent getOrCompute from 4 threads leaves one valid entry") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, "threads");
    constexpr int kThreads = 4;
    std::atomic<int> producerCalls{0};
    std::vector<pme::BodyModes> results(kThreads);
    std::vector<pme::Status> statuses(kThreads, pme::Status::InvalidArgument);
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            lib::Report report;
            statuses[static_cast<std::size_t>(t)] = lib::getOrCompute(
                loc, key, results[static_cast<std::size_t>(t)],
                [&](pme::BodyModes& out) {
                    ++producerCalls;
                    out = fakeBody(2);
                    return pme::Status::Ok;
                },
                report);
        });
    }
    go.store(true);
    for (auto& th : threads) {
        th.join();
    }
    for (int t = 0; t < kThreads; ++t) {
        CHECK(statuses[static_cast<std::size_t>(t)] == pme::Status::Ok);
        CHECK(same(results[static_cast<std::size_t>(t)], fakeBody(2)));
    }
    CHECK(producerCalls.load() >= 1);
    CHECK(producerCalls.load() <= kThreads);
    // One entry, no temporary files left behind, and it is valid.
    const auto names = fileNames(loc.cacheBody());
    REQUIRE(names.size() == 1);
    CHECK(*names.begin() == lib::toHex(key) + ".bin");
    lib::Report report;
    pme::BodyModes again;
    bool hit = false;
    REQUIRE(lib::getOrCompute(
                loc, key, again, [](pme::BodyModes&) { return pme::Status::InvalidArgument; },
                report, &hit) == pme::Status::Ok);
    CHECK(hit);
    CHECK(report.diagnostics.empty());
    CHECK(same(again, fakeBody(2)));
}

TEST_CASE("D5-R4 pruneCache keeps the most recently used entries") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    std::vector<lib::CacheKey> keys;
    keys.reserve(4);
    lib::Report report;
    std::string payload;
    lib::encodePayload(fakeBody(2), payload);
    const auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 4; ++i) {
        keys.push_back(lib::bodyKey(pme::BodyParams{}, {}, {}, "prune-" + std::to_string(i)));
        REQUIRE(lib::cacheWrite(loc, keys.back(), lib::CacheKind::Body, payload, report) ==
                pme::Status::Ok);
        fs::last_write_time(lib::cacheEntryPath(loc, keys.back()),
                            now - std::chrono::hours(4 - i));  // entry 0 is the oldest
    }
    // A hit on entry 1 makes it the most recently used one.
    std::string out;
    REQUIRE(lib::cacheRead(loc, keys[1], lib::CacheKind::Body, out, report));
    // Files that are not cache entries are never touched.
    spit(loc.cacheBody() / "notes.txt", "keep");
    spit(loc.cache / "outside.bin", "keep");
    spit(loc.cacheBody() / "stale.bin.tmp-1", "leftover");
    fs::last_write_time(loc.cacheBody() / "stale.bin.tmp-1", now - std::chrono::hours(5));

    const std::uint64_t entrySize = fs::file_size(lib::cacheEntryPath(loc, keys[0]));
    lib::PruneResult result;
    REQUIRE(lib::pruneCache(loc, 2 * entrySize, result, report) == pme::Status::Ok);
    CHECK(result.entriesRemoved == 2);
    CHECK(result.bytesRemoved == 2 * entrySize);
    CHECK(result.bytesRemaining == 2 * entrySize);
    CHECK_FALSE(fs::exists(lib::cacheEntryPath(loc, keys[0])));
    CHECK(fs::exists(lib::cacheEntryPath(loc, keys[1])));  // touched by the hit
    CHECK_FALSE(fs::exists(lib::cacheEntryPath(loc, keys[2])));
    CHECK(fs::exists(lib::cacheEntryPath(loc, keys[3])));
    CHECK(fs::exists(loc.cacheBody() / "notes.txt"));
    CHECK(fs::exists(loc.cache / "outside.bin"));
    CHECK_FALSE(fs::exists(loc.cacheBody() / "stale.bin.tmp-1"));

    // Within the bound nothing more is deleted; an absent cache is fine.
    REQUIRE(lib::pruneCache(loc, 2 * entrySize, result, report) == pme::Status::Ok);
    CHECK(result.entriesRemoved == 0);
    REQUIRE(lib::pruneCache(loc, 0, result, report) == pme::Status::Ok);
    CHECK(result.entriesRemoved == 2);
    CHECK(fs::exists(loc.cacheBody() / "notes.txt"));
    TempDir empty;
    REQUIRE(lib::pruneCache(empty.locations(), lib::kDefaultCacheMaxBytes, result, report) ==
            pme::Status::Ok);
    CHECK(lib::kDefaultCacheMaxBytes == (std::uint64_t{1} << 30U));
}

// ---- Independent reviewer checks (D5 code review) -------------------------------------------

TEST_CASE("D5-R4 every BodyParams and PlateParams member changes the body key") {
    const std::vector<pme::Point> points = {{1.0, 2.0, 3.0}};
    const std::vector<pme::Point> channels = {{0.5, -0.5, 0.25}};
    const auto keyOf = [&](const pme::BodyParams& p) {
        return lib::toHex(lib::bodyKey(p, points, channels, "v"));
    };
    pme::BodyParams base;
    base.plate.ribs.push_back({{0, 0, 0}, {1, 0, 0}, 0.01, 0.02, 1e9, 400.0});
    base.plate.outline = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    base.plate.thicknessNx = 2;
    base.plate.thicknessNy = 2;
    base.plate.thicknessGrid = {0.01, 0.01, 0.01, 0.01};
    base.plate.bridges.push_back({{{0, 0, 0}, {1, 0, 0}}, 0.03, 0.04, 1e9, 600.0});
    base.modalTableHash = "sha256:00";
    const std::string reference = keyOf(base);

    std::vector<std::pair<std::string, pme::BodyParams>> edits;
    const auto edit = [&](const std::string& what, auto&& fn) {
        pme::BodyParams p = base;
        fn(p);
        edits.emplace_back(what, std::move(p));
    };
    edit("source", [](pme::BodyParams& p) { p.source = pme::BodySource::ModalTable; });
    edit("modalTableHash", [](pme::BodyParams& p) { p.modalTableHash = "sha256:01"; });
#define PME_D5_EDIT_DOUBLE(member) \
    edit(#member, [](pme::BodyParams& p) { p.plate.member += 0.125; })
    PME_D5_EDIT_DOUBLE(lengthX);
    PME_D5_EDIT_DOUBLE(lengthY);
    PME_D5_EDIT_DOUBLE(thickness);
    PME_D5_EDIT_DOUBLE(grainAngle);
    PME_D5_EDIT_DOUBLE(youngsL);
    PME_D5_EDIT_DOUBLE(youngsR);
    PME_D5_EDIT_DOUBLE(shearLR);
    PME_D5_EDIT_DOUBLE(poissonLR);
    PME_D5_EDIT_DOUBLE(poissonRL);
    PME_D5_EDIT_DOUBLE(density);
    PME_D5_EDIT_DOUBLE(moisture);
    PME_D5_EDIT_DOUBLE(dEdMcL);
    PME_D5_EDIT_DOUBLE(dEdMcR);
    PME_D5_EDIT_DOUBLE(dGdMc);
    PME_D5_EDIT_DOUBLE(dRhodMc);
    PME_D5_EDIT_DOUBLE(lossFactor);
    PME_D5_EDIT_DOUBLE(rimStiffness);
    PME_D5_EDIT_DOUBLE(fMax);
    PME_D5_EDIT_DOUBLE(rimRotationalStiffness);
#undef PME_D5_EDIT_DOUBLE
    edit("thicknessNx", [](pme::BodyParams& p) { ++p.plate.thicknessNx; });
    edit("thicknessNy", [](pme::BodyParams& p) { ++p.plate.thicknessNy; });
    edit("thicknessGrid value", [](pme::BodyParams& p) { p.plate.thicknessGrid[3] += 0.001; });
    edit("thicknessGrid size", [](pme::BodyParams& p) { p.plate.thicknessGrid.push_back(0.01); });
    edit("outline vertex", [](pme::BodyParams& p) { p.plate.outline[2].y += 0.1; });
    edit("outline size", [](pme::BodyParams& p) { p.plate.outline.push_back({0, 1, 0}); });
    edit("rib start", [](pme::BodyParams& p) { p.plate.ribs[0].start.z += 0.1; });
    edit("rib end", [](pme::BodyParams& p) { p.plate.ribs[0].end.y += 0.1; });
    edit("rib width", [](pme::BodyParams& p) { p.plate.ribs[0].width += 0.1; });
    edit("rib height", [](pme::BodyParams& p) { p.plate.ribs[0].height += 0.1; });
    edit("rib youngs", [](pme::BodyParams& p) { p.plate.ribs[0].youngs += 1.0; });
    edit("rib density", [](pme::BodyParams& p) { p.plate.ribs[0].density += 1.0; });
    edit("rib count", [](pme::BodyParams& p) { p.plate.ribs.push_back(p.plate.ribs[0]); });
    edit("bridge path vertex", [](pme::BodyParams& p) { p.plate.bridges[0].path[1].x += 0.1; });
    edit("bridge path size",
         [](pme::BodyParams& p) { p.plate.bridges[0].path.push_back({2, 0, 0}); });
    edit("bridge width", [](pme::BodyParams& p) { p.plate.bridges[0].width += 0.1; });
    edit("bridge height", [](pme::BodyParams& p) { p.plate.bridges[0].height += 0.1; });
    edit("bridge youngs", [](pme::BodyParams& p) { p.plate.bridges[0].youngs += 1.0; });
    edit("bridge density", [](pme::BodyParams& p) { p.plate.bridges[0].density += 1.0; });
    edit("bridge count", [](pme::BodyParams& p) { p.plate.bridges.push_back(p.plate.bridges[0]); });

    std::set<std::string> seen{reference};
    for (const auto& [what, params] : edits) {
        const std::string key = keyOf(params);
        CHECK_MESSAGE(key != reference, what);
        CHECK_MESSAGE(seen.insert(key).second, what, " collides with another edit");
    }
    // -0.0 and +0.0 are different bit patterns: "doubles by bit pattern".
    pme::BodyParams negZero = base;
    negZero.plate.lengthX = -0.0;
    CHECK(keyOf(negZero) != keyOf(base));
    // Point order and channel order matter.
    const std::vector<pme::Point> two = {{1, 0, 0}, {2, 0, 0}};
    const std::vector<pme::Point> swapped = {{2, 0, 0}, {1, 0, 0}};
    CHECK(lib::bodyKey(base, two, channels, "v") != lib::bodyKey(base, swapped, channels, "v"));
    CHECK(lib::bodyKey(base, points, two, "v") != lib::bodyKey(base, points, swapped, "v"));
    // The same positions given as bridge points or as channels must not collide.
    CHECK(lib::bodyKey(base, two, {}, "v") != lib::bodyKey(base, {}, two, "v"));
}

TEST_CASE("D5-R5 any byte flip, truncation or extension of a cache entry is never returned") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const lib::CacheKey key = lib::bodyKey(pme::BodyParams{}, {}, {}, "fuzz");
    const pme::BodyModes original = fakeBody(2);
    const auto fetch = [&](int& calls, lib::Report& report) {
        pme::BodyModes out;
        REQUIRE(lib::getOrCompute(
                    loc, key, out,
                    [&](pme::BodyModes& o) {
                        ++calls;
                        o = original;
                        return pme::Status::Ok;
                    },
                    report) == pme::Status::Ok);
        CHECK(same(out, original));
    };
    int calls = 0;
    lib::Report fill;
    fetch(calls, fill);
    REQUIRE(calls == 1);
    const fs::path file = lib::cacheEntryPath(loc, key);
    const std::string good = slurp(file);
    REQUIRE(good.size() > 100);

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto next = [&state]() {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    for (int round = 0; round < 150; ++round) {
        std::string bad = good;
        const auto mode = static_cast<unsigned>(next() % 3U);
        if (mode == 0U) {
            const auto at = static_cast<std::size_t>(next() % bad.size());
            bad[at] = static_cast<char>(bad[at] ^ static_cast<char>(1U + next() % 255U));
        } else if (mode == 1U) {
            bad.resize(static_cast<std::size_t>(next() % bad.size()));
        } else {
            bad.append(std::string(1U + next() % 16U, '\x5a'));
        }
        spit(file, bad);
        int recomputed = 0;
        lib::Report report;
        fetch(recomputed, report);
        CHECK_MESSAGE(recomputed == 1, "round ", round, " mode ", mode);
        CHECK_MESSAGE(hasCode(report.diagnostics, lib::code::kIntegrity), "round ", round);
        CHECK(slurp(file) == good);  // recomputed and rewritten identically
    }
}
