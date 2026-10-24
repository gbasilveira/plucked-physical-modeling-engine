// SPDX-License-Identifier: MIT
// Unique scratch directory names for tests. CTest runs every doctest case as its own process and
// several at once, so a name must differ between processes: an unseeded std::rand() returns the
// same value in every process and made concurrent reference tests share (and delete) one
// directory. No engine dependency.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>

namespace pme::test {

// A not yet existing path under the system temp directory: <prefix>_<random bits>_<clock ticks>.
inline std::filesystem::path unique_scratch_dir(const std::string& prefix) {
    std::random_device rd;
    const std::uint64_t bits = (static_cast<std::uint64_t>(rd()) << 32U) ^ rd();
    const auto ticks =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           (prefix + "_" + std::to_string(bits) + "_" + std::to_string(ticks));
}

}  // namespace pme::test
