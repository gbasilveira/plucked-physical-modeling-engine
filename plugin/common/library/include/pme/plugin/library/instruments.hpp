// SPDX-License-Identifier: MIT
#pragma once

/// @file instruments.hpp
/// Instrument import, export, listing and loading (D5-R3, D5-R5). Non-audio; allocates; never
/// throws; re-entrant (two plug-in instances may use the same directories at once).
///
/// SPEC sections used: SPEC-INST "Instrument file" (a versioned human-readable description plus
/// binary blobs referenced by hash); SPEC-API "Descriptions". Parsing, migration and validation
/// are B1's (`pme::loader`); this header adds only storage.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pme/loader/instrument_file.hpp"
#include "pme/plugin/library/error.hpp"
#include "pme/plugin/library/locations.hpp"
#include "pme/status.hpp"

namespace pme::plugin::library {

/// Reduce an instrument name to `[a-z0-9-]`: upper case to lower case, every other character
/// (including every non-ASCII byte) to `-`, runs collapsed, leading and trailing `-` removed, at
/// most 64 characters. An empty result becomes `instrument`.
[[nodiscard]] std::string reduceName(std::string_view name);

/// Load a library instrument by id (`factory/<name>` or `user/<name>`), always with
/// `verifyBlobs = true` (D5-R5): a missing blob or a SHA-256 mismatch is an error `blob` whose
/// path is the manifest pointer (`/blobs/<i>/...`) and whose message names the blob file. `out`
/// is unchanged on failure.
[[nodiscard]] Status loadLibraryInstrument(const Locations& locations, std::string_view id,
                                           loader::InstrumentFile& out, Report& report);

/// SHA-256 of the instrument's canonical text (`writeInstrument` output) as `sha256:<hex>`. This
/// is the `instrument.hash` of a preset.
[[nodiscard]] Status instrumentHash(const loader::InstrumentFile& file, std::string& hash,
                                    Report& report);

/// Outcome of `importInstrument`.
struct ImportResult {
    std::string id;           ///< Library id of the stored instrument (`user/<name>`).
    bool alreadyPresent{};    ///< True if an identical instrument was already stored (idempotent).
    int sourceVersion{};      ///< Schema version of the imported text (before migration).
};

/// Import an instrument file into `user/instruments/<name>/` (D5-R3). The file is loaded with
/// B1's `loadInstrumentFile(..., verifyBlobs = true)` (every error is B1's, with its JSON
/// Pointer), then stored as canonical current-version text, which is the migration through B1;
/// every manifest blob is copied to the same relative path and its hash is verified again after
/// the copy. `<name>` is `reduceName(desc.name)`; an existing directory holding a different
/// instrument gets the suffix `-2`, `-3`, ...; one holding the same canonical text is reused. The
/// directory is built under a temporary name and renamed into place, so a failure leaves no
/// partial directory.
[[nodiscard]] Status importInstrument(const Locations& locations,
                                      const std::filesystem::path& sourceInstrumentJson,
                                      ImportResult& result, Report& report);

/// Export a library instrument to `targetDir/<name>/instrument.json` (canonical) plus its blobs
/// (D5-R3). Re-importing the result gives the same canonical text. A single-file archive is out of
/// scope. `writtenInstrumentJson` receives the path of the written instrument file.
[[nodiscard]] Status exportInstrument(const Locations& locations, std::string_view id,
                                      const std::filesystem::path& targetDir,
                                      std::filesystem::path& writtenInstrumentJson,
                                      Report& report);

/// One row of `listInstruments` / `listPresets`.
struct ListEntry {
    std::string id;       ///< Library id.
    std::string name;     ///< Display name (instrument `name`, or preset `name`).
    Source source{Source::User};
    int schemaVersion{};  ///< Schema version found on disk (-1 if unreadable).
    std::vector<LibraryError> errors;  ///< Why this entry could not be read; empty if fine.
};

/// List factory then user instruments, sorted by id within each source. One bad entry does not
/// fail the listing: it appears with `errors`. Blob hashes are not verified here (loading does).
[[nodiscard]] std::vector<ListEntry> listInstruments(const Locations& locations);

/// Copy a file in binary mode through a temporary file, verifying the destination against
/// `expectedHash` (`sha256:<hex>`) after the copy. Used for blobs; exposed for tests.
[[nodiscard]] Status copyFileVerified(const std::filesystem::path& from,
                                      const std::filesystem::path& to,
                                      std::string_view expectedHash, Report& report);

}  // namespace pme::plugin::library
