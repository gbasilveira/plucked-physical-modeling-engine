// SPDX-License-Identifier: MIT
#include "pme/analysis/wav.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace pme::analysis {

namespace {

constexpr std::uint16_t kTagPcm = 0x0001;
constexpr std::uint16_t kTagFloat = 0x0003;
constexpr std::uint16_t kTagExtensible = 0xFFFE;

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU));
    }
}

void put_tag(std::vector<std::uint8_t>& out, const char* tag) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(tag[i]));
    }
}

bool tag_is(const std::uint8_t* p, const char* tag) {
    return std::memcmp(p, tag, 4) == 0;
}

unsigned bits_of(WavFormat f) {
    switch (f) {
        case WavFormat::pcm16: return 16;
        case WavFormat::pcm24: return 24;
        case WavFormat::pcm32: return 32;
        case WavFormat::float32: return 32;
        case WavFormat::float64: return 64;
    }
    return 0;
}

bool is_float(WavFormat f) {
    return f == WavFormat::float32 || f == WavFormat::float64;
}

std::int64_t clamp_round(double x, unsigned bits) {
    const double scale = std::ldexp(1.0, static_cast<int>(bits) - 1);
    const double v = std::round(x * scale);
    const double lo = -scale;
    const double hi = scale - 1.0;
    return static_cast<std::int64_t>(std::min(std::max(v, lo), hi));
}

}  // namespace

WavData parse_wav(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12) {
        throw WavError("WAV: file shorter than the RIFF header");
    }
    const std::uint8_t* b = bytes.data();
    if (tag_is(b, "RF64") || tag_is(b, "BW64")) {
        throw WavError("WAV: RF64/BW64 files are not supported");
    }
    if (!tag_is(b, "RIFF") || !tag_is(b + 8, "WAVE")) {
        throw WavError("WAV: not a RIFF/WAVE file");
    }

    bool have_fmt = false;
    std::uint16_t tag = 0;
    unsigned channels = 0;
    std::uint32_t rate = 0;
    unsigned bits = 0;
    std::uint16_t block_align = 0;
    std::span<const std::uint8_t> data;
    bool have_data = false;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const std::uint8_t* chunk = b + pos;
        const std::size_t size = read_u32(chunk + 4);
        const std::size_t body = pos + 8;
        if (body + size > bytes.size()) {
            if (tag_is(chunk, "data") || tag_is(chunk, "fmt ")) {
                throw WavError("WAV: chunk '" + std::string(reinterpret_cast<const char*>(chunk), 4) +
                               "' is truncated");
            }
            break;  // a damaged trailing metadata chunk does not affect the audio
        }
        if (tag_is(chunk, "fmt ")) {
            if (size < 16) {
                throw WavError("WAV: fmt chunk shorter than 16 bytes");
            }
            tag = read_u16(b + body);
            channels = read_u16(b + body + 2);
            rate = read_u32(b + body + 4);
            block_align = read_u16(b + body + 12);
            bits = read_u16(b + body + 14);
            if (tag == kTagExtensible) {
                if (size < 40) {
                    throw WavError("WAV: WAVE_FORMAT_EXTENSIBLE fmt chunk shorter than 40 bytes");
                }
                tag = read_u16(b + body + 24);  // first two bytes of the sub-format GUID
            }
            have_fmt = true;
        } else if (tag_is(chunk, "data")) {
            data = bytes.subspan(body, size);
            have_data = true;
        }
        pos = body + size + (size & 1U);
    }

    if (!have_fmt) {
        throw WavError("WAV: missing fmt chunk");
    }
    if (!have_data) {
        throw WavError("WAV: missing data chunk");
    }
    if (channels == 0) {
        throw WavError("WAV: zero channels");
    }
    WavFormat format{};
    if (tag == kTagPcm && bits == 16) {
        format = WavFormat::pcm16;
    } else if (tag == kTagPcm && bits == 24) {
        format = WavFormat::pcm24;
    } else if (tag == kTagPcm && bits == 32) {
        format = WavFormat::pcm32;
    } else if (tag == kTagFloat && bits == 32) {
        format = WavFormat::float32;
    } else if (tag == kTagFloat && bits == 64) {
        format = WavFormat::float64;
    } else {
        throw WavError("WAV: unsupported format tag " + std::to_string(tag) + " with " +
                       std::to_string(bits) + " bits per sample");
    }
    const unsigned bytes_per_sample = bits / 8U;
    if (block_align != channels * bytes_per_sample) {
        throw WavError("WAV: block alignment does not match channels and bit depth");
    }
    if (data.size() % block_align != 0) {
        throw WavError("WAV: data chunk is not a whole number of frames");
    }

    WavData wav;
    wav.sample_rate_hz = static_cast<double>(rate);
    wav.channels = channels;
    wav.format = format;
    const std::size_t count = data.size() / bytes_per_sample;
    wav.samples.resize(count);
    const std::uint8_t* p = data.data();
    for (std::size_t i = 0; i < count; ++i, p += bytes_per_sample) {
        switch (format) {
            case WavFormat::pcm16:
                wav.samples[i] = static_cast<double>(static_cast<std::int16_t>(read_u16(p))) / 32768.0;
                break;
            case WavFormat::pcm24: {
                std::uint32_t v = static_cast<std::uint32_t>(p[0]) |
                                  (static_cast<std::uint32_t>(p[1]) << 8U) |
                                  (static_cast<std::uint32_t>(p[2]) << 16U);
                if ((v & 0x800000U) != 0U) {
                    v |= 0xFF000000U;
                }
                wav.samples[i] = static_cast<double>(static_cast<std::int32_t>(v)) / 8388608.0;
                break;
            }
            case WavFormat::pcm32:
                wav.samples[i] =
                    static_cast<double>(static_cast<std::int32_t>(read_u32(p))) / 2147483648.0;
                break;
            case WavFormat::float32:
                wav.samples[i] = static_cast<double>(std::bit_cast<float>(read_u32(p)));
                break;
            case WavFormat::float64: {
                const std::uint64_t lo = read_u32(p);
                const std::uint64_t hi = read_u32(p + 4);
                wav.samples[i] = std::bit_cast<double>(lo | (hi << 32U));
                break;
            }
        }
    }
    return wav;
}

