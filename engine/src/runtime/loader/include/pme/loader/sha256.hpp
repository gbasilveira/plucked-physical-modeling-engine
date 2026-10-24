// SPDX-License-Identifier: MIT
// SHA-256 (FIPS 180-4) for blob hashes (B1-R3). Non-real-time.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace pme::loader {

/// Incremental SHA-256. Thread: any (one object per thread). Allocates: no.
class Sha256 {
public:
    Sha256() noexcept;

    /// Absorb bytes.
    void update(std::span<const std::byte> data) noexcept;

    /// Finish and return the 32-byte digest; the object must not be used afterwards.
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t totalBytes_{};
};

/// Lower-case hex of a digest.
[[nodiscard]] std::string toHex(const std::array<std::uint8_t, 32>& digest);

/// SHA-256 of a byte string as 64 lower-case hex digits.
[[nodiscard]] std::string sha256Hex(std::span<const std::byte> data);

}  // namespace pme::loader
