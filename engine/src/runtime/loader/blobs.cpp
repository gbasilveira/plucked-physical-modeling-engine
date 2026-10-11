// SPDX-License-Identifier: MIT
// Blob manifest: reading, writing and consistency checks (B1-R3; SPEC-INST "Instrument file").
#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <map>
#include <set>

#include "pme/loader/sha256.hpp"
#include "reader.hpp"

namespace pme::loader::detail {

namespace {

constexpr std::int64_t kSafeMax = 9007199254740991;  // 2^53 - 1, exact in every JSON tool
constexpr IntRule kPositive{true, 1, true, kSafeMax, {0, 0}, false};
constexpr IntRule kOffset{true, 0, true, kSafeMax, {0, 0}, false};

const std::vector<std::string_view>& dtypeNames() {
    static const std::vector<std::string_view> names{"float32", "float64", "int32", "uint32"};
    return names;
}

std::size_t dtypeSize(const std::string& dtype) { return dtype == "float64" ? 8U : 4U; }

// A blob path is relative, uses forward slashes and has no empty, "." or ".." segment and no ':'.
bool validBlobPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string part =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

// Bytes of an array, or nullopt if the product exceeds 2^53 (not representable in the manifest).
std::optional<std::uint64_t> arrayBytes(const BlobArray& a) {
    std::uint64_t bytes = dtypeSize(a.dtype);
    for (const std::int64_t dim : a.shape) {
        const auto d = static_cast<std::uint64_t>(dim);
        if (d == 0 || bytes > static_cast<std::uint64_t>(kSafeMax) / d) {
            return std::nullopt;
        }
        bytes *= d;
    }
    return bytes;
}

std::string blobPointer(std::size_t blob) { return "/blobs/" + std::to_string(blob); }

std::string arrayPointer(std::size_t blob, std::size_t array) {
    return blobPointer(blob) + "/arrays/" + std::to_string(array);
}

}  // namespace

void read(const Reader& r, Producer& o) {
    ObjectScope s(r);
    if (!s.ok()) {
        return;
    }
    s.text("tool", o.tool, true, TextRule::Any);
    s.text("version", o.version, true, TextRule::Any);
    s.finish();
}

void read(const Reader& r, BlobArray& o) {
    ObjectScope s(r);
    if (!s.ok()) {
        return;
    }
    s.text("name", o.name, true, TextRule::Any);
    s.choice("dtype", o.dtype, true, dtypeNames());
    s.integers("shape", o.shape, true, kPositive, 1);
    s.text("unit", o.unit, true, TextRule::Any);
    s.integer("offset", o.offset, true, kOffset);
    s.finish();
}

void read(const Reader& r, BlobEntry& o) {
    ObjectScope s(r);
    if (!s.ok()) {
        return;
    }
    s.text("hash", o.hash, true, TextRule::Hash);
    s.text("path", o.path, true, TextRule::Any);
    Producer producer;
    s.object("producer", producer, true);
    o.tool = producer.tool;
    o.toolVersion = producer.version;
    s.objects("arrays", o.arrays, true, 0);
    s.finish();
}

OJson blobsToJson(const std::vector<BlobEntry>& blobs) {
    OJson list = OJson::array();
    for (const BlobEntry& b : blobs) {
        OJson e = OJson::object();
        e["hash"] = b.hash;
        e["path"] = b.path;
        e["producer"] = OJson::object();
        e["producer"]["tool"] = b.tool;
        e["producer"]["version"] = b.toolVersion;
        e["arrays"] = OJson::array();
        for (const BlobArray& a : b.arrays) {
            OJson j = OJson::object();
            j["name"] = a.name;
            j["dtype"] = a.dtype;
            j["shape"] = OJson::array();
            for (const std::int64_t d : a.shape) {
                j["shape"].push_back(d);
            }
            j["unit"] = a.unit;
            j["offset"] = a.offset;
            e["arrays"].push_back(std::move(j));
        }
        list.push_back(std::move(e));
    }
    return list;
}

void checkManifest(const std::vector<BlobEntry>& blobs, ErrorList& errors) {
    const auto fail = [&errors](const std::string& path, Code code, std::string message) {
        errors.add(LoadError{path, toString(code), std::move(message)});
    };
    std::set<std::string> paths;
    std::set<std::string> hashes;
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        const BlobEntry& b = blobs[i];
        if (!validBlobPath(b.path)) {
            fail(blobPointer(i) + "/path", Code::Blob,
                 "expected a relative path with forward slashes and no empty, '.' or '..' part");
        } else if (!paths.insert(b.path).second) {
            fail(blobPointer(i) + "/path", Code::Duplicate, "a blob path appears twice");
        }
        if (!hashes.insert(b.hash).second) {
            fail(blobPointer(i) + "/hash", Code::Duplicate, "a blob hash appears twice");
        }
        std::set<std::string> names;
        struct Span {
            std::uint64_t begin;
            std::uint64_t end;
            std::size_t index;
        };
        std::vector<Span> spans;
        for (std::size_t k = 0; k < b.arrays.size(); ++k) {
            const BlobArray& a = b.arrays[k];
            if (!names.insert(a.name).second) {
                fail(arrayPointer(i, k) + "/name", Code::Duplicate,
                     "an array name appears twice in one blob");
            }
            const std::size_t size = dtypeSize(a.dtype);
            if (static_cast<std::uint64_t>(a.offset) % size != 0) {
                fail(arrayPointer(i, k) + "/offset", Code::Blob,
                     "expected an offset aligned to " + std::to_string(size) + " bytes");
            }
            const std::optional<std::uint64_t> bytes = arrayBytes(a);
            if (!bytes) {
                fail(arrayPointer(i, k) + "/shape", Code::Blob, "array is larger than 2^53 bytes");
                continue;
            }
            const auto begin = static_cast<std::uint64_t>(a.offset);
            spans.push_back(Span{begin, begin + *bytes, k});
        }
        std::stable_sort(spans.begin(), spans.end(),
                         [](const Span& x, const Span& y) { return x.begin < y.begin; });
        for (std::size_t s = 1; s < spans.size(); ++s) {
            if (spans[s].begin < spans[s - 1].end) {
                fail(arrayPointer(i, spans[s].index) + "/offset", Code::Blob,
                     "array overlaps the array before it in the file");
            }
        }
    }
}

