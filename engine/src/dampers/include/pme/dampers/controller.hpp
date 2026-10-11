// SPDX-License-Identifier: MIT
// Key, sustain and sostenuto logic that drives the damper engagement (A6-R3, A6-R4, A6-R5).
//
// SPEC sections implemented: SPEC-DMP "Lift fraction", "Timing", "Sostenuto" (with SI-0016's
// thresholds), "Undamped notes"; SPEC-EXEC step 10. C4 routes NoteOn, NoteOff, KeyDepth and
// Pedal events into it at their sample offsets; C1 reads `engagement(key)` per sample or per
// sub-block. The Soft pedal is not handled here (A5).
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "pme/dampers.hpp"
#include "pme/dampers/lift.hpp"
#include "pme/status.hpp"

namespace pme::dmp {

/// Number of keys the controller tracks (MIDI note numbers 0 .. 127).
inline constexpr int kKeyCount = 128;
/// Sostenuto pedal depth at and above which the pedal is down (SI-0016).
inline constexpr double kSostenutoDown = 0.5;
/// l_key at and above which a key is latched when the sostenuto pedal goes down (SI-0016).
inline constexpr double kSostenutoLatchLift = 0.5;

/// The lift state of one key after the last input (SPEC-DMP "Lift fraction", "Sostenuto").
struct KeyLift {
    double lKey{0.0};    ///< l_key in force: 1 while latched, else the key's own value.
    double lPed{0.0};    ///< l_ped from the sustain pedal.
    double l{0.0};       ///< l = max(l_key, l_ped); 1 for a key without a damper.
    bool latched{false};  ///< Held up by the sostenuto latch.
};

/// All per-key damper state, preallocated for keys 0 .. 127 (A6-R5). Event methods and
/// `advance` are `noexcept` and allocation-free (audio thread); `configure` and `setKey` are
/// non-real-time.
class DamperController {
public:
    /// Set the sample rate and the half-pedal region (`Defaults::halfPedalP0/P1`, registry
    /// `damper.halfPedalStart/End`); resets every key to "no damper". `InvalidArgument` if
    /// fs <= 0 or not 0 <= P0 < P1 <= 1; the controller is unchanged then.
    /// Thread: non-audio. Allocates: no.
    [[nodiscard]] Status configure(double fs, double p0, double p1) noexcept;

    /// Give key `key` (0 .. 127) its damper. `std::nullopt` or `hasDamper == false` means no
    /// damper: l = 1 and e = 0 whatever the inputs (SPEC-DMP "Undamped notes"). A damped key
    /// starts at rest, e = 1. `InvalidArgument` for a bad key or invalid parameters; unchanged
    /// then. Thread: non-audio. Allocates: no.
    [[nodiscard]] Status setKey(int key, const std::optional<DamperParams>& params) noexcept;

    /// Key goes down: l_key = 1 (SPEC-DMP "Lift fraction"). Thread: audio. Allocates: no.
    void noteOn(int key) noexcept;
    /// Key released: l_key = 0 unless latched; `releaseVelocity` (clamped to [0, 1]) sets
    /// tau_down for the landing (SPEC-DMP "Timing"). Thread: audio. Allocates: no.
    void noteOff(int key, double releaseVelocity) noexcept;
    /// Continuous key depth: l_key = clamp(depth, 0, 1) (SPEC-DMP "Lift fraction"); a non-finite
    /// depth is ignored. Thread: audio. Allocates: no.
    void keyDepth(int key, double depth) noexcept;
    /// Sustain pedal depth P (non-finite ignored). Thread: audio. Allocates: no.
    void setSustain(double depth) noexcept;
    /// Sostenuto pedal depth: down while >= 0.5, released below it, no hysteresis; at the
    /// moment it goes down every damped key with l_key >= 0.5 is latched (l_key = 1) until the
    /// pedal is released (SPEC-DMP "Sostenuto", SI-0016). Non-finite depth is ignored.
    /// Thread: audio. Allocates: no.
    void setSostenuto(double depth) noexcept;

    /// Advance every moving engagement by one sample (SPEC-EXEC step 10). Returns true while
    /// any engagement is still moving. Thread: audio. Allocates: no.
    bool advance() noexcept;
    /// `n` calls of `advance()`; returns true while any engagement is still moving.
    bool advance(int n) noexcept;

    /// Engagement e of `key` (0 for a key without a damper or outside 0 .. 127).
    [[nodiscard]] double engagement(int key) const noexcept;
    /// e_target of `key`.
    [[nodiscard]] double target(int key) const noexcept;
    /// Engagement `n` samples ahead at the present target (closed form), for the sub-block
    /// refresh. Thread: audio. Allocates: no.
    [[nodiscard]] double projected(int key, int n) const noexcept;
    /// Lift state of `key` after the last input.
    [[nodiscard]] KeyLift lift(int key) const noexcept;
    /// True if `key` has a damper.
    [[nodiscard]] bool hasDamper(int key) const noexcept;
    /// True while any engagement is moving.
    [[nodiscard]] bool anyMoving() const noexcept { return (movingMask_[0] | movingMask_[1]) != 0U; }
    /// Sustain depth P, sostenuto depth and sostenuto state.
    [[nodiscard]] double sustainDepth() const noexcept { return sustain_; }
    [[nodiscard]] bool sostenutoDown() const noexcept { return sostenutoDown_; }
    /// tau_down in force for `key` (from the last NoteOff's release velocity).
    [[nodiscard]] double tauDown(int key) const noexcept;

private:
    struct Slot {
        bool damped{false};
        bool latched{false};
        double lKey{0.0};   // the key's own l_key
        double nuRel{0.0};  // release velocity of the last NoteOff
        EngagementFollower follower{};
    };

    [[nodiscard]] static bool validKey(int key) noexcept { return key >= 0 && key < kKeyCount; }
    [[nodiscard]] double effectiveLKey(const Slot& s) const noexcept {
        return s.latched ? 1.0 : s.lKey;
    }
    void retarget(int key) noexcept;
    void retargetAll() noexcept;
    void markMoving(int key) noexcept;

    double fs_{0.0};
    double p0_{0.35};
    double p1_{0.65};
    double sustain_{0.0};
    double lPed_{0.0};
    bool sostenutoDown_{false};
    std::array<Slot, kKeyCount> slots_{};
    std::array<std::uint64_t, 2> movingMask_{};
};

}  // namespace pme::dmp
