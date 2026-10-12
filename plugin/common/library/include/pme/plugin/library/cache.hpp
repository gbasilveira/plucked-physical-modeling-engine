// SPDX-License-Identifier: MIT
#pragma once

/// @file cache.hpp
/// Hash-keyed cache of compiled body data (D5-R4, D5-R5). Non-audio; allocates; never throws;
/// re-entrant (several threads or processes may share one cache directory).
///
/// SPEC sections used: SPEC-INST "Instrument file" (sample-rate-dependent data is never stored; it
/// is compiled on load), SPEC-API "Compile functions", SPEC-BODY "Local bridge model". The cache
/// stores `pme::BodyModes` (the result of `compileBody`) and `pme::LocalBridge` (the result of
/// `buildLocalBridge`) only: both are independent of the sample rate. Key encoding, container
/// format and the dependency table: docs/dev/library.md.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "pme/body.hpp"
#include "pme/instrument.hpp"
#include "pme/plugin/library/error.hpp"
#include "pme/plugin/library/locations.hpp"
#include "pme/plugin/library/preset.hpp"
#include "pme/status.hpp"

namespace pme::plugin::library {

/// Version of the key encoding and of the container format. Bumping it invalidates every entry.
inline constexpr std::uint32_t kCacheFormatVersion = 1;

/// Default size bound of `pruneCache`: 1 GiB.
inline constexpr std::uint64_t kDefaultCacheMaxBytes = std::uint64_t{1} << 30U;

/// SHA-256 key of one entry.
using CacheKey = std::array<std::uint8_t, 32>;

/// Lower-case hex of a key (the entry file name without `.bin`).
[[nodiscard]] std::string toHex(const CacheKey& key);

/// What an entry holds.
enum class CacheKind : std::uint8_t {
    Body = 1,         ///< `BodyModes`
    LocalBridge = 2,  ///< `LocalBridge`
};

/// Key of a body entry: SHA-256 over the explicit field-by-field encoding of exactly the
/// arguments of `compileBody` (`params`, the bridge-point positions in order, the channel
/// positions in order), plus `kCacheFormatVersion`, `interfacesVersionMajor` and
/// `producerVersion`.
[[nodiscard]] CacheKey bodyKey(const BodyParams& params, std::span<const Point> bridgePoints,
                               std::span<const Point> channels, std::string_view producerVersion);

/// Key of a local-bridge entry: the body key plus exactly the arguments of `buildLocalBridge`
/// (`point`, `maxModes`, `fSplit`).
[[nodiscard]] CacheKey localBridgeKey(const CacheKey& body, int point, int maxModes, double fSplit);

/// The keys of one instrument.
struct BodyCacheKeys {
    CacheKey body{};                     ///< Key of the `BodyModes` entry.
    std::vector<CacheKey> localBridges;  ///< One key per bridge point, in order.
    int maxModes{};                      ///< Effective M_b used in the local-bridge keys.
    double fSplit{};                     ///< Effective f_split used in the local-bridge keys.
};

/// Keys for an instrument and its parameter overrides. The effective `maxModes` and `fSplit` are
/// the instrument's `Defaults` (`localBridgeModes`, `localBridgeFSplit`) with `maxModes`
/// overridden by the structural parameter `body.localBridgeModes` (id 7001) when `overrides` sets
/// it. Strings, exciters, dampers, tuning, `strings.fMax`, the sample rate and the continuous
/// parameters are not inputs of `compileBody` or `buildLocalBridge` and do not enter any key.
[[nodiscard]] Status bodyCacheKeys(const InstrumentDesc& desc, const ParamOverrides& overrides,
                                   std::string_view producerVersion, BodyCacheKeys& out,
                                   Report& report);

// ---- Payloads -------------------------------------------------------------------------------

/// Serialise a struct field by field, little-endian, with lengths.
void encodePayload(const BodyModes& value, std::string& out);
/// ditto
void encodePayload(const LocalBridge& value, std::string& out);
/// Deserialise and check that the lengths are consistent (for example
/// `phi.size() == numModes * numPoints`). False leaves `out` unchanged.
[[nodiscard]] bool decodePayload(std::string_view bytes, BodyModes& out);
/// ditto: the arrays of a local bridge have the length of `modeIndex`.
[[nodiscard]] bool decodePayload(std::string_view bytes, LocalBridge& out);

// ---- Store ----------------------------------------------------------------------------------

/// Read an entry. True and `payload` set on a hit, after the magic, versions, kind, key, payload
/// length and payload SHA-256 were verified and the file's modification time was set to now (so
/// `pruneCache` sees it as recently used). A missing file is a plain miss. A bad entry is
/// reported as a diagnostic `integrity` with the file path, deleted, and is a miss; it is never
/// returned.
[[nodiscard]] bool cacheRead(const Locations& locations, const CacheKey& key, CacheKind kind,
                             std::string& payload, Report& report);

/// Delete an entry whose payload failed to decode, with the diagnostic `integrity`.
void cacheDiscard(const Locations& locations, const CacheKey& key, std::string_view why,
                  Report& report);

/// Write an entry atomically (temporary file in `cache/body/`, then rename). Failure is the error
/// `cache` or `io`.
[[nodiscard]] Status cacheWrite(const Locations& locations, const CacheKey& key, CacheKind kind,
                                std::string_view payload, Report& report);

/// Path of an entry file.
[[nodiscard]] std::filesystem::path cacheEntryPath(const Locations& locations, const CacheKey& key);

template <class T>
struct CacheTraits;
template <>
struct CacheTraits<BodyModes> {
    static constexpr CacheKind kind = CacheKind::Body;
};
template <>
struct CacheTraits<LocalBridge> {
    static constexpr CacheKind kind = CacheKind::LocalBridge;
};

/// Return the cached `T` for `key`, or call `producer(T&)` (a callable returning `Status`) and
/// store its result. The kind of entry follows from `T` (`BodyModes` or `LocalBridge`). On a
/// miss or an invalid entry the producer runs; on `NotImplemented` or any status other than `Ok`
/// nothing is stored and that status is returned with `out` unchanged. A failure to store only
/// adds a diagnostic `cache`: the computed value is still returned. `hit` (optional) tells
/// whether the value came from the cache.
template <class T, class Producer>
[[nodiscard]] Status getOrCompute(const Locations& locations, const CacheKey& key, T& out,
                                  Producer&& producer, Report& report, bool* hit = nullptr) {
    constexpr CacheKind kind = CacheTraits<T>::kind;
    if (hit != nullptr) {
        *hit = false;
    }
    std::string payload;
    if (cacheRead(locations, key, kind, payload, report)) {
        T cached;
        if (decodePayload(payload, cached)) {
            out = std::move(cached);
            if (hit != nullptr) {
                *hit = true;
            }
            return Status::Ok;
        }
        cacheDiscard(locations, key, "payload fails its consistency check", report);
    }
    T fresh;
    const Status status = std::forward<Producer>(producer)(fresh);
    if (status != Status::Ok) {
        return status;
    }
    payload.clear();
    encodePayload(fresh, payload);
    Report storeReport;
    if (cacheWrite(locations, key, kind, payload, storeReport) != Status::Ok) {
        for (auto& e : storeReport.errors) {
            report.diagnostic(std::move(e.path), code::kCache, std::move(e.message));
        }
    }
    out = std::move(fresh);
    return Status::Ok;
}

/// Outcome of `pruneCache`.
struct PruneResult {
    std::uint64_t entriesRemoved{};
    std::uint64_t bytesRemoved{};
    std::uint64_t bytesRemaining{};
};

/// Delete least-recently-used entries (by modification time; a hit touches the file) until the
/// total size of `cache/body/*.bin` is at most `maxBytes`. Also removes temporary files older than
/// one hour. Never deletes outside `cache/body/`.
[[nodiscard]] Status pruneCache(const Locations& locations, std::uint64_t maxBytes,
                                PruneResult& result, Report& report);

}  // namespace pme::plugin::library
