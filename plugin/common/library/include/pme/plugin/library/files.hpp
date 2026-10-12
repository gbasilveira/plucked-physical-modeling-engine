// SPDX-License-Identifier: MIT
#pragma once

/// @file files.hpp
/// File helpers of the library (D5): UTF-8 paths and atomic writes. Non-audio; allocates; never
/// throws.
///
/// Atomic writes: the bytes go to a temporary file in the target's directory, which is then
/// renamed over the target with `std::filesystem::rename`. On POSIX the rename replaces the
/// target atomically. On Windows the standard library's rename replaces an existing file
/// (MoveFileEx with replace semantics); if a reader holds the target open the rename can fail
/// with a sharing violation, so it is retried a few times (10 ms apart) before the write fails.
/// Readers therefore see the old or the new file, never a partial one.

#include <filesystem>
#include <string>
#include <string_view>

#include "pme/plugin/library/error.hpp"
#include "pme/status.hpp"

namespace pme::plugin::library {

/// Path from UTF-8 text (also on Windows, where the native encoding is UTF-16).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);

/// UTF-8 text of a path.
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

/// Read a whole file in binary mode. `not-found` if it does not exist, `io` otherwise.
[[nodiscard]] Status readFile(const std::filesystem::path& path, std::string& out, Report& report);

/// Write `bytes` to `path` atomically (creating parent directories). `io` on failure.
[[nodiscard]] Status writeFileAtomic(const std::filesystem::path& path, std::string_view bytes,
                                     Report& report);

/// Write `bytes` to `path` only if `path` does not exist (creating parent directories). The temporary
/// file is hard-linked to the target, which fails if the target exists, so two processes cannot
/// both create it; on a file system without hard links the call falls back to a check followed by a
/// rename. `created` tells whether this call created the file; an existing file is not an error.
[[nodiscard]] Status writeFileExclusive(const std::filesystem::path& path, std::string_view bytes,
                                        bool& created, Report& report);

/// SHA-256 of a file as 64 lower-case hex digits (B1's implementation). `io` on failure.
[[nodiscard]] Status hashFile(const std::filesystem::path& path, std::string& hex, Report& report);

/// SHA-256 of a byte string as `sha256:` plus 64 hex digits, the form of manifest hashes.
[[nodiscard]] std::string sha256Tagged(std::string_view bytes);

/// A token that is unique in this process and, with overwhelming probability, across processes.
/// Used to name temporary files and directories.
[[nodiscard]] std::string uniqueToken();

/// Rename `from` to `to` with the retry described above. Returns false and sets `ec` on failure.
[[nodiscard]] bool renameWithRetry(const std::filesystem::path& from,
                                   const std::filesystem::path& to, std::error_code& ec);

}  // namespace pme::plugin::library
