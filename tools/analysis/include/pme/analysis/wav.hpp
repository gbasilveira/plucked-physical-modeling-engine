// SPDX-License-Identifier: MIT
// RIFF/WAVE reading and writing for tests and tools (P0.3-R5). Never linked into the engine.
// Reads PCM 16/24/32-bit integer and IEEE float 32/64-bit, any channel count, including
// WAVE_FORMAT_EXTENSIBLE; samples are float64, integer formats scaled to [-1, 1). Little-endian on
// every system. Unknown chunks (LIST, fact, ...) are skipped; RF64/BW64 and compressed formats are
// rejected with a WavError.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace pme::analysis {

enum class WavFormat : std::uint8_t { pcm16, pcm24, pcm32, float32, float64 };

class WavError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct WavData {
    double sample_rate_hz = 48000.0;
    unsigned channels = 1;
    WavFormat format = WavFormat::pcm16;
    std::vector<double> samples;  // interleaved, frames * channels values

    [[nodiscard]] std::size_t frames() const {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

// Throws WavError on malformed or unsupported input.
WavData parse_wav(std::span<const std::uint8_t> bytes);

// Throws WavError when the data cannot be represented (zero channels, size not a multiple of the
// channel count, sample rate not a positive integer).
std::vector<std::uint8_t> encode_wav(const WavData& wav);

WavData read_wav(const std::filesystem::path& path);
void write_wav(const std::filesystem::path& path, const WavData& wav);

}  // namespace pme::analysis