namespace {

// Streams the file through SHA-256. Returns false if it cannot be read.
bool hashFile(const std::filesystem::path& file, std::string& hex) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    Sha256 sha;
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got > 0) {
            sha.update(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(buffer.data()), got));
        }
    }
    if (in.bad()) {
        return false;
    }
    hex = toHex(sha.finish());
    return true;
}

}  // namespace

void checkBlobFiles(const std::vector<BlobEntry>& blobs, const std::filesystem::path& directory,
                    bool verify, ErrorList& errors) {
    const auto fail = [&errors](const std::string& path, std::string message) {
        errors.add(LoadError{path, toString(Code::Blob), std::move(message)});
    };
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        const BlobEntry& b = blobs[i];
        if (!validBlobPath(b.path)) {
            continue;  // already reported by checkManifest
        }
        const std::filesystem::path file = directory / std::filesystem::path(b.path);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            if (verify) {
                fail(blobPointer(i) + "/path", "blob file not found: " + b.path);
            }
            continue;
        }
        const std::uintmax_t length = std::filesystem::file_size(file, ec);
        if (ec) {
            fail(blobPointer(i) + "/path", "cannot read blob file: " + b.path);
            continue;
        }
        for (std::size_t k = 0; k < b.arrays.size(); ++k) {
            const std::optional<std::uint64_t> bytes = arrayBytes(b.arrays[k]);
            if (!bytes) {
                continue;
            }
            if (static_cast<std::uint64_t>(b.arrays[k].offset) + *bytes > length) {
                fail(arrayPointer(i, k) + "/offset", "array extends past the end of the file (" +
                                                         std::to_string(length) + " bytes)");
            }
        }
        if (verify) {
            std::string hex;
            if (!hashFile(file, hex)) {
                fail(blobPointer(i) + "/path", "cannot read blob file: " + b.path);
            } else if ("sha256:" + hex != b.hash) {
                fail(blobPointer(i) + "/hash", "expected sha256:" + hex);
            }
        }
    }
}

}  // namespace pme::loader::detail
