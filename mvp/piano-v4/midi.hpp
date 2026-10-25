// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include "piano.hpp"

namespace piano_mvp_v4 {
inline void dispatchMidi(Piano& piano, const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < 3 || data[1] > 127 || data[2] > 127) return;
    switch (data[0] & 0xf0) {
        case 0x90: piano.noteOn(data[1],data[2]/127.0); break;
        case 0x80: piano.noteOff(data[1]); break;
        case 0xb0:
            if (data[1] == 64) piano.sustain(data[2] >= 64);
            if (data[1] == 120) piano.reset();
            if (data[1] == 123) for (int key = 21; key <= 108; ++key) piano.noteOff(key);
            if (data[1] == 121) piano.sustain(false);
            break;
    }
}
}
