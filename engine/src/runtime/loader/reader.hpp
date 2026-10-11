// SPDX-License-Identifier: MIT
// Internal helpers of the instrument file loader: error collection and a validating JSON reader
// (B1-R4, B1-R5). The per-struct code that uses them is generated (fields_generated.hpp).
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "pme/loader/instrument_file.hpp"

namespace pme::loader::detail {

using Json = nlohmann::json;
using OJson = nlohmann::ordered_json;

/// At most this many errors are reported (B1-R5).
inline constexpr std::size_t kMaxErrors = 100;
/// Deepest nesting of arrays and objects accepted (a real file nests about 12 levels).
inline constexpr std::size_t kMaxDepth = 64;

/// The closed list of error codes shared with the Python loader (B1-R5).
enum class Code : std::uint8_t {
    Parse,
    Type,
    Missing,
    UnknownKey,
    Range,
    Enum,
    Version,
    Migration,
    Reference,
    Duplicate,
    Structure,
    Blob,
};

[[nodiscard]] const char* toString(Code code) noexcept;

/// Numeric bound of a number member (physical constraint, docs/dev/instrument-format.md).
struct Bound {
    bool hasLo;
    double lo;
    bool loExclusive;
    bool hasHi;
    double hi;
    bool hiExclusive;
};

/// Constraint of an integer member.
struct IntRule {
    bool hasLo;
    std::int64_t lo;
    bool hasHi;
    std::int64_t hi;
    std::array<int, 2> oneOf;
    bool hasOneOf;
};

/// Pattern of a string member.
enum class TextRule : std::uint8_t {
    Any,
    HashOrEmpty,  ///< Empty, or `sha256:` and 64 lower-case hex digits.
    Hash,         ///< `sha256:` and 64 lower-case hex digits.
};

/// Escape a key for a JSON Pointer (RFC 6901).
[[nodiscard]] std::string escapePointer(std::string_view key);

/// Collects errors; `take` sorts by (path, code), drops duplicates and caps the list.
class ErrorList {
public:
    void add(LoadError error);
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] std::vector<LoadError> take();

private:
    std::vector<LoadError> items_;
};

/// A JSON node together with its pointer path and the error sink.
class Reader {
public:
    Reader(ErrorList& errors, const Json& node, std::string path)
        : errors_(&errors), node_(&node), path_(std::move(path)) {}

    [[nodiscard]] const Json& node() const noexcept { return *node_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    /// Report an error at this node.
    void error(Code code, std::string message) const;
    /// Report an error at a member of this node.
    void errorAt(std::string_view key, Code code, std::string message) const;
    [[nodiscard]] Reader child(std::string_view key, const Json& node) const;
    [[nodiscard]] Reader child(std::size_t index, const Json& node) const;

private:
    ErrorList* errors_;
    const Json* node_;
    std::string path_;
};

/// Validating access to the members of one JSON object.
class ObjectScope {
public:
    /// Reports `type` and becomes not ok when the node is not an object.
    explicit ObjectScope(const Reader& r);

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool has(std::string_view key) const;

    void number(std::string_view key, double& out, bool required, const Bound& bound);
    void boolean(std::string_view key, bool& out, bool required);
    void text(std::string_view key, std::string& out, bool required, TextRule rule);
    void numbers(std::string_view key, std::vector<double>& out, bool required, const Bound& element,
                 std::size_t minItems);
    /// A string member restricted to `values` (stored as text).
    void choice(std::string_view key, std::string& out, bool required,
                const std::vector<std::string_view>& values);

    template <class T>
    void integer(std::string_view key, T& out, bool required, const IntRule& rule) {
        std::int64_t value = 0;
        if (readInteger(key, value, required, rule)) {
            out = static_cast<T>(value);
        }
    }

    template <class T>
    void integers(std::string_view key, std::vector<T>& out, bool required, const IntRule& element,
                  std::size_t minItems) {
        const Json* m = arrayMember(key, required, minItems);
        if (m == nullptr) {
            return;
        }
        const Reader c = r_->child(key, *m);
        std::vector<T> result;
        for (std::size_t i = 0; i < m->size(); ++i) {
            std::int64_t value = 0;
            if (checkInteger(c.child(i, (*m)[i]), value, element)) {
                result.push_back(static_cast<T>(value));
            }
        }
        out = std::move(result);
    }

