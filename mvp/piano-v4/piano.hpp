// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include "pme/exciters/hammer.hpp"
#include "pme/bridge/terms.hpp"
#include "soundboard.hpp"

namespace piano_mvp_v4 {
// Experimental fixed preset. All allocations happen in prepare(), never during playback.
class Piano {
public:
    bool prepare(double sampleRate);
    // Set before prepare(), while playback is stopped.
    bool setBridgeFeedback(bool enabled) noexcept {
        if (ready_) return false;
        feedback_ = enabled; return true;
    }
    void reset() noexcept;
    void noteOn(int key, double velocity) noexcept;
    void noteOff(int key) noexcept;
    void sustain(bool down) noexcept;
    void sample(float& left, float& right) noexcept;
    int activeVoices() const noexcept;
    std::uint64_t numericalFaults() const noexcept { return numericalFaults_; }
    double peakBridgeMotion() const noexcept { return peakBridgeMotion_; }
private:
    struct Voice {
        pme::StringModes modes;
        pme::SegmentBridgeTerms terms;
        pme::exciters::StruckNote<double> note;
        pme::bridge::BridgePoint<double> bridge;
        bool active{}, held{};
        std::uint64_t age{}, quiet{};
        double release{1.0}, pan{};
    };
    std::array<Voice, 88> voices_;
    piano_mvp_v3::Soundboard soundboard_;
    bool feedback_{true};
    double fs_{48000.0}, releaseStep_{}, peakBridgeMotion_{};
    std::uint64_t clock_{};
    std::uint64_t numericalFaults_{};
    bool pedal_{}, ready_{};
};
}
