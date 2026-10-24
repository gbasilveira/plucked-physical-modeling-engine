// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-INST "Instrument file" (only sample-rate-independent data is
// stored), SPEC-BODY "Modal data (offline output)" and "Local bridge model" (the stored structs).
// D5-R4, D5-R5.
#include "pme/plugin/library/cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <system_error>

#include "byte_io.hpp"
#include "pme/loader/sha256.hpp"
#include "pme/plugin/library/files.hpp"

namespace pme::plugin::library {
namespace {

using detail::ByteReader;
using detail::ByteWriter;

constexpr std::string_view kMagic = "PMECACHE";               // 8 bytes
constexpr std::size_t kHeaderSize = 8 + 4 + 4 + 32 + 8 + 32;  // 88 bytes
constexpr std::string_view kEntrySuffix = ".bin";

std::array<std::uint8_t, 32> payloadDigest(std::string_view payload) {
    loader::Sha256 sha;
    sha.update(std::as_bytes(std::span<const char>(payload.data(), payload.size())));
    return sha.finish();
}

std::string encodeEntry(CacheKind kind, const CacheKey& key, std::string_view payload) {
    std::string bytes;
    bytes.reserve(kHeaderSize + payload.size());
    ByteWriter w(bytes);
    bytes.append(kMagic);
    w.u32(kCacheFormatVersion);
    w.u32(static_cast<std::uint32_t>(kind));
    w.raw(key.data(), key.size());
    w.u64(payload.size());
    const auto digest = payloadDigest(payload);
    w.raw(digest.data(), digest.size());
    bytes.append(payload);
    return bytes;
}

/// Empty string if the entry is valid (payload set), otherwise the reason.
std::string decodeEntry(std::string_view bytes, CacheKind kind, const CacheKey& key,
                        std::string_view& payload) {
    if (bytes.size() < kHeaderSize) {
        return "entry is truncated (shorter than its header)";
    }
    ByteReader r(bytes);
    if (r.bytes(kMagic.size()) != kMagic) {
        return "bad magic";
    }
    if (r.u32() != kCacheFormatVersion) {
        return "unsupported cache format version";
    }
    if (r.u32() != static_cast<std::uint32_t>(kind)) {
        return "entry holds a different kind of data";
    }
    const std::string_view storedKey = r.bytes(key.size());
    if (storedKey.size() != key.size() ||
        std::memcmp(storedKey.data(), key.data(), key.size()) != 0) {
        return "stored key differs from the requested key";
    }
    const std::uint64_t length = r.u64();
    const std::string_view storedDigest = r.bytes(32);
    if (!r.ok() || length != bytes.size() - kHeaderSize) {
        return "payload length does not match the file size";
    }
    const std::string_view body = bytes.substr(kHeaderSize);
    const auto digest = payloadDigest(body);
    if (std::memcmp(digest.data(), storedDigest.data(), digest.size()) != 0) {
        return "payload SHA-256 mismatch";
    }
    payload = body;
    return {};
}

bool allSizes(std::uint64_t expected, std::initializer_list<std::size_t> sizes) {
    return std::all_of(sizes.begin(), sizes.end(),
                       [expected](std::size_t s) { return s == expected; });
}

}  // namespace

std::filesystem::path cacheEntryPath(const Locations& locations, const CacheKey& key) {
    return locations.cacheBody() / pathFromUtf8(toHex(key) + std::string(kEntrySuffix));
}

void encodePayload(const BodyModes& value, std::string& out) {
    ByteWriter w(out);
    w.i32(value.numModes);
    w.i32(value.numPoints);
    w.i32(value.numChannels);
    w.f64s(value.omega);
    w.f64s(value.sigma);
    w.f64s(value.phi);
    w.f64s(value.alpha);
    w.f64s(value.beta);
    w.f64s(value.channelDelay);
    w.f64s(value.residualMobility);
}

bool decodePayload(std::string_view bytes, BodyModes& out) {
    ByteReader r(bytes);
    BodyModes v;
    v.numModes = r.i32();
    v.numPoints = r.i32();
    v.numChannels = r.i32();
    if (!r.f64s(v.omega) || !r.f64s(v.sigma) || !r.f64s(v.phi) || !r.f64s(v.alpha) ||
        !r.f64s(v.beta) || !r.f64s(v.channelDelay) || !r.f64s(v.residualMobility) || !r.atEnd()) {
        return false;
    }
    if (v.numModes < 0 || v.numPoints < 0 || v.numChannels < 0) {
        return false;
    }
    const auto k = static_cast<std::uint64_t>(v.numModes);
    const auto p = static_cast<std::uint64_t>(v.numPoints);
    const auto c = static_cast<std::uint64_t>(v.numChannels);
    if (!allSizes(k, {v.omega.size(), v.sigma.size()}) || v.phi.size() != k * p ||
        v.alpha.size() != c * k || v.beta.size() != c * k) {
        return false;
    }
    // channelDelay and residualMobility may be empty (a producer that does not fill them).
    if ((!v.channelDelay.empty() && v.channelDelay.size() != c) ||
        (!v.residualMobility.empty() && v.residualMobility.size() != p)) {
        return false;
    }
    out = std::move(v);
    return true;
}

void encodePayload(const LocalBridge& value, std::string& out) {
    ByteWriter w(out);
    w.i32(value.point);
    w.i32s(value.modeIndex);
    w.f64s(value.omega);
    w.f64s(value.sigma);
    w.f64s(value.phi);
    w.f64(value.conductanceHf);
    w.f64(value.fSplit);
}

bool decodePayload(std::string_view bytes, LocalBridge& out) {
    ByteReader r(bytes);
    LocalBridge v;
    v.point = r.i32();
    if (!r.i32s(v.modeIndex) || !r.f64s(v.omega) || !r.f64s(v.sigma) || !r.f64s(v.phi)) {
        return false;
    }
    v.conductanceHf = r.f64();
    v.fSplit = r.f64();
    if (!r.ok() || !r.atEnd() || v.point < 0) {
        return false;
    }
    if (!allSizes(v.modeIndex.size(), {v.omega.size(), v.sigma.size(), v.phi.size()})) {
        return false;
    }
    out = std::move(v);
    return true;
}

bool cacheRead(const Locations& locations, const CacheKey& key, CacheKind kind,
               std::string& payload, Report& report) {
    try {
        const std::filesystem::path file = cacheEntryPath(locations, key);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            return false;
        }
        std::string bytes;
        Report readReport;
        if (readFile(file, bytes, readReport) != Status::Ok) {
            for (auto& e : readReport.errors) {
                report.diagnostic(std::move(e.path), code::kCache, std::move(e.message));
            }
            return false;
        }
        std::string_view body;
        const std::string reason = decodeEntry(bytes, kind, key, body);
        if (!reason.empty()) {
            report.diagnostic(pathToUtf8(file), code::kIntegrity,
                              "cache entry discarded: " + reason);
            std::filesystem::remove(file, ec);
            return false;
        }
        payload.assign(body);
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);
        return true;
    } catch (...) {
        report.diagnostic("", code::kCache, "unexpected failure while reading the cache");
        return false;
    }
}

