// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-INST "Instrument file" (where instruments, presets and body
// blobs live). D5-R1.
#include "pme/plugin/library/locations.hpp"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// <windows.h> must come first: <knownfolders.h> (pulled in by <shlobj.h>) needs EXTERN_C and GUID.
// The FOLDERID_* GUIDs are defined in uuid.lib, which CMakeLists.txt links explicitly.
#include <objbase.h>
#include <shlobj.h>
#include <windows.h>
#endif

#include "pme/plugin/ids.hpp"
#include "pme/plugin/library/files.hpp"

namespace pme::plugin::library {
namespace {

[[maybe_unused]] std::optional<std::string> processEnvironment(std::string_view name) {
    const std::string key(name);
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key.c_str()) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string text(value);
    std::free(value);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return text;
#else
    const char* value = std::getenv(key.c_str());  // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

/// A non-empty variable holding an absolute path, or nothing.
std::optional<std::filesystem::path> absoluteVariable(const EnvLookup& env, std::string_view name) {
    const std::optional<std::string> value = env(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    std::filesystem::path path = pathFromUtf8(*value);
    if (!path.is_absolute()) {
        return std::nullopt;
    }
    return path;
}

bool homeDirectory(const EnvLookup& env, std::filesystem::path& home, Report& report) {
    const std::optional<std::string> value = env("HOME");
    if (!value || value->empty()) {
        report.error("HOME", code::kIo, "HOME is not set; no user directory can be chosen");
        return false;
    }
    std::filesystem::path path = pathFromUtf8(*value);
    if (!path.is_absolute()) {
        report.error("HOME", code::kIo, "HOME is not an absolute path: " + *value);
        return false;
    }
    home = std::move(path);
    return true;
}

std::vector<std::filesystem::path> splitDataDirs(const std::string& text) {
    std::vector<std::filesystem::path> dirs;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(':', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string part = text.substr(start, end - start);
        if (!part.empty()) {
            std::filesystem::path path = pathFromUtf8(part);
            if (path.is_absolute()) {
                dirs.push_back(std::move(path));
            }
        }
        start = end + 1;
    }
    return dirs;
}

bool linuxLocations(const EnvLookup& env, Locations& out, Report& report) {
    const std::filesystem::path dataName = pathFromUtf8(kDataDirName);
    // Factory: first existing <dir>/<data dir> in XDG_DATA_DIRS.
    const std::optional<std::string> dataDirsVar = env("XDG_DATA_DIRS");
    std::vector<std::filesystem::path> dirs = splitDataDirs(
        dataDirsVar && !dataDirsVar->empty() ? *dataDirsVar : "/usr/local/share:/usr/share");
    if (dirs.empty()) {
        // Literal defaults: POSIX paths are not "absolute" under Windows path rules, so they must
        // not pass through splitDataDirs' absoluteness filter (an empty list would be UB below).
        dirs = {std::filesystem::path("/usr/local/share"), std::filesystem::path("/usr/share")};
    }
    std::filesystem::path factory = dirs.front() / dataName;
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir / dataName, ec)) {
            factory = dir / dataName;
            break;
        }
    }
    std::filesystem::path home;
    std::filesystem::path userBase;
    if (const auto xdg = absoluteVariable(env, "XDG_DATA_HOME")) {
        userBase = *xdg;
    } else {
        if (!homeDirectory(env, home, report)) {
            return false;
        }
        userBase = home / ".local" / "share";
    }
    std::filesystem::path cacheBase;
    if (const auto xdg = absoluteVariable(env, "XDG_CACHE_HOME")) {
        cacheBase = *xdg;
    } else {
        if (home.empty() && !homeDirectory(env, home, report)) {
            return false;
        }
        cacheBase = home / ".cache";
    }
    out.factory = std::move(factory);
    out.user = userBase / dataName;
    out.cache = cacheBase / dataName;
    return true;
}

bool macLocations(const EnvLookup& env, Locations& out, Report& report) {
    std::filesystem::path home;
    if (!homeDirectory(env, home, report)) {
        return false;
    }
    const std::filesystem::path dataName = pathFromUtf8(kDataDirName);
    out.factory = std::filesystem::path("/Library") / "Application Support" / dataName;
    out.user = home / "Library" / "Application Support" / dataName;
    out.cache = home / "Library" / "Caches" / dataName;
    return true;
}

