// SPDX-License-Identifier: MIT
// Helpers shared by the loader tests.
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace b1test {

/// Contents of a file as bytes (binary mode, so CRLF handling cannot change them).
inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace b1test
