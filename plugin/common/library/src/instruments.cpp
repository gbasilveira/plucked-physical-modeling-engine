// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-INST "Instrument file" (canonical instrument text and blobs
// stored apart, referenced by hash); SPEC-API "Descriptions". D5-R3, D5-R5.
#include "pme/plugin/library/instruments.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <span>
#include <system_error>

#include "pme/loader/sha256.hpp"
#include "pme/plugin/library/files.hpp"

namespace pme::plugin::library {
namespace {

constexpr std::size_t kChunkSize = std::size_t{64} * 1024;

/// Removes a directory tree on scope exit unless released.
class TempDirGuard {
public:
    explicit TempDirGuard(std::filesystem::path dir) : dir_(std::move(dir)) {}
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;
    TempDirGuard(TempDirGuard&&) = delete;
    TempDirGuard& operator=(TempDirGuard&&) = delete;
    ~TempDirGuard() {
        if (!released_ && !dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
    }
    void release() { released_ = true; }

private:
    std::filesystem::path dir_;
    bool released_{false};
};

void passThrough(const std::vector<loader::LoadError>& loadErrors, Report& report) {
    for (const auto& e : loadErrors) {
        report.errors.push_back({e.path, e.code, e.message});
    }
}

/// Index i of a pointer `/blobs/<i>...`, or -1.
long blobIndexOf(const std::string& pointer) {
    constexpr std::string_view kPrefix = "/blobs/";
    if (!pointer.starts_with(kPrefix)) {
        return -1;
    }
    const char* first = pointer.data() + kPrefix.size();
    const char* last = pointer.data() + pointer.size();
    long index = 0;
    const auto r = std::from_chars(first, last, index);
    return r.ec == std::errc() ? index : -1;
}

/// B1 load of `file` with blob verification; blob errors also name the blob file.
Status loadVerified(const std::filesystem::path& file, loader::InstrumentFile& out,
                    Report& report) {
    std::vector<loader::LoadError> loadErrors;
    if (loader::loadInstrumentFile(file, /*verifyBlobs=*/true, out, loadErrors) == Status::Ok) {
        return Status::Ok;
    }
    loader::InstrumentFile manifest;
    bool haveManifest = false;
    const bool anyBlob = std::any_of(loadErrors.begin(), loadErrors.end(),
                                     [](const loader::LoadError& e) { return e.code == "blob"; });
    if (anyBlob) {
        std::string text;
        Report ignored;
        std::vector<loader::LoadError> manifestErrors;
        haveManifest = readFile(file, text, ignored) == Status::Ok &&
                       loader::loadInstrument(text, manifest, manifestErrors) == Status::Ok;
    }
    for (const auto& e : loadErrors) {
        std::string message = e.message;
        if (e.code == "blob" && haveManifest) {
            const long index = blobIndexOf(e.path);
            if (index >= 0 && static_cast<std::size_t>(index) < manifest.blobs.size()) {
                message +=
                    " (blob file " +
                    pathToUtf8(file.parent_path() /
                               pathFromUtf8(manifest.blobs[static_cast<std::size_t>(index)].path)) +
                    ")";
            }
        }
        report.errors.push_back({e.path, e.code, std::move(message)});
    }
    return Status::InvalidArgument;
}

}  // namespace

std::string reduceName(std::string_view name) {
    std::string out;
    bool pendingDash = false;
    for (const char raw : name) {
        char c = raw;
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) {
            if (pendingDash && !out.empty()) {
                out.push_back('-');
            }
            pendingDash = false;
            out.push_back(c);
        } else {
            pendingDash = true;
        }
    }
    if (out.size() > 64) {
        out.resize(64);
        while (!out.empty() && out.back() == '-') {
            out.pop_back();
        }
    }
    return out.empty() ? std::string("instrument") : out;
}

Status copyFileVerified(const std::filesystem::path& from, const std::filesystem::path& to,
                        std::string_view expectedHash, Report& report) {
    std::filesystem::path temp;
    try {
        std::error_code ec;
        std::filesystem::create_directories(to.parent_path(), ec);
        if (ec) {
            report.error(pathToUtf8(to.parent_path()), code::kIo,
                         "cannot create directory: " + ec.message());
            return Status::InvalidArgument;
        }
        temp = to;
        temp += ".tmp-" + uniqueToken();
        {
            std::ifstream in(from, std::ios::binary);
            if (!in) {
                report.error(pathToUtf8(from), code::kNotFound, "cannot open file to copy");
                return Status::InvalidArgument;
            }
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                report.error(pathToUtf8(temp), code::kIo, "cannot create temporary file");
                return Status::InvalidArgument;
            }
            std::string buffer(kChunkSize, '\0');
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize got = in.gcount();
                if (got > 0) {
                    out.write(buffer.data(), got);
                }
            }
            out.flush();
            if (in.bad() || !out) {
                out.close();
                std::filesystem::remove(temp, ec);
                report.error(pathToUtf8(to), code::kIo, "cannot copy file");
                return Status::InvalidArgument;
            }
        }
        std::string hex;
        Report local;
        if (hashFile(temp, hex, local) != Status::Ok || "sha256:" + hex != expectedHash) {
            std::filesystem::remove(temp, ec);
            report.error(pathToUtf8(to), code::kBlob,
                         "copy does not match " + std::string(expectedHash) +
                             " (the copy hashed to sha256:" + hex + ")");
            return Status::InvalidArgument;
        }
        if (!renameWithRetry(temp, to, ec)) {
            const std::string reason = ec.message();
            std::filesystem::remove(temp, ec);
            report.error(pathToUtf8(to), code::kIo, "cannot place file: " + reason);
            return Status::InvalidArgument;
        }
        return Status::Ok;
    } catch (...) {
        std::error_code ec;
        if (!temp.empty()) {
            std::filesystem::remove(temp, ec);
        }
        report.error(pathToUtf8(to), code::kIo, "unexpected failure while copying");
        return Status::InvalidArgument;
    }
}