    template <class E, std::size_t N>
    void enumeration(std::string_view key, E& out, bool required,
                     const std::array<const char*, N>& names) {
        const Json* m = member(key, required);
        if (m == nullptr) {
            return;
        }
        const Reader c = r_->child(key, *m);
        if (!m->is_string()) {
            c.error(Code::Type, "expected a string");
            return;
        }
        const std::string& text = m->get_ref<const std::string&>();
        for (std::size_t i = 0; i < N; ++i) {
            if (text == names[i]) {
                out = static_cast<E>(i);
                return;
            }
        }
        c.error(Code::Enum, "expected one of: " + join(names.data(), N));
    }

    template <class T>
    void object(std::string_view key, T& out, bool required) {
        const Json* m = member(key, required);
        if (m != nullptr) {
            read(r_->child(key, *m), out);
        }
    }

    template <class T>
    void objects(std::string_view key, std::vector<T>& out, bool required, std::size_t minItems) {
        const Json* m = arrayMember(key, required, minItems);
        if (m == nullptr) {
            return;
        }
        const Reader c = r_->child(key, *m);
        std::vector<T> result;
        result.reserve(m->size());
        for (std::size_t i = 0; i < m->size(); ++i) {
            T item{};
            read(c.child(i, (*m)[i]), item);
            result.push_back(std::move(item));
        }
        out = std::move(result);
    }

    template <class T>
    void optional(std::string_view key, std::optional<T>& out) {
        const Json* m = member(key, false);
        if (m != nullptr) {
            T value{};
            read(r_->child(key, *m), value);
            out = std::move(value);
        }
    }

    /// Reports `unknown_key` for every member that was not asked for.
    void finish();

private:
    [[nodiscard]] static std::string join(const char* const* names, std::size_t count);
    [[nodiscard]] const Json* member(std::string_view key, bool required);
    [[nodiscard]] const Json* arrayMember(std::string_view key, bool required, std::size_t minItems);
    [[nodiscard]] bool readInteger(std::string_view key, std::int64_t& out, bool required,
                                   const IntRule& rule);
    [[nodiscard]] static bool checkInteger(const Reader& c, std::int64_t& out, const IntRule& rule);

    const Reader* r_;
    std::vector<std::string_view> known_;
    bool ok_{true};
};

/// Shortest decimal that round-trips to `value`, laid out as docs/dev/instrument-format.md
/// "Canonical form" specifies (like nlohmann/json). `value` must be finite.
[[nodiscard]] std::string formatDouble(double value);

/// Canonical text of a document tree (2-space indentation, keys in insertion order, final LF).
[[nodiscard]] std::string serialise(const OJson& root);

/// Manifest members that have no counterpart in the frozen structs (B1-R3).
struct Producer {
    std::string tool;
    std::string version;
};
void read(const Reader& r, Producer& o);
void read(const Reader& r, BlobArray& o);
void read(const Reader& r, BlobEntry& o);

/// The manifest as an ordered tree.
[[nodiscard]] OJson blobsToJson(const std::vector<BlobEntry>& blobs);

/// Migration of a document of an older schema version to the current one (B1-R2). Errors go to
/// `errors`; the tree is migrated in place. Thread: non-audio.
void migrateToCurrent(Json& doc, int fromVersion, ErrorList& errors);

/// Cross-field rules JSON Schema cannot express (docs/dev/instrument-format.md, "Cross-field
/// rules"), run only on a structurally valid file.
void checkCrossFieldRules(const InstrumentFile& file, ErrorList& errors);

/// Manifest checks that need no file: shapes, alignment, overlap, duplicates, hash references.
void checkManifest(const std::vector<BlobEntry>& blobs, ErrorList& errors);

/// Checks against the blob files in `directory`: lengths always when the file exists; existence and
/// SHA-256 when `verify`.
void checkBlobFiles(const std::vector<BlobEntry>& blobs, const std::filesystem::path& directory,
                    bool verify, ErrorList& errors);

}  // namespace pme::loader::detail