#ifdef _WIN32
bool knownFolder(const KNOWNFOLDERID& id, std::filesystem::path& out, Report& report) {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        if (raw != nullptr) {
            CoTaskMemFree(raw);
        }
        report.error("", code::kIo, "SHGetKnownFolderPath failed");
        return false;
    }
    out = std::filesystem::path(std::wstring(raw));
    CoTaskMemFree(raw);
    if (!out.is_absolute()) {
        report.error(pathToUtf8(out), code::kIo, "known folder is not an absolute path");
        return false;
    }
    return true;
}
#endif

}  // namespace

Status locationsFromEnvironment(Platform platform, const EnvLookup& env, Locations& out,
                                Report& report) {
    try {
        Locations result;
        const bool ok = platform == Platform::Linux ? linuxLocations(env, result, report)
                                                    : macLocations(env, result, report);
        if (!ok) {
            return Status::InvalidArgument;
        }
        out = std::move(result);
        return Status::Ok;
    } catch (...) {
        report.error("", code::kIo, "unexpected failure while resolving locations");
        return Status::InvalidArgument;
    }
}

Status defaultLocations(Locations& out, Report& report) {
#ifdef _WIN32
    try {
        const std::filesystem::path dataName = pathFromUtf8(kDataDirName);
        std::filesystem::path programData;
        std::filesystem::path roaming;
        std::filesystem::path local;
        if (!knownFolder(FOLDERID_ProgramData, programData, report) ||
            !knownFolder(FOLDERID_RoamingAppData, roaming, report) ||
            !knownFolder(FOLDERID_LocalAppData, local, report)) {
            return Status::InvalidArgument;
        }
        out.factory = programData / dataName;
        out.user = roaming / dataName;
        out.cache = local / dataName / "Cache";
        return Status::Ok;
    } catch (...) {
        report.error("", code::kIo, "unexpected failure while resolving locations");
        return Status::InvalidArgument;
    }
#elif defined(__APPLE__)
    return locationsFromEnvironment(Platform::MacOs, processEnvironment, out, report);
#else
    return locationsFromEnvironment(Platform::Linux, processEnvironment, out, report);
#endif
}

Status parseLibraryId(std::string_view id, LibraryId& out, Report& report) {
    const auto bad = [&](std::string message) {
        report.error(std::string(id), code::kInvalidId, std::move(message));
        return Status::InvalidArgument;
    };
    LibraryId parsed;
    std::string_view name;
    if (id.starts_with("factory/")) {
        parsed.source = Source::Factory;
        name = id.substr(8);
    } else if (id.starts_with("user/")) {
        parsed.source = Source::User;
        name = id.substr(5);
    } else {
        return bad("a library id starts with factory/ or user/");
    }
    if (name.empty() || name.size() > 128) {
        return bad("the name after the prefix must have 1 to 128 characters");
    }
    if (name.front() == '.') {
        return bad("the name must not start with a dot");
    }
    for (const char c : name) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!allowed) {
            return bad("the name may contain only letters, digits, '-', '_' and '.'");
        }
    }
    parsed.name = std::string(name);
    out = std::move(parsed);
    return Status::Ok;
}

std::string formatLibraryId(Source source, std::string_view name) {
    return std::string(source == Source::Factory ? "factory/" : "user/") + std::string(name);
}

Status resolveInstrument(const Locations& locations, std::string_view id,
                         std::filesystem::path& instrumentJson, Report& report) {
    try {
        LibraryId parsed;
        if (parseLibraryId(id, parsed, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        const std::filesystem::path root = parsed.source == Source::Factory
                                               ? locations.factoryInstruments()
                                               : locations.userInstruments();
        std::filesystem::path file = root / pathFromUtf8(parsed.name) / "instrument.json";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            report.error(std::string(id), code::kNotFound,
                         "no instrument.json at " + pathToUtf8(file));
            return Status::InvalidArgument;
        }
        instrumentJson = std::move(file);
        return Status::Ok;
    } catch (...) {
        report.error(std::string(id), code::kIo, "unexpected failure while resolving");
        return Status::InvalidArgument;
    }
}

Status resolvePreset(const Locations& locations, std::string_view id,
                     std::filesystem::path& presetFile, Report& report) {
    try {
        LibraryId parsed;
        if (parseLibraryId(id, parsed, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        const std::filesystem::path root =
            parsed.source == Source::Factory ? locations.factoryPresets() : locations.userPresets();
        std::filesystem::path file = root / pathFromUtf8(parsed.name + kPresetExtension);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            report.error(std::string(id), code::kNotFound, "no preset file at " + pathToUtf8(file));
            return Status::InvalidArgument;
        }
        presetFile = std::move(file);
        return Status::Ok;
    } catch (...) {
        report.error(std::string(id), code::kIo, "unexpected failure while resolving");
        return Status::InvalidArgument;
    }
}

}  // namespace pme::plugin::library
