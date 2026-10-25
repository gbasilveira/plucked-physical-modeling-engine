// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include "pme/exciters/hammer.hpp"
#include "pme/bridge/terms.hpp"
#include "soundboard.hpp"
#include "grand_board.hpp"
#include "sympathetic_string.hpp"
#include "bridge_mass.hpp"
#include "tension_note.hpp"

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
    // 0.5.1, all before prepare(); each default on and independent (see README 0.5.1).
    // Shared board: one bidirectionally coupled soundboard for all strings (needs bridge
    // feedback; off reproduces the 0.5.0 private per-key bridges bit-for-bit).
    bool setSharedBoard(bool enabled) noexcept {
        if (ready_) return false;
        sharedBoard_ = enabled; return true;
    }
    // Key-dependent bridge points on the bass/treble bridge curve (off: 0.5.0 pan blend).
    // Only affects the shared board.
    bool setGrandBridgeGeometry(bool enabled) noexcept {
        if (ready_) return false;
        grandGeometry_ = enabled; return true;
    }
    // Simulate unstruck strings whose dampers are lifted (pedal, held key, undamped treble) when
    // the board moves their bridge point. Only affects the shared board.
    bool setSympatheticStrings(bool enabled) noexcept {
        if (ready_) return false;
        sympathetic_ = enabled; return true;
    }
    // Largest number of simultaneously simulated unstruck strings, 0..88 (default 88).
    bool setSympatheticLimit(int n) noexcept {
        if (ready_ || n < 0 || n > maxPolyphony) return false;
        sympLimit_ = n; return true;
    }
    // 0.5.2, before prepare(). Kirchhoff-Carrier tension modulation of the struck strings (pitch
    // glide, phantom partials; default on). Off reproduces 0.5.1 bit-for-bit.
    bool setTensionModulation(bool enabled) noexcept {
        if (ready_) return false;
        tension_ = enabled; return true;
    }
    // Tension modulation only for keys 21..n (CPU lever, default 108 = all keys). See README 0.5.2.
    bool setTensionMaxKey(int n) noexcept {
        if (ready_ || n < 20 || n > 108) return false;
        tensionMaxKey_ = n; return true;
    }
    // Plug-in cap on sympathetic strings (CPU, README 0.5.1).
    static constexpr int pluginSympatheticLimit = 8;
    // Plug-in tension modulation boundary (CPU, README 0.5.2): keys 21..47 only. With all keys the
    // pedalled 12-key cluster runs at 0.50x real time, with this boundary at 0.97x.
    static constexpr int pluginTensionMaxKey = 47;
    // Board bridge-point speed (m/s) above which a lifted, idle string starts to be simulated,
    // and below which (half of it) an idle sympathetic string may go back to sleep. Assumption.
    static constexpr double wakeVelocity = 1e-4;
    // Unstruck strings keep only vertical modes below this (the board is coupled up to 1.87 kHz).
    static constexpr double sympatheticCutHz = 2000.0;
    // Lift the damper of a key without striking it (diagnostic/test; noteOff releases it).
    void liftDamper(int key) noexcept {
        if (ready_ && key >= 21 && key <= 108) voices_[key - 21].held = true;
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
    int activeVoices() const noexcept;             // struck (hammer-driven) voices
    int sympatheticVoices() const noexcept;        // simulated but never struck since waking
    const GrandBoard& board() const noexcept { return board_; }
    bool sharedBoardActive() const noexcept { return shared_; }
    bool voiceActive(int key) const noexcept { return key >= 21 && key <= 108 && voices_[key-21].active; }
    // Last bridge force (N) this key's strings put on the board (shared board only).
    double portForce(int key) const noexcept { return key >= 21 && key <= 108 ? voices_[key-21].force : 0.0; }
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
        TensionStruckNote note;           // 0.5.2: StruckNote + tension modulation (tension_note.hpp)
        pme::bridge::BridgePoint<double> bridge;
        bool active{}, held{}, undamped{}, struck{}, sympathetic{};
        double force{};     // F^n of this voice's strings on the shared board (0.5.1)
        SympatheticString light;        // unstruck-string model (sympathetic voices)
        double lightW{}, dwPrev{};      // its bridge displacement since waking; last board increment
        std::uint64_t age{}, quiet{};
        double release{1.0}, pan{};
    };
    std::array<Voice, 88> voices_;
    piano_mvp_v3::Soundboard soundboard_;
    GrandBoard board_;
    bool sharedBoard_{true}, grandGeometry_{true}, sympathetic_{true}, shared_{};
    int sympLimit_{maxPolyphony};
    std::uint64_t sampleClock_{};
    void sampleShared(float& left, float& right) noexcept;
    void wakeSympathetic() noexcept;
    void primeBridge(Voice& v, int port) noexcept;
    std::vector<float> coincidence_;   // [candidate*88 + sounding] harmonic coincidence score
    bool feedback_{true};
    bool unisons_{true};
    bool residualLoss_{true};
    bool softFelt_{true};
    bool bridgeMass_{true};
    bool saturatingLoss_{true};
    bool tension_{true};
    int tensionMaxKey_{108};
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
