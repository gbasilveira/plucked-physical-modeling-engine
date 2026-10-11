// SPDX-License-Identifier: MIT
// Helpers shared by the loader tests.
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace b1test {

/// Contents of a file as bytes (binary mode, so CRLF handling cannot change them).
inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The value of an optional that the test has established is present.
template <class T>
const T& must(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::runtime_error("expected the optional member to be present");
    }
    return *value;
}

}  // namespace b1test