Status loadLibraryInstrument(const Locations& locations, std::string_view id,
                             loader::InstrumentFile& out, Report& report) {
    try {
        std::filesystem::path file;
        if (resolveInstrument(locations, id, file, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        loader::InstrumentFile loaded;
        if (loadVerified(file, loaded, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        out = std::move(loaded);
        return Status::Ok;
    } catch (...) {
        report.error(std::string(id), code::kIo, "unexpected failure while loading");
        return Status::InvalidArgument;
    }
}

Status instrumentHash(const loader::InstrumentFile& file, std::string& hash, Report& report) {
    try {
        std::string text;
        if (loader::writeInstrument(file, text) != Status::Ok) {
            report.error("", code::kStructure, "instrument cannot be written (non-finite number)");
            return Status::InvalidArgument;
        }
        hash = sha256Tagged(text);
        return Status::Ok;
    } catch (...) {
        report.error("", code::kIo, "unexpected failure while hashing");
        return Status::InvalidArgument;
    }
}

Status importInstrument(const Locations& locations,
                        const std::filesystem::path& sourceInstrumentJson, ImportResult& result,
                        Report& report) {
    std::filesystem::path staging;
    try {
        loader::InstrumentFile file;
        std::vector<loader::LoadError> loadErrors;
        if (loader::loadInstrumentFile(sourceInstrumentJson, /*verifyBlobs=*/true, file,
                                       loadErrors) != Status::Ok) {
            passThrough(loadErrors, report);
            return Status::InvalidArgument;
        }
        std::string text;
        if (loader::writeInstrument(file, text) != Status::Ok) {
            report.error("", code::kStructure, "instrument cannot be written (non-finite number)");
            return Status::InvalidArgument;
        }
        const std::string hash = sha256Tagged(text);
        const std::string base = reduceName(file.desc.name);
        const std::filesystem::path sourceDir = sourceInstrumentJson.parent_path();
        const std::filesystem::path root = locations.userInstruments();

        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec) {
            report.error(pathToUtf8(root), code::kIo, "cannot create directory: " + ec.message());
            return Status::InvalidArgument;
        }
        staging = root / (".import-" + uniqueToken());
        std::filesystem::create_directories(staging, ec);
        if (ec) {
            report.error(pathToUtf8(staging), code::kIo,
                         "cannot create directory: " + ec.message());
            return Status::InvalidArgument;
        }
        TempDirGuard guard(staging);
        for (const auto& blob : file.blobs) {
            const std::filesystem::path relative = pathFromUtf8(blob.path);
            if (copyFileVerified(sourceDir / relative, staging / relative, blob.hash, report) !=
                Status::Ok) {
                return Status::InvalidArgument;
            }
        }
        if (writeFileAtomic(staging / "instrument.json", text, report) != Status::Ok) {
            return Status::InvalidArgument;
        }

        constexpr int kMaxCandidates = 1000;
        for (int n = 1; n <= kMaxCandidates; ++n) {
            const std::string name = n == 1 ? base : base + "-" + std::to_string(n);
            const std::filesystem::path target = root / pathFromUtf8(name);
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (std::filesystem::exists(target, ec)) {
                    std::string existing;
                    Report ignored;
                    if (readFile(target / "instrument.json", existing, ignored) == Status::Ok &&
                        sha256Tagged(existing) == hash) {
                        result.id = formatLibraryId(Source::User, name);
                        result.alreadyPresent = true;
                        result.sourceVersion = file.sourceVersion;
                        return Status::Ok;  // the guard removes the staging directory
                    }
                    break;  // a different instrument: try the next suffix
                }
                std::filesystem::rename(staging, target, ec);
                if (!ec) {
                    guard.release();
                    result.id = formatLibraryId(Source::User, name);
                    result.alreadyPresent = false;
                    result.sourceVersion = file.sourceVersion;
                    return Status::Ok;
                }
                if (!std::filesystem::exists(target, ec)) {
                    report.error(pathToUtf8(target), code::kIo, "cannot move directory into place");
                    return Status::InvalidArgument;
                }
                // Another process created the target meanwhile: look at it again.
            }
        }
        report.error(pathToUtf8(root), code::kIo, "no free directory name for " + base);
        return Status::InvalidArgument;
    } catch (...) {
        if (!staging.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(staging, ec);
        }
        report.error(pathToUtf8(sourceInstrumentJson), code::kIo,
                     "unexpected failure while importing");
        return Status::InvalidArgument;
    }
}

Status exportInstrument(const Locations& locations, std::string_view id,
                        const std::filesystem::path& targetDir,
                        std::filesystem::path& writtenInstrumentJson, Report& report) {
    try {
        LibraryId parsed;
        if (parseLibraryId(id, parsed, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        std::filesystem::path sourceFile;
        if (resolveInstrument(locations, id, sourceFile, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        loader::InstrumentFile file;
        if (loadVerified(sourceFile, file, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        std::string text;
        if (loader::writeInstrument(file, text) != Status::Ok) {
            report.error(std::string(id), code::kStructure,
                         "instrument cannot be written (non-finite number)");
            return Status::InvalidArgument;
        }
        const std::filesystem::path sourceDir = sourceFile.parent_path();
        const std::filesystem::path destination = targetDir / pathFromUtf8(parsed.name);
        for (const auto& blob : file.blobs) {
            const std::filesystem::path relative = pathFromUtf8(blob.path);
            if (copyFileVerified(sourceDir / relative, destination / relative, blob.hash, report) !=
                Status::Ok) {
                return Status::InvalidArgument;
            }
        }
        // The instrument file goes last: a reader that finds it finds every blob.
        const std::filesystem::path target = destination / "instrument.json";
        if (writeFileAtomic(target, text, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        writtenInstrumentJson = target;
        return Status::Ok;
    } catch (...) {
        report.error(std::string(id), code::kIo, "unexpected failure while exporting");
        return Status::InvalidArgument;
    }
}

std::vector<ListEntry> listInstruments(const Locations& locations) {
    std::vector<ListEntry> entries;
    const auto scan = [&entries](const std::filesystem::path& root, Source source) {
        try {
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                return;
            }
            std::vector<std::filesystem::path> dirs;
            for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code typeEc;
                if (it->is_directory(typeEc)) {
                    const std::string name = pathToUtf8(it->path().filename());
                    if (!name.empty() && name.front() != '.') {
                        dirs.push_back(it->path());
                    }
                }
            }
            std::sort(dirs.begin(), dirs.end());
            for (const auto& dir : dirs) {
                ListEntry entry;
                entry.source = source;
                const std::string name = pathToUtf8(dir.filename());
                entry.id = formatLibraryId(source, name);
                entry.schemaVersion = -1;
                loader::InstrumentFile file;
                std::vector<loader::LoadError> loadErrors;
                const std::filesystem::path json = dir / "instrument.json";
                if (!std::filesystem::is_regular_file(json, ec)) {
                    entry.errors.push_back(
                        {pathToUtf8(json), std::string(code::kNotFound), "no instrument.json"});
                } else if (loader::loadInstrumentFile(json, /*verifyBlobs=*/false, file,
                                                      loadErrors) == Status::Ok) {
                    entry.name = file.desc.name;
                    entry.schemaVersion = file.sourceVersion;
                } else {
                    for (const auto& e : loadErrors) {
                        entry.errors.push_back({e.path, e.code, e.message});
                    }
                }
                entries.push_back(std::move(entry));
            }
        } catch (...) {
            return;  // A directory that cannot be scanned contributes no entries.
        }
    };
    scan(locations.factoryInstruments(), Source::Factory);
    scan(locations.userInstruments(), Source::User);
    return entries;
}

}  // namespace pme::plugin::library
