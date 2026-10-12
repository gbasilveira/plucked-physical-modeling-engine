// SPDX-License-Identifier: MIT
// File helpers of the instrument and preset library (D5). Non-real-time.
#include "pme/plugin/library/files.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <random>
#include <span>
#include <system_error>
#include <thread>

#include "pme/loader/sha256.hpp"

namespace pme::plugin::library {

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

Status readFile(const std::filesystem::path& path, std::string& out, Report& report) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            report.error(pathToUtf8(path), code::kNotFound, "file not found");
            return Status::InvalidArgument;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            report.error(pathToUtf8(path), code::kIo, "cannot open file");
            return Status::InvalidArgument;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.bad()) {
            report.error(pathToUtf8(path), code::kIo, "cannot read file");
            return Status::InvalidArgument;
        }
        out = std::move(text);
        return Status::Ok;
    } catch (...) {
        report.error(pathToUtf8(path), code::kIo, "unexpected failure while reading");
        return Status::InvalidArgument;
    }
}

std::string uniqueToken() {
    static std::atomic<std::uint64_t> counter{0};
    std::uint64_t entropy = 0;
    try {
        std::random_device device;
        entropy = (static_cast<std::uint64_t>(device()) << 32U) | device();
    } catch (...) {
        entropy = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    const std::uint64_t thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t mixed = entropy ^ (thread * 0x9E3779B97F4A7C15ULL) ^ (n << 48U);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    for (int shift = 60; shift >= 0; shift -= 4) {
        token.push_back(kHex[(mixed >> static_cast<unsigned>(shift)) & 0xFU]);
    }
    token += '-';
    token += std::to_string(n);
    return token;
}

bool renameWithRetry(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::error_code& ec) {
    constexpr int kAttempts = 5;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        std::filesystem::rename(from, to, ec);
        if (!ec) {
            return true;
        }
#ifdef _WIN32
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#else
        break;  // POSIX: a failed rename is not transient.
#endif
    }
    return false;
}

Status writeFileAtomic(const std::filesystem::path& path, std::string_view bytes, Report& report) {
    std::filesystem::path temp;
    try {
        std::error_code ec;
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                report.error(pathToUtf8(parent), code::kIo,
                             "cannot create directory: " + ec.message());
                return Status::InvalidArgument;
            }
        }
        temp = path;
        temp += ".tmp-" + uniqueToken();
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                report.error(pathToUtf8(temp), code::kIo, "cannot create temporary file");
                return Status::InvalidArgument;
            }
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) {
                out.close();
                std::filesystem::remove(temp, ec);
                report.error(pathToUtf8(path), code::kIo, "cannot write file");
                return Status::InvalidArgument;
            }
        }
        if (!renameWithRetry(temp, path, ec)) {
            const std::string reason = ec.message();
            std::filesystem::remove(temp, ec);
            report.error(pathToUtf8(path), code::kIo, "cannot replace file: " + reason);
            return Status::InvalidArgument;
        }
        return Status::Ok;
    } catch (...) {
        std::error_code ec;
        if (!temp.empty()) {
            std::filesystem::remove(temp, ec);
        }
        report.error(pathToUtf8(path), code::kIo, "unexpected failure while writing");
        return Status::InvalidArgument;
    }
}

Status writeFileExclusive(const std::filesystem::path& path, std::string_view bytes,
                          bool& created, Report& report) {
    std::filesystem::path temp;
    try {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            report.error(pathToUtf8(path.parent_path()), code::kIo,
                         "cannot create directory: " + ec.message());
            return Status::InvalidArgument;
        }
        temp = path;
        temp += ".tmp-" + uniqueToken();
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                report.error(pathToUtf8(temp), code::kIo, "cannot create temporary file");
                return Status::InvalidArgument;
            }
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) {
                out.close();
                std::filesystem::remove(temp, ec);
                report.error(pathToUtf8(path), code::kIo, "cannot write file");
                return Status::InvalidArgument;
            }
        }
        std::filesystem::create_hard_link(temp, path, ec);
        if (!ec) {
            std::filesystem::remove(temp, ec);
            created = true;
            return Status::Ok;
        }
        std::error_code existsEc;
        if (std::filesystem::exists(path, existsEc)) {
            std::filesystem::remove(temp, ec);
            created = false;
            return Status::Ok;
        }
        // No hard links on this file system: fall back to rename (small race window).
        if (!renameWithRetry(temp, path, ec)) {
            const std::string reason = ec.message();
            std::filesystem::remove(temp, ec);
            report.error(pathToUtf8(path), code::kIo, "cannot create file: " + reason);
            return Status::InvalidArgument;
        }
        created = true;
        return Status::Ok;
    } catch (...) {
        std::error_code ec;
        if (!temp.empty()) {
            std::filesystem::remove(temp, ec);
        }
        report.error(pathToUtf8(path), code::kIo, "unexpected failure while writing");
        return Status::InvalidArgument;
    }
}

Status hashFile(const std::filesystem::path& path, std::string& hex, Report& report) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            report.error(pathToUtf8(path), code::kNotFound, "file not found");
            return Status::InvalidArgument;
        }
        loader::Sha256 sha;
        std::string buffer(64U * 1024U, '\0');
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            if (got > 0) {
                sha.update(std::as_bytes(std::span<const char>(buffer.data(), got)));
            }
        }
        if (in.bad()) {
            report.error(pathToUtf8(path), code::kIo, "cannot read file");
            return Status::InvalidArgument;
        }
        hex = loader::toHex(sha.finish());
        return Status::Ok;
    } catch (...) {
        report.error(pathToUtf8(path), code::kIo, "unexpected failure while hashing");
        return Status::InvalidArgument;
    }
}

std::string sha256Tagged(std::string_view bytes) {
    return "sha256:" + loader::sha256Hex(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
}

}  // namespace pme::plugin::library
