// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include "pme/exciters/hammer.hpp"
#include "pme/bridge/terms.hpp"

namespace piano_mvp_v2 {
// Experimental fixed preset. All allocations happen in prepare(), never during playback.
class Piano {
public:
    bool prepare(double sampleRate);
    void reset() noexcept;
    void noteOn(int key, double velocity) noexcept;
    void noteOff(int key) noexcept;
    void sustain(bool down) noexcept;
    void sample(float& left, float& right) noexcept;
    int activeVoices() const noexcept;
    std::uint64_t numericalFaults() const noexcept { return numericalFaults_; }
private:
    struct Voice {
        pme::StringModes modes;
        pme::SegmentBridgeTerms terms;
        pme::exciters::StruckNote<double> note;
        bool active{}, held{};
        std::uint64_t age{}, quiet{};
        double release{1.0}, pan{};
    };
    struct Resonance { double a{}, r2{}, y1{}, y2{}, gain{}; };
    std::array<Voice, 88> voices_;
    std::array<Resonance, 4> body_;
    double fs_{48000.0}, releaseStep_{}, dcX_{}, dcY_{};
    std::uint64_t clock_{};
    std::uint64_t numericalFaults_{};
    bool pedal_{}, ready_{};
};
}
