// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include "pme/exciters/hammer.hpp"
#include "pme/bridge/terms.hpp"
#include "soundboard.hpp"
#include "bridge_mass.hpp"

namespace piano_mvp_v4 {
// Experimental fixed preset. All allocations happen in prepare(), never during playback.
class Piano {
public:
    static constexpr int maxVoices = 12;        // real-time default (CPU)
    static constexpr int maxPolyphony = 88;     // one voice per key: no stealing
    // 0.5.0 assumption: dampers cover keys below F#6 (MIDI 90); F#6 and above are undamped,
    // as on most grands (stated range varies, roughly the top 1.5 octaves). Not measured.
    static constexpr int firstUndampedKey = 90;
    bool prepare(double sampleRate);
    // Set before prepare(), while playback is stopped.
    bool setBridgeFeedback(bool enabled) noexcept {
        if (ready_) return false;
        feedback_ = enabled; return true;
    }
    bool setUnisonStrings(bool enabled) noexcept {
        if (ready_) return false;
        unisons_ = enabled; return true;
    }
    bool setResidualBridgeLoss(bool enabled) noexcept {
        if (ready_) return false;
        residualLoss_ = enabled; return true;
    }
    bool setSoftFelt(bool enabled) noexcept {
        if (ready_) return false;
        softFelt_ = enabled; return true;
    }
    // 0.4.9: both default on (see README). Bridge-top mass: low-pass force transmission.
    bool setBridgeMassLoading(bool enabled) noexcept {
        if (ready_) return false;
        bridgeMass_ = enabled; return true;
    }
    // String viscosity saturates above kStringLossCornerHz.
    bool setSaturatingStringLoss(bool enabled) noexcept {
        if (ready_) return false;
        saturatingLoss_ = enabled; return true;
    }
    // 0.5.0, before prepare(). Voice limit 1..88 (default 12); 88 never steals.
    bool setVoiceLimit(int n) noexcept {
        if (ready_ || n < 1 || n > maxPolyphony) return false;
        voiceLimit_ = n; return true;
    }
    // Una corda (CC67 >= 64): applied to notes struck while down; default on (see README).
    bool setUnaCorda(bool enabled) noexcept {
        if (ready_) return false;
        unaCordaEnabled_ = enabled; return true;
    }
    // Keys >= firstUndampedKey ignore note-off damping (they decay naturally).
    bool setUndampedTreble(bool enabled) noexcept {
        if (ready_) return false;
        undampedTreble_ = enabled; return true;
    }
    static int stringsForKey(int key) noexcept {
        return key <= 32 ? 1 : (key <= 51 ? 2 : 3);
    }
    static double feltStiffnessScale(int key) noexcept {
        if (key<=72) return 0.35;
        if (key>=84) return 1.0;
        const double t=(key-72)/12.0;
        return 0.35+0.65*t*t*(3-2*t);
    }
    void reset() noexcept;
    void noteOn(int key, double velocity) noexcept;
    void noteOff(int key) noexcept;
    void sustain(bool down) noexcept;
    void softPedal(bool down) noexcept { unaCordaDown_ = unaCordaEnabled_ && down; }
    bool softPedalDown() const noexcept { return unaCordaDown_; }
    void sample(float& left, float& right) noexcept;
    int activeVoices() const noexcept;
    std::uint64_t numericalFaults() const noexcept { return numericalFaults_; }
    std::uint64_t voiceSteals() const noexcept { return voiceSteals_; }
    double peakBridgeMotion() const noexcept { return peakBridgeMotion_; }
    pme::exciters::HammerDiagnostics hammerDiagnostics(int key) const noexcept {
        return key>=21 && key<=108 ? voices_[key-21].note.diagnostics()
                                  : pme::exciters::HammerDiagnostics{};
    }
private:
    struct Voice {
        std::array<pme::StringModes, 3> modes;
        std::array<pme::SegmentBridgeTerms, 3> terms;
        pme::exciters::StruckNote<double> note;
        pme::bridge::BridgePoint<double> bridge;
        bool active{}, held{}, undamped{};
        std::uint64_t age{}, quiet{};
        double release{1.0}, pan{};
    };
    std::array<Voice, 88> voices_;
    piano_mvp_v3::Soundboard soundboard_;
    bool feedback_{true};
    bool unisons_{true};
    bool residualLoss_{true};
    bool softFelt_{true};
    bool bridgeMass_{true};
    bool saturatingLoss_{true};
    bool unaCordaEnabled_{true}, undampedTreble_{true}, unaCordaDown_{};
    int voiceLimit_{maxVoices};
    BridgeMassTransmission bridgeMassFilter_;
    double fs_{48000.0}, releaseStep_{}, peakBridgeMotion_{};
    std::uint64_t clock_{};
    std::uint64_t numericalFaults_{};
    std::uint64_t voiceSteals_{};
    bool pedal_{}, ready_{};
};
}
