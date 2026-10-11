// SPDX-License-Identifier: MIT
// Non-real-time loader and canonical writer of instrument files (B1-R2 to B1-R5).
//
// SPEC sections implemented: SPEC-INST "Instrument definitions" and "Instrument file"; SPEC-API
// "Descriptions". The format is documented in docs/dev/instrument-format.md. This header is
// internal to the loader (not part of the frozen public interfaces) and is never linked into
// `pme_engine` or a `pme_<module>` target. Nothing here is reachable from `Engine::process`.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pme/instrument.hpp"
#include "pme/status.hpp"

namespace pme::loader {

/// Current schema version (docs/dev/instrument-format.md, "Versions").
inline constexpr int kCurrentSchemaVersion = 1;

/// One validation error: a JSON Pointer (RFC 6901), a code from the closed list below and a
/// human-readable message that names the expected value (B1-R5). Codes: `parse`, `type`,
/// `missing`, `unknown_key`, `range`, `enum`, `version`, `migration`, `reference`, `duplicate`,
/// `structure`, `blob`.
struct LoadError {
    std::string path;     ///< JSON Pointer of the offending value; empty for the document root.
    std::string code;     ///< One of the codes above.
    std::string message;  ///< Human-readable reason.
};

/// One array inside a blob file (B1-R3).
struct BlobArray {
    std::string name;                ///< Array name.
    std::string dtype;               ///< `float32`, `float64`, `int32` or `uint32`.
    std::string unit;                ///< Physical unit of the elements.
    std::vector<std::int64_t> shape;  ///< Positive extents.
    std::int64_t offset{};           ///< Bytes from the start of the file, aligned to the dtype size.
};

/// One blob file of the manifest (B1-R3).
struct BlobEntry {
    std::string hash;  ///< `sha256:` followed by 64 lower-case hex digits of the whole file.
    std::string path;  ///< Relative to the instrument file's directory, forward slashes.
    std::string tool;  ///< Producing tool.
    std::string toolVersion;         ///< Producing tool version.
    std::vector<BlobArray> arrays;   ///< Arrays stored in the file.
};

/// A loaded instrument file. The manifest is not part of `InstrumentDesc` (frozen), so it is
/// returned beside it.
struct InstrumentFile {
    InstrumentDesc desc;             ///< The instrument description.
    std::vector<BlobEntry> blobs;    ///< The blob manifest.
    int sourceVersion{};             ///< Schema version of the loaded text (before migration).
};

/// Parse, migrate, validate and read an instrument document held in memory.
///
/// Blob consistency that needs no file (alignment, overlap, duplicates, hash references) is
/// checked; file-dependent checks need `loadInstrumentFile`.
/// Thread: non-audio. Allocates: yes. Never throws.
/// @return `Ok` and `out` set, or `InvalidArgument` with at least one entry in `errors` (all
/// errors found, at most 100, sorted by path then code) and `out` unchanged.
[[nodiscard]] Status loadInstrument(std::string_view json, InstrumentFile& out,
                                    std::vector<LoadError>& errors);

/// As `loadInstrument`, reading `path`, and additionally checking the blob files next to it:
/// offsets and sizes against the file length when the file exists, and, when `verifyBlobs`, that
/// every file exists and matches its SHA-256 (the engine-side load passes false).
[[nodiscard]] Status loadInstrumentFile(const std::filesystem::path& path, bool verifyBlobs,
                                        InstrumentFile& out, std::vector<LoadError>& errors);

/// Write the canonical text (docs/dev/instrument-format.md, "Canonical form"): current schema
/// version, 2-space indentation, keys in schema order, shortest round-trip numbers, LF, final
/// newline. `InvalidArgument` if a number is not finite.
/// Thread: non-audio. Allocates: yes. Never throws.
[[nodiscard]] Status writeInstrument(const InstrumentFile& file, std::string& json);

}  // namespace pme::loader
