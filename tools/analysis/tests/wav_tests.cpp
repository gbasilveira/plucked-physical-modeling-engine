// SPDX-License-Identifier: MIT
// P0.3-R5: WAV round trips for every supported format, WAVE_FORMAT_EXTENSIBLE on read, and errors.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <pme/analysis/wav.hpp>
#include <pme/test/tolerance.hpp>

using pme::analysis::WavData;
using pme::analysis::WavError;
using pme::analysis::WavFormat;

namespace {

WavData make_signal(WavFormat format, unsigned channels) {
    WavData wav;
    wav.sample_rate_hz = 44100.0;
    wav.channels = channels;
    wav.format = format;
    for (unsigned frame = 0; frame < 200; ++frame) {
        for (unsigned ch = 0; ch < channels; ++ch) {
            const double phase = 0.05 * static_cast<double>(frame) * static_cast<double>(ch + 1);
            wav.samples.push_back(0.8 * std::sin(phase));
        }
    }
    // Edge values: full-scale negative is exactly representable in every integer format.
    wav.samples[0] = -1.0;
    wav.samples[1 % wav.samples.size()] = 0.0;
    return wav;
}

double step_for(WavFormat f) {
    switch (f) {
        case WavFormat::pcm16: return 1.0 / 32768.0;
        case WavFormat::pcm24: return 1.0 / 8388608.0;
        case WavFormat::pcm32: return 1.0 / 2147483648.0;
        case WavFormat::float32: return 0.0;  // exact for float32 values (checked below)
        case WavFormat::float64: return 0.0;
    }
    return 0.0;
}

}  // namespace

TEST_CASE("P0.3-R5 round trip through every supported format and channel count") {
    for (const WavFormat format : {WavFormat::pcm16, WavFormat::pcm24, WavFormat::pcm32,
                                   WavFormat::float32, WavFormat::float64}) {
        for (const unsigned channels : {1U, 2U, 6U}) {
            WavData in = make_signal(format, channels);
            if (format == WavFormat::float32) {
                for (double& v : in.samples) {
                    v = static_cast<double>(static_cast<float>(v));
                }
            }
            const std::vector<std::uint8_t> bytes = pme::analysis::encode_wav(in);
            const WavData out = pme::analysis::parse_wav(bytes);
            INFO("format " << static_cast<int>(format) << " channels " << channels);
            CHECK(out.format == format);
            CHECK(out.channels == channels);
            CHECK(out.sample_rate_hz == 44100.0);
            REQUIRE(out.samples.size() == in.samples.size());
            const double tol = 0.5 * step_for(format);
            for (std::size_t i = 0; i < in.samples.size(); ++i) {
                CHECK(std::fabs(out.samples[i] - in.samples[i]) <= tol);
            }
        }
    }
}

TEST_CASE("P0.3-R5 integer formats clamp and stay below plus one") {
    WavData wav;
    wav.format = WavFormat::pcm16;
    wav.samples = {1.5, -1.5, 1.0};
    const WavData out = pme::analysis::parse_wav(pme::analysis::encode_wav(wav));
    CHECK(out.samples[0] == doctest::Approx(32767.0 / 32768.0));
    CHECK(out.samples[1] == -1.0);
    CHECK(out.samples[2] < 1.0);
}

TEST_CASE("P0.3-R5 file round trip") {
    const WavData in = make_signal(WavFormat::float64, 2);
    const auto path = std::filesystem::temp_directory_path() / "pme_p0_3_r5_roundtrip.wav";
    pme::analysis::write_wav(path, in);
    const WavData out = pme::analysis::read_wav(path);
    std::filesystem::remove(path);
    CHECK(out.samples == in.samples);
    CHECK_THROWS_AS(pme::analysis::read_wav(path), WavError);
}

