// SPDX-License-Identifier: MIT
#pragma once

/// @file event_translator.hpp
/// Host-neutral input to `pme::Event` (D1-R2, D1-R3). SPEC-API "Events", SPEC-EXEC step 2.1:
/// the engine takes one `EventList` per host block, sorted by non-decreasing offset, every offset
/// in [0, frames). The buffer is preallocated; the translating calls never allocate.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "pme/pme.hpp"

namespace pme::plugin {

class EventTranslator {
public:
    /// Largest number of events held for one host block; further events are dropped and counted.
    static constexpr std::size_t kCapacity = 4096;

    /// Non-audio thread. Allocates (reserves the buffer).
    EventTranslator();

    /// Start a host block of `frames` samples. Offsets passed afterwards are clamped to
    /// [0, frames - 1]. Audio thread. Allocates: no.
    void begin(std::uint32_t frames) noexcept;

    /// CLAP-style note on: `velocity` in [0, 1], key outside 0..127 is ignored. A velocity <= 0
    /// becomes 1/127 (the smallest MIDI velocity), because `NoteOn` needs nu in (0, 1].
    void noteOn(std::uint32_t offset, int key, double velocity) noexcept;
    /// Note off with release velocity in [0, 1].
    void noteOff(std::uint32_t offset, int key, double releaseVelocity) noexcept;
    /// Note choke: `NoteOff` with release velocity 0.
    void noteChoke(std::uint32_t offset, int key) noexcept;
    /// One MIDI 1.0 channel message (status byte with the channel in the low nibble, two data
    /// bytes): note on / off, polyphonic aftertouch, CC64 / 66 / 67. Others are ignored.
    void midi(std::uint32_t offset, std::uint8_t status, std::uint8_t data1,
              std::uint8_t data2) noexcept;
    /// Parameter value (already clamped by the caller), delivered as `Param`.
    void param(std::uint32_t offset, ParamId id, double value) noexcept;

    /// Stable-sort by offset (insertion sort; the input is normally sorted already).
    void sort() noexcept;
    /// The events of the block so far.
    [[nodiscard]] std::span<Event> events() noexcept { return {events_.data(), count_}; }
    /// Events dropped because the buffer was full, since construction.
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

private:
    using Payload = decltype(Event::payload);
    void push(std::uint32_t offset, const Payload& payload) noexcept;
    std::vector<Event> events_;
    std::size_t count_{0};
    std::uint32_t frames_{0};
    std::uint64_t dropped_{0};
};

}  // namespace pme::plugin
