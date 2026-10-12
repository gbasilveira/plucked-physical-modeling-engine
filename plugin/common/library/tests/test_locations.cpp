// SPDX-License-Identifier: MIT
// D5-R1: install locations and library ids.
#include <doctest/doctest.h>

#include <cstdlib>
#include <map>
#include <optional>
#include <string>

#include "pme/plugin/ids.hpp"
#include "pme/plugin/library/locations.hpp"
#include "test_support.hpp"

using namespace d5test;  // NOLINT(google-build-using-namespace)

namespace {

lib::EnvLookup envOf(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](std::string_view name) -> std::optional<std::string> {
        const auto it = vars.find(std::string(name));
        if (it == vars.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

}  // namespace

// The POSIX tables use POSIX absolute paths (/home/u), which are not absolute on Windows.
#ifndef _WIN32
TEST_CASE("D5-R1 linux locations follow the XDG table") {
    TempDir tmp;
    const fs::path dataA = tmp.path() / "dataA";
    const fs::path dataB = tmp.path() / "dataB";
    fs::create_directories(dataB / pme::plugin::kDataDirName);  // only the second one exists
    const std::string dirs = lib::pathToUtf8(dataA) + ":" + lib::pathToUtf8(dataB);
    lib::Locations loc;
    lib::Report report;
    REQUIRE(lib::locationsFromEnvironment(lib::Platform::Linux,
                                          envOf({{"HOME", "/home/u"},
                                                 {"XDG_DATA_DIRS", dirs},
                                                 {"XDG_DATA_HOME", "/xdg/data"},
                                                 {"XDG_CACHE_HOME", "/xdg/cache"}}),
                                          loc, report) == pme::Status::Ok);
    CHECK(loc.factory == dataB / pme::plugin::kDataDirName);
    CHECK(loc.user == fs::path("/xdg/data") / pme::plugin::kDataDirName);
    CHECK(loc.cache == fs::path("/xdg/cache") / pme::plugin::kDataDirName);
    CHECK(loc.userInstruments() == loc.user / "instruments");
    CHECK(loc.factoryPresets() == loc.factory / "presets");
    CHECK(loc.cacheBody() == loc.cache / "body");
}

TEST_CASE("D5-R1 linux defaults come from HOME and the standard data dirs") {
    lib::Locations loc;
    lib::Report report;
    REQUIRE(lib::locationsFromEnvironment(lib::Platform::Linux, envOf({{"HOME", "/home/u"}}), loc,
                                          report) == pme::Status::Ok);
    CHECK(loc.user == fs::path("/home/u/.local/share") / pme::plugin::kDataDirName);
    CHECK(loc.cache == fs::path("/home/u/.cache") / pme::plugin::kDataDirName);
    // The first existing of /usr/local/share/pme and /usr/share/pme, else the first.
    fs::path expected = fs::path("/usr/local/share") / pme::plugin::kDataDirName;
    const fs::path usrShare = fs::path("/usr/share") / pme::plugin::kDataDirName;
    if (!fs::is_directory(expected) && fs::is_directory(usrShare)) {
        expected = usrShare;
    }
    CHECK(loc.factory == expected);
}

TEST_CASE("D5-R1 linux ignores relative XDG values") {
    lib::Locations loc;
    lib::Report report;
    REQUIRE(lib::locationsFromEnvironment(
                lib::Platform::Linux,
                envOf({{"HOME", "/home/u"}, {"XDG_DATA_HOME", "rel/data"}, {"XDG_CACHE_HOME", ""}}),
                loc, report) == pme::Status::Ok);
    CHECK(loc.user == fs::path("/home/u/.local/share") / pme::plugin::kDataDirName);
    CHECK(loc.cache == fs::path("/home/u/.cache") / pme::plugin::kDataDirName);
}

TEST_CASE("D5-R1 macos locations follow the table") {
    lib::Locations loc;
    lib::Report report;
    REQUIRE(lib::locationsFromEnvironment(lib::Platform::MacOs, envOf({{"HOME", "/Users/u"}}), loc,
                                          report) == pme::Status::Ok);
    CHECK(loc.factory == fs::path("/Library/Application Support") / pme::plugin::kDataDirName);
    CHECK(loc.user == fs::path("/Users/u/Library/Application Support") / pme::plugin::kDataDirName);
    CHECK(loc.cache == fs::path("/Users/u/Library/Caches") / pme::plugin::kDataDirName);
}

#endif

TEST_CASE("D5-R1 a missing or relative HOME is an error") {
    for (const auto platform : {lib::Platform::Linux, lib::Platform::MacOs}) {
        lib::Locations loc;
        loc.user = "unchanged";
        {
            lib::Report report;
            CHECK(lib::locationsFromEnvironment(platform, envOf({}), loc, report) ==
                  pme::Status::InvalidArgument);
            CHECK(hasCode(report.errors, lib::code::kIo));
            CHECK(loc.user == fs::path("unchanged"));
        }
        {
            lib::Report report;
            CHECK(lib::locationsFromEnvironment(platform, envOf({{"HOME", "relative/home"}}), loc,
                                                report) == pme::Status::InvalidArgument);
            CHECK(hasCode(report.errors, lib::code::kIo));
        }
        {
            lib::Report report;
            CHECK(lib::locationsFromEnvironment(platform, envOf({{"HOME", ""}}), loc, report) ==
                  pme::Status::InvalidArgument);
        }
    }
}

TEST_CASE("D5-R1 defaultLocations reads the process environment or the known folders") {
    lib::Locations loc;
    lib::Report report;
#ifdef _WIN32
    REQUIRE(lib::defaultLocations(loc, report) == pme::Status::Ok);
    CHECK(loc.factory.is_absolute());
    CHECK(loc.user.is_absolute());
    CHECK(loc.cache.is_absolute());
    CHECK(loc.factory.filename() == pme::plugin::kDataDirName);
    CHECK(loc.user.filename() == pme::plugin::kDataDirName);
    CHECK(loc.cache.filename() == "Cache");
    CHECK(loc.cache.parent_path().filename() == pme::plugin::kDataDirName);
#else
    TempDir tmp;
    const std::string home = lib::pathToUtf8(tmp.path());
    // Restore the environment this test changes, whatever the outcome.
    struct EnvRestore {
        std::optional<std::string> home;
        std::optional<std::string> data;
        std::optional<std::string> cache;
        static std::optional<std::string> get(const char* name) {
            const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
            return v == nullptr ? std::nullopt : std::optional<std::string>(v);
        }
        static void put(const char* name, const std::optional<std::string>& v) {
            if (v) {
                setenv(name, v->c_str(), 1);
            } else {
                unsetenv(name);
            }
        }
        EnvRestore()
            : home(get("HOME")), data(get("XDG_DATA_HOME")), cache(get("XDG_CACHE_HOME")) {}
        ~EnvRestore() {
            put("HOME", home);
            put("XDG_DATA_HOME", data);
            put("XDG_CACHE_HOME", cache);
        }
        EnvRestore(const EnvRestore&) = delete;
        EnvRestore& operator=(const EnvRestore&) = delete;
    } restore;
    REQUIRE(setenv("HOME", home.c_str(), 1) == 0);
    REQUIRE(unsetenv("XDG_DATA_HOME") == 0);
    REQUIRE(unsetenv("XDG_CACHE_HOME") == 0);
    REQUIRE(lib::defaultLocations(loc, report) == pme::Status::Ok);
#ifdef __APPLE__
    CHECK(loc.user == tmp.path() / "Library" / "Application Support" / pme::plugin::kDataDirName);
    CHECK(loc.cache == tmp.path() / "Library" / "Caches" / pme::plugin::kDataDirName);
#else
    CHECK(loc.user == tmp.path() / ".local" / "share" / pme::plugin::kDataDirName);
    CHECK(loc.cache == tmp.path() / ".cache" / pme::plugin::kDataDirName);
    const fs::path xdgData = tmp.path() / "xdg-data";
    REQUIRE(setenv("XDG_DATA_HOME", lib::pathToUtf8(xdgData).c_str(), 1) == 0);
    REQUIRE(lib::defaultLocations(loc, report) == pme::Status::Ok);
    CHECK(loc.user == xdgData / pme::plugin::kDataDirName);
    REQUIRE(unsetenv("XDG_DATA_HOME") == 0);
#endif
    REQUIRE(unsetenv("HOME") == 0);
    lib::Report missing;
    CHECK(lib::defaultLocations(loc, missing) == pme::Status::InvalidArgument);
    CHECK(hasCode(missing.errors, lib::code::kIo));
#endif
}

TEST_CASE("D5-R1 ids resolve and never escape their root") {
    TempDir tmp;
    const lib::Locations loc = tmp.locations();
    const fs::path factoryFile =
        installExample("test-monochord", loc.factoryInstruments() / "mono");
    const fs::path userFile = installExample("test-unison-pair", loc.userInstruments() / "pair");

    fs::path out;
    lib::Report report;
    REQUIRE(lib::resolveInstrument(loc, "factory/mono", out, report) == pme::Status::Ok);
    CHECK(out == factoryFile);
    REQUIRE(lib::resolveInstrument(loc, "user/pair", out, report) == pme::Status::Ok);
    CHECK(out == userFile);

    // A user entry never shadows a factory entry: the prefix makes the id unique.
    installExample("test-unison-pair", loc.userInstruments() / "mono");
    REQUIRE(lib::resolveInstrument(loc, "factory/mono", out, report) == pme::Status::Ok);
    CHECK(out == factoryFile);
    REQUIRE(lib::resolveInstrument(loc, "user/mono", out, report) == pme::Status::Ok);
    CHECK(out == loc.userInstruments() / "mono" / "instrument.json");

    for (const char* bad :
         {"../x", "user/../x", "user/..", "user/a/b", "/etc/passwd", "user//x", "user/.hidden",
          "user/a\\b", "user/c:d", "user/", "", "mono", "factory/../../etc", "other/mono"}) {
        lib::Report badReport;
        fs::path unchanged = "unchanged";
        CHECK_MESSAGE(
            lib::resolveInstrument(loc, bad, unchanged, badReport) == pme::Status::InvalidArgument,
            bad);
        CHECK_MESSAGE(hasCode(badReport.errors, lib::code::kInvalidId), bad);
        CHECK(unchanged == fs::path("unchanged"));
    }
    lib::Report missing;
    CHECK(lib::resolveInstrument(loc, "user/none", out, missing) == pme::Status::InvalidArgument);
    CHECK(hasCode(missing.errors, lib::code::kNotFound));
}