void cacheDiscard(const Locations& locations, const CacheKey& key, std::string_view why,
                  Report& report) {
    try {
        const std::filesystem::path file = cacheEntryPath(locations, key);
        report.diagnostic(pathToUtf8(file), code::kIntegrity,
                          "cache entry discarded: " + std::string(why));
        std::error_code ec;
        std::filesystem::remove(file, ec);
    } catch (...) {
        report.diagnostic("", code::kCache, "unexpected failure while discarding an entry");
    }
}

Status cacheWrite(const Locations& locations, const CacheKey& key, CacheKind kind,
                  std::string_view payload, Report& report) {
    try {
        return writeFileAtomic(cacheEntryPath(locations, key), encodeEntry(kind, key, payload),
                               report);
    } catch (...) {
        report.error("", code::kCache, "unexpected failure while writing the cache");
        return Status::InvalidArgument;
    }
}

Status pruneCache(const Locations& locations, std::uint64_t maxBytes, PruneResult& result,
                  Report& report) {
    try {
        struct Entry {
            std::filesystem::path path;
            std::uint64_t size{};
            std::filesystem::file_time_type time;
        };
        PruneResult pruned;
        const std::filesystem::path root = locations.cacheBody();
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            result = pruned;
            return Status::Ok;
        }
        std::vector<Entry> entries;
        std::uint64_t total = 0;
        const auto staleLimit =
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) {
                continue;
            }
            const std::string name = pathToUtf8(it->path().filename());
            const auto time = std::filesystem::last_write_time(it->path(), fileEc);
            if (fileEc) {
                continue;
            }
            if (name.find(".tmp-") != std::string::npos) {
                if (time < staleLimit) {
                    std::filesystem::remove(it->path(), fileEc);  // leftover of a crashed writer
                }
                continue;
            }
            if (!name.ends_with(kEntrySuffix)) {
                continue;  // not ours: never touched
            }
            const auto size = std::filesystem::file_size(it->path(), fileEc);
            if (fileEc) {
                continue;
            }
            entries.push_back({it->path(), static_cast<std::uint64_t>(size), time});
            total += size;
        }
        if (ec) {
            report.error(pathToUtf8(root), code::kIo, "cannot list the cache: " + ec.message());
            return Status::InvalidArgument;
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.time != b.time ? a.time < b.time : a.path < b.path;
        });
        for (const Entry& e : entries) {
            if (total <= maxBytes) {
                break;
            }
            std::error_code removeEc;
            if (std::filesystem::remove(e.path, removeEc)) {
                total -= e.size;
                ++pruned.entriesRemoved;
                pruned.bytesRemoved += e.size;
            } else if (removeEc) {
                report.diagnostic(pathToUtf8(e.path), code::kCache,
                                  "cannot delete cache entry: " + removeEc.message());
            }
        }
        pruned.bytesRemaining = total;
        result = pruned;
        return Status::Ok;
    } catch (...) {
        report.error("", code::kCache, "unexpected failure while pruning the cache");
        return Status::InvalidArgument;
    }
}

}  // namespace pme::plugin::library
