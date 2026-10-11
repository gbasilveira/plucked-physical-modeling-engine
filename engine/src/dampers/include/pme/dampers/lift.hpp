// SPDX-License-Identifier: MIT
// Lift fraction, engagement target and the one-pole engagement follower (A6-R3, A6-R4).
//
// SPEC sections implemented: SPEC-DMP "Lift fraction", "Timing", "Undamped notes"; SPEC-EXEC
// per-sample step 10 (advance the engagement). Everything here is noexcept and allocation-free;
// only the controller's and the damper models' configuration is non-real-time.
#pragma once

#include <cmath>
#include <cstdint>

#include "pme/dampers.hpp"
#include "pme/status.hpp"

namespace pme::dmp {

/// Largest distance |e - e_target| at which the follower snaps onto the target (the exact
/// one-pole response never reaches it). 1e-9 is far below the 0.1% ramp threshold of SPEC-OSC
/// "Time-varying parameters", so the snap never causes an audible step.
inline constexpr double kEngagementSnap = 1.0e-9;

/// Sustain-pedal contribution l_ped = clamp((P - P0) / (P1 - P0), 0, 1) (SPEC-DMP "Lift
/// fraction"). Requires P0 < P1. Thread: any. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): P, P0, P1 in SPEC order
[[nodiscard]] inline double pedalLift(double depth, double p0, double p1) noexcept {
    const double t = (depth - p0) / (p1 - p0);  // SPEC-DMP lift fraction
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

/// l = max(l_key, l_ped) (SPEC-DMP "Lift fraction"). Thread: any. Allocates: no.
[[nodiscard]] inline double liftFraction(double lKey, double lPed) noexcept {
    return lKey > lPed ? lKey : lPed;  // SPEC-DMP lift fraction
}

/// e_target = (1 - l)^eta_d (SPEC-DMP "Lift fraction"); l is clamped to [0, 1].
/// Thread: any. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): l then eta_d in SPEC order
[[nodiscard]] inline double engagementTarget(double lift, double etaD) noexcept {
    const double l = lift < 0.0 ? 0.0 : (lift > 1.0 ? 1.0 : lift);
    return std::pow(1.0 - l, etaD);  // SPEC-DMP lift fraction
}

/// tau_down = tau_0 / (1 + k_r nu_rel) (SPEC-DMP "Timing"); nu_rel is clamped to [0, 1].
/// Thread: any. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): tau_0, k_r, nu_rel in SPEC order
[[nodiscard]] inline double landingTau(double tau0, double kRelease, double nuRel) noexcept {
    const double v = nuRel < 0.0 ? 0.0 : (nuRel > 1.0 ? 1.0 : nuRel);
    return tau0 / (1.0 + kRelease * v);  // SPEC-DMP timing
}

/// `InvalidArgument` unless 0 <= P0 < P1 <= 1 (finite). Thread: non-audio. Allocates: no.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): P0 then P1
[[nodiscard]] Status validateHalfPedal(double p0, double p1) noexcept;

/// `InvalidArgument` for non-physical damper parameters: xRatio outside [0, 1], width < 0,
/// Kd < 0, Rd < 0, tau0 <= 0, tauUp <= 0, kRelease < 0, etaD <= 0 or any value not finite.
/// Thread: non-audio. Allocates: no.
[[nodiscard]] Status validateParams(const DamperParams& p) noexcept;

/// Engagement e of one damper and its one-pole approach to e_target (SPEC-DMP "Timing").
///
/// e^{n+1} = e_target + (e^n - e_target) exp(-dt / tau), exact for a constant target, with
/// tau = tau_up while lifting (e_target < e) and tau_down while landing (e_target > e).
class EngagementFollower {
public:
    /// Set the time constants and the sample rate; e is set to `initialE` (a damper at rest
    /// lies on the string: 1) and the target to the same value. `fs` > 0 and valid `params` are
    /// the caller's duty (`validateParams`). Thread: non-audio. Allocates: no.
    void configure(const DamperParams& params, double fs, double initialE) noexcept;

    /// New e_target; the landing time constant is computed from `nuRel` of the last NoteOff.
    /// Thread: audio. Allocates: no.
    void setTarget(double target, double nuRel) noexcept;

    /// One sample (SPEC-EXEC step 10). Returns true while e is still moving.
    /// Thread: audio. Allocates: no.
    bool step() noexcept;

    /// e after `n` further samples at the present target, in closed form (used to look ahead to
    /// the end of a sub-block). Thread: audio. Allocates: no.
    [[nodiscard]] double projected(int n) const noexcept;

    [[nodiscard]] double value() const noexcept { return e_; }
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] bool moving() const noexcept { return e_ != target_; }
    [[nodiscard]] double tauUp() const noexcept { return tauUp_; }
    [[nodiscard]] double tauDown() const noexcept { return tauDown_; }
    [[nodiscard]] double etaD() const noexcept { return params_.etaD; }
    /// Force e and the target (no ramp). Thread: any. Allocates: no.
    void setValue(double e) noexcept {
        e_ = e;
        target_ = e;
    }

private:
    DamperParams params_{};
    double dt_{0.0};
    double e_{0.0};
    double target_{0.0};
    double tauUp_{0.0};
    double tauDown_{0.0};
    double decayUp_{0.0};
    double decayDown_{0.0};
};

}  // namespace pme::dmp