std::vector<std::uint8_t> encode_wav(const WavData& wav) {
    if (wav.channels == 0 || wav.channels > 0xFFFFU) {
        throw WavError("WAV: channel count must be in 1 .. 65535");
    }
    if (wav.samples.size() % wav.channels != 0) {
        throw WavError("WAV: sample count is not a multiple of the channel count");
    }
    if (!(wav.sample_rate_hz >= 1.0) || wav.sample_rate_hz != std::floor(wav.sample_rate_hz) ||
        wav.sample_rate_hz > 4294967295.0) {
        throw WavError("WAV: sample rate must be a positive integer");
    }
    const unsigned bits = bits_of(wav.format);
    const unsigned bytes_per_sample = bits / 8U;
    const std::uint64_t data_size = static_cast<std::uint64_t>(wav.samples.size()) * bytes_per_sample;
    if (data_size > 0xFFFFFFFFULL - 64U) {
        throw WavError("WAV: data larger than 4 GiB is not supported");
    }

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(data_size) + 64);
    put_tag(out, "RIFF");
    put_u32(out, static_cast<std::uint32_t>(36 + data_size + (data_size & 1U)));
    put_tag(out, "WAVE");
    put_tag(out, "fmt ");
    put_u32(out, 16);
    put_u16(out, is_float(wav.format) ? kTagFloat : kTagPcm);
    put_u16(out, static_cast<std::uint16_t>(wav.channels));
    put_u32(out, static_cast<std::uint32_t>(wav.sample_rate_hz));
    put_u32(out, static_cast<std::uint32_t>(wav.sample_rate_hz) * wav.channels * bytes_per_sample);
    put_u16(out, static_cast<std::uint16_t>(wav.channels * bytes_per_sample));
    put_u16(out, static_cast<std::uint16_t>(bits));
    put_tag(out, "data");
    put_u32(out, static_cast<std::uint32_t>(data_size));
    for (const double x : wav.samples) {
        switch (wav.format) {
            case WavFormat::pcm16:
                put_u16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(clamp_round(x, 16))));
                break;
            case WavFormat::pcm24: {
                const auto v = static_cast<std::uint32_t>(static_cast<std::int32_t>(clamp_round(x, 24)));
                out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
                out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
                out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
                break;
            }
            case WavFormat::pcm32:
                put_u32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(clamp_round(x, 32))));
                break;
            case WavFormat::float32:
                put_u32(out, std::bit_cast<std::uint32_t>(static_cast<float>(x)));
                break;
            case WavFormat::float64: {
                const auto v = std::bit_cast<std::uint64_t>(x);
                put_u32(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFU));
                put_u32(out, static_cast<std::uint32_t>(v >> 32U));
                break;
            }
        }
    }
    if ((data_size & 1U) != 0U) {
        out.push_back(0);
    }
    return out;
}

WavData read_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw WavError("WAV: cannot open " + path.string());
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    return parse_wav(bytes);
}

void write_wav(const std::filesystem::path& path, const WavData& wav) {
    const std::vector<std::uint8_t> bytes = encode_wav(wav);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw WavError("WAV: cannot create " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw WavError("WAV: write failed for " + path.string());
    }
}

}  // namespace pme::analysis
