// SPDX-License-Identifier: MIT
#pragma once

/// @file error.hpp
/// Errors and diagnostics of the instrument and preset library (D5). Non-real-time code: nothing
/// in `pme_plugin_library` may be called from `Engine::process` or from any audio-thread callback.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pme::plugin::library {

/// One error or diagnostic. Mirrors B1's `pme::loader::LoadError`: B1 errors pass through
/// unchanged (JSON Pointer path); file-system errors carry the file path in `path`.
struct LibraryError {
    std::string path;     ///< JSON Pointer, file path or library id, depending on the code.
    std::string code;     ///< One of the codes in `pme::plugin::library::code`.
    std::string message;  ///< Human-readable reason.
};

/// Closed set of codes (docs/dev/library.md, "Error codes"). The first block is B1's set, passed
/// through from the instrument loader and reused for preset files; the rest belong to the library.
namespace code {
inline constexpr std::string_view kParse = "parse";
inline constexpr std::string_view kType = "type";
inline constexpr std::string_view kMissing = "missing";
inline constexpr std::string_view kUnknownKey = "unknown_key";
inline constexpr std::string_view kRange = "range";
inline constexpr std::string_view kEnum = "enum";
inline constexpr std::string_view kVersion = "version";
inline constexpr std::string_view kMigration = "migration";
inline constexpr std::string_view kReference = "reference";
inline constexpr std::string_view kDuplicate = "duplicate";
inline constexpr std::string_view kStructure = "structure";
inline constexpr std::string_view kBlob = "blob";
// Library errors.
inline constexpr std::string_view kIo = "io";                 ///< File-system failure.
inline constexpr std::string_view kNotFound = "not-found";    ///< Id, file or directory missing.
inline constexpr std::string_view kInvalidId = "invalid-id";  ///< Malformed library id.
inline constexpr std::string_view kIntegrity = "integrity";   ///< A stored file failed its check.
inline constexpr std::string_view kCache = "cache";           ///< A cache entry was not stored.
// Diagnostics (never fail an operation).
inline constexpr std::string_view kInstrumentChanged = "instrument-changed";
inline constexpr std::string_view kUnknownParam = "unknown-param";
inline constexpr std::string_view kClamped = "clamped";
}  // namespace code

/// Result detail of a library call: `errors` explain a failure (the call returned a status other
/// than `Ok`); `diagnostics` are findings from calls that may have succeeded.
struct Report {
    std::vector<LibraryError> errors;       ///< Reasons for a failure.
    std::vector<LibraryError> diagnostics;  ///< Non-fatal findings.

    /// Append an error.
    void error(std::string path, std::string_view codeName, std::string message) {
        errors.push_back({std::move(path), std::string(codeName), std::move(message)});
    }
    /// Append a diagnostic.
    void diagnostic(std::string path, std::string_view codeName, std::string message) {
        diagnostics.push_back({std::move(path), std::string(codeName), std::move(message)});
    }
    /// True if an error or diagnostic with this code exists.
    [[nodiscard]] bool has(std::string_view codeName) const {
        for (const auto& e : errors) {
            if (e.code == codeName) {
                return true;
            }
        }
        for (const auto& d : diagnostics) {
            if (d.code == codeName) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace pme::plugin::library
