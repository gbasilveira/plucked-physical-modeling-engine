// SPDX-License-Identifier: MIT
// Helpers shared by the D5 tests: temporary directories, fixture copies, byte edits.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "pme/plugin/library/files.hpp"
#include "pme/plugin/library/locations.hpp"

namespace d5test {

namespace fs = std::filesystem;
namespace lib = pme::plugin::library;

/// A fresh directory under the system temporary directory, removed on destruction. `suffix` may
/// hold non-ASCII text (UTF-8) to exercise such paths.
class TempDir {
public:
    explicit TempDir(const std::u8string& suffix = u8"") {
        root_ = fs::temp_directory_path() / (std::string("pme-d5-") + lib::uniqueToken());
        if (!suffix.empty()) {
            root_ /= fs::path(suffix);
        }
        fs::create_directories(root_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    [[nodiscard]] const fs::path& path() const { return root_; }
    [[nodiscard]] lib::Locations locations() const {
        return {root_ / "factory", root_ / "user", root_ / "cache"};
    }

private:
    fs::path root_;
};

inline std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void spit(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline void copyTree(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

inline fs::path b1Fixtures() { return fs::path(PME_B1_FIXTURES); }
inline fs::path b1Examples() { return fs::path(PME_B1_EXAMPLES); }

/// Copy an example instrument into `<dir>/instrument.json`; returns that path.
inline fs::path installExample(const std::string& name, const fs::path& dir) {
    copyTree(b1Examples() / name, dir);
    return dir / "instrument.json";
}

/// The B1 all-fields fixture (a ModalTable body, two bridge points, two blobs) copied with its
/// blobs into `dir`; returns the instrument.json path.
inline fs::path installBlobInstrument(const fs::path& dir) {
    fs::create_directories(dir);
    fs::copy_file(b1Fixtures() / "all-fields.json", dir / "instrument.json",
                  fs::copy_options::overwrite_existing);
    copyTree(b1Fixtures() / "blobs", dir / "blobs");
    return dir / "instrument.json";
}

/// Flip one bit-pattern byte (xor 0xFF) at `offset`.
inline void flipByte(const fs::path& path, std::size_t offset) {
    std::string bytes = slurp(path);
    bytes.at(offset) = static_cast<char>(bytes.at(offset) ^ 0xFF);
    spit(path, bytes);
}

/// Names of the regular files directly in `dir`.
inline std::set<std::string> fileNames(const fs::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            names.insert(lib::pathToUtf8(entry.path().filename()));
        }
    }
    return names;
}

inline bool hasCode(const std::vector<lib::LibraryError>& list, std::string_view code) {
    for (const auto& e : list) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace d5test
