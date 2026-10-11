// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-API "Events" (event types), SPEC-EXEC step 2.1 (sorted offsets).
#include "pme/plugin/event_translator.hpp"

#include <algorithm>
#include <cmath>

namespace pme::plugin {

namespace {
constexpr int kMidiNoteOff = 0x80;
constexpr int kMidiNoteOn = 0x90;
constexpr int kMidiPolyPressure = 0xA0;
constexpr int kMidiControlChange = 0xB0;
constexpr int kCcSustain = 64;
constexpr int kCcSostenuto = 66;
constexpr int kCcSoft = 67;
constexpr float kMidiScale = 127.0F;
constexpr int kHalfRange = 64;  // CC values >= 64 count as "on" for the switch pedal

bool validKey(int key) noexcept { return key >= 0 && key <= 127; }

float unit(double v) noexcept {
    if (!(v > 0.0)) {  // also NaN
        return 0.0F;
    }
    return v >= 1.0 ? 1.0F : static_cast<float>(v);
}
}  // namespace

EventTranslator::EventTranslator() : events_(kCapacity) {}

void EventTranslator::begin(std::uint32_t frames) noexcept {
    count_ = 0;
    frames_ = frames;
}

void EventTranslator::push(std::uint32_t offset, const Payload& payload) noexcept {
    if (count_ >= events_.size()) {
        ++dropped_;
        return;
    }
    const std::uint32_t last = frames_ > 0 ? frames_ - 1 : 0;
    Event& e = events_[count_++];
    e.offset = static_cast<std::int32_t>(std::min(offset, last));
    e.payload = payload;
}

void EventTranslator::noteOn(std::uint32_t offset, int key, double velocity) noexcept {
    if (!validKey(key)) {
        return;
    }
    float v = unit(velocity);
    if (v <= 0.0F) {
        v = 1.0F / kMidiScale;
    }
    push(offset, NoteOn{key, v});
}

void EventTranslator::noteOff(std::uint32_t offset, int key, double releaseVelocity) noexcept {
    if (!validKey(key)) {
        return;
    }
    push(offset, NoteOff{key, unit(releaseVelocity)});
}

void EventTranslator::noteChoke(std::uint32_t offset, int key) noexcept {
    noteOff(offset, key, 0.0);
}

// D1-R2 translation table.
void EventTranslator::midi(std::uint32_t offset, std::uint8_t status, std::uint8_t data1,
                           std::uint8_t data2) noexcept {
    const int type = status & 0xF0;
    const int d1 = data1 & 0x7F;
    const int d2 = data2 & 0x7F;
    switch (type) {
        case kMidiNoteOn:
            if (d2 > 0) {
                push(offset, NoteOn{d1, static_cast<float>(d2) / kMidiScale});
            } else {
                push(offset, NoteOff{d1, 0.0F});
            }
            break;
        case kMidiNoteOff:
            push(offset, NoteOff{d1, static_cast<float>(d2) / kMidiScale});
            break;
        case kMidiPolyPressure:
            push(offset, KeyDepth{d1, static_cast<float>(d2) / kMidiScale});
            break;
        case kMidiControlChange:
            if (d1 == kCcSustain) {
                push(offset, Pedal{PedalKind::Sustain, static_cast<float>(d2) / kMidiScale});
            } else if (d1 == kCcSostenuto) {
                push(offset, Pedal{PedalKind::Sostenuto, d2 >= kHalfRange ? 1.0F : 0.0F});
            } else if (d1 == kCcSoft) {
                push(offset, Pedal{PedalKind::Soft, static_cast<float>(d2) / kMidiScale});
            }
            break;
        default:
            break;
    }
}

void EventTranslator::param(std::uint32_t offset, ParamId id, double value) noexcept {
    push(offset, Param{id, value});
}

void EventTranslator::sort() noexcept {
    for (std::size_t i = 1; i < count_; ++i) {
        std::size_t j = i;
        while (j > 0 && events_[j - 1].offset > events_[j].offset) {
            std::swap(events_[j - 1], events_[j]);
            --j;
        }
    }
}

}  // namespace pme::plugin
