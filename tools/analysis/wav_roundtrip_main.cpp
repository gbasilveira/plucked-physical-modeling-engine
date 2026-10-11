// SPDX-License-Identifier: MIT
// pme_wav_roundtrip write <file> <format> <channels>
// pme_wav_roundtrip read <file>
//
// Cross-language WAV check (P0.3-R5, V1 follow-up): tools/tests/test_p0_3_r5_cross_language.py
// writes a WAV with this tool and reads it with pme_analysis (Python), and the reverse. `write`
// emits a deterministic signal built from dyadic rationals k/1024 (|k| <= 1024), which every
// format represents exactly, so the comparison is sample-exact. `read` prints
// "<format> <channels> <rate> <frames>" and then one sample per line with 17 significant digits.
// Formats: pcm16, pcm24, pcm32, float32, float64. Exit 0 on success, 2 on a usage or file error.

#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "pme/analysis/wav.hpp"

namespace {

using pme::analysis::WavData;
using pme::analysis::WavFormat;

constexpr std::size_t kFrames = 480;
constexpr double kRate = 48000.0;

const char* format_name(WavFormat f) {
    switch (f) {
        case WavFormat::pcm16:
            return "pcm16";
        case WavFormat::pcm24:
            return "pcm24";
        case WavFormat::pcm32:
            return "pcm32";
        case WavFormat::float32:
            return "float32";
        case WavFormat::float64:
            return "float64";
    }
    return "?";
}

bool parse_format(std::string_view name, WavFormat& out) {
    for (const WavFormat f : {WavFormat::pcm16, WavFormat::pcm24, WavFormat::pcm32,
                              WavFormat::float32, WavFormat::float64}) {
        if (name == format_name(f)) {
            out = f;
            return true;
        }
    }
    return false;
}

// The test signal: sample i (interleaved index) is ((37 i + 11) mod 2001 - 1000) / 1024, and
// sample 0 is -1.0. The Python test computes the same values.
double test_sample(std::size_t i) {
    if (i == 0) {
        return -1.0;
    }
    return static_cast<double>(static_cast<long>((37 * i + 11) % 2001) - 1000) / 1024.0;
}

int usage() {
    std::cerr << "usage: pme_wav_roundtrip write <file> <pcm16|pcm24|pcm32|float32|float64> "
                 "<channels>\n"
                 "       pme_wav_roundtrip read <file>\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "write") {
            WavData wav;
            if (!parse_format(argv[3], wav.format)) {
                return usage();
            }
            const int channels = std::stoi(argv[4]);
            if (channels < 1) {
                return usage();
            }
            wav.channels = static_cast<unsigned>(channels);
            wav.sample_rate_hz = kRate;
            wav.samples.resize(kFrames * wav.channels);
            for (std::size_t i = 0; i < wav.samples.size(); ++i) {
                wav.samples[i] = test_sample(i);
            }
            pme::analysis::write_wav(argv[2], wav);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "read") {
            const WavData wav = pme::analysis::read_wav(argv[2]);
            std::printf("%s %u %.17g %zu\n", format_name(wav.format), wav.channels,
                        wav.sample_rate_hz, wav.frames());
            for (const double s : wav.samples) {
                std::printf("%.17g\n", s);
            }
            return 0;
        }
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "pme_wav_roundtrip: " << e.what() << '\n';
        return 2;
    }
}