TEST_CASE("P0.3-R5 reads WAVE_FORMAT_EXTENSIBLE and skips unknown chunks") {
    // Hand-built file: LIST chunk (odd size, padded), then fmt (extensible, 24-bit stereo), data.
    std::vector<std::uint8_t> b;
    auto u16 = [&](std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v & 0xFF)); b.push_back(static_cast<std::uint8_t>(v >> 8)); };
    auto u32 = [&](std::uint32_t v) { u16(v & 0xFFFF); u16(static_cast<std::uint16_t>(v >> 16)); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(t[i])); };
    tag("RIFF"); u32(0); tag("WAVE");
    tag("LIST"); u32(3); b.push_back('a'); b.push_back('b'); b.push_back('c'); b.push_back(0);
    tag("fmt "); u32(40);
    u16(0xFFFE); u16(2); u32(48000); u32(48000 * 6); u16(6); u16(24);
    u16(22); u16(24); u32(3);  // cbSize, valid bits, channel mask
    u16(1);                    // sub-format GUID begins with the PCM tag
    for (int i = 0; i < 14; ++i) b.push_back(0);
    tag("data"); u32(12);
    // frames: (+0.5, -0.25) and (-1.0, 0.0)
    const std::int32_t v[4] = {0x400000, -0x200000, -0x800000, 0};
    for (const std::int32_t s : v) {
        b.push_back(static_cast<std::uint8_t>(s & 0xFF));
        b.push_back(static_cast<std::uint8_t>((s >> 8) & 0xFF));
        b.push_back(static_cast<std::uint8_t>((s >> 16) & 0xFF));
    }
    const WavData wav = pme::analysis::parse_wav(b);
    CHECK(wav.format == WavFormat::pcm24);
    CHECK(wav.channels == 2);
    CHECK(wav.sample_rate_hz == 48000.0);
    REQUIRE(wav.samples.size() == 4);
    CHECK(wav.samples[0] == 0.5);
    CHECK(wav.samples[1] == -0.25);
    CHECK(wav.samples[2] == -1.0);
    CHECK(wav.samples[3] == 0.0);
}

TEST_CASE("P0.3-R5 rejects malformed and unsupported files with clear errors") {
    const std::vector<std::uint8_t> too_short = {'R', 'I', 'F', 'F'};
    CHECK_THROWS_AS(pme::analysis::parse_wav(too_short), WavError);

    std::vector<std::uint8_t> not_wave = pme::analysis::encode_wav(make_signal(WavFormat::pcm16, 1));
    not_wave[8] = 'X';
    CHECK_THROWS_WITH_AS(pme::analysis::parse_wav(not_wave), doctest::Contains("not a RIFF/WAVE"), WavError);

    std::vector<std::uint8_t> rf64 = pme::analysis::encode_wav(make_signal(WavFormat::pcm16, 1));
    rf64[0] = 'R'; rf64[1] = 'F'; rf64[2] = '6'; rf64[3] = '4';
    CHECK_THROWS_WITH_AS(pme::analysis::parse_wav(rf64), doctest::Contains("RF64"), WavError);

    std::vector<std::uint8_t> adpcm = pme::analysis::encode_wav(make_signal(WavFormat::pcm16, 1));
    adpcm[20] = 2;  // format tag 2 = MS ADPCM
    CHECK_THROWS_WITH_AS(pme::analysis::parse_wav(adpcm), doctest::Contains("unsupported format"), WavError);

    std::vector<std::uint8_t> truncated = pme::analysis::encode_wav(make_signal(WavFormat::pcm16, 1));
    truncated.resize(truncated.size() - 10);
    CHECK_THROWS_WITH_AS(pme::analysis::parse_wav(truncated), doctest::Contains("truncated"), WavError);

    WavData bad_rate = make_signal(WavFormat::pcm16, 1);
    bad_rate.sample_rate_hz = 44100.5;
    CHECK_THROWS_AS(pme::analysis::encode_wav(bad_rate), WavError);
    WavData bad_channels = make_signal(WavFormat::pcm16, 2);
    bad_channels.samples.pop_back();
    CHECK_THROWS_AS(pme::analysis::encode_wav(bad_channels), WavError);
}
