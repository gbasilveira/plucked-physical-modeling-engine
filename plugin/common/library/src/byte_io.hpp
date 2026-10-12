// SPDX-License-Identifier: MIT
// Explicit little-endian byte encoding for cache keys and cache payloads (D5-R4). Internal.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pme::plugin::library::detail {

/// Appends fixed-width little-endian values to a string used as a byte buffer.
class ByteWriter {
public:
    explicit ByteWriter(std::string& buffer) : buffer_(&buffer) {}

    void u8(std::uint8_t v) { buffer_->push_back(static_cast<char>(v)); }
    void u32(std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) {
            u8(static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU));
        }
    }
    void u64(std::uint64_t v) {
        for (unsigned i = 0; i < 8; ++i) {
            u8(static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU));
        }
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    /// IEEE-754 binary64 bit pattern, little-endian.
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    /// Length (u64) followed by the bytes.
    void str(std::string_view s) {
        u64(s.size());
        buffer_->append(s.data(), s.size());
    }
    void raw(const std::uint8_t* data, std::size_t n) {
        buffer_->append(reinterpret_cast<const char*>(data), n);  // NOLINT(*-reinterpret-cast)
    }
    /// Count (u64) followed by the values.
    void f64s(const std::vector<double>& v) {
        u64(v.size());
        for (const double d : v) {
            f64(d);
        }
    }
    void i32s(const std::vector<int>& v) {
        u64(v.size());
        for (const int d : v) {
            i32(d);
        }
    }

private:
    std::string* buffer_;
};

/// Reads what `ByteWriter` wrote. Any overrun clears `ok()` and returns zeros.
class ByteReader {
public:
    explicit ByteReader(std::string_view data) : data_(data) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] bool atEnd() const { return pos_ == data_.size(); }

    std::uint8_t u8() {
        if (pos_ >= data_.size()) {
            ok_ = false;
            return 0;
        }
        return static_cast<std::uint8_t>(data_[pos_++]);
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(u8()) << (8U * i);
        }
        return v;
    }
    std::uint64_t u64() {
        std::uint64_t v = 0;
        for (unsigned i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(u8()) << (8U * i);
        }
        return v;
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }
    /// `n` raw bytes (empty view and `ok() == false` on overrun).
    std::string_view bytes(std::size_t n) {
        if (n > data_.size() - pos_) {
            ok_ = false;
            return {};
        }
        const std::string_view v = data_.substr(pos_, n);
        pos_ += n;
        return v;
    }
    bool f64s(std::vector<double>& out) {
        const std::uint64_t n = u64();
        if (!ok_ || n > (data_.size() - pos_) / 8U) {
            ok_ = false;
            return false;
        }
        out.resize(static_cast<std::size_t>(n));
        for (double& d : out) {
            d = f64();
        }
        return ok_;
    }
    bool i32s(std::vector<int>& out) {
        const std::uint64_t n = u64();
        if (!ok_ || n > (data_.size() - pos_) / 4U) {
            ok_ = false;
            return false;
        }
        out.resize(static_cast<std::size_t>(n));
        for (int& d : out) {
            d = i32();
        }
        return ok_;
    }

private:
    std::string_view data_;
    std::size_t pos_{0};
    bool ok_{true};
};

}  // namespace pme::plugin::library::detail
