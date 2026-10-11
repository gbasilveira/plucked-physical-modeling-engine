// SPDX-License-Identifier: MIT
// Diagonal damper: extra per-mode damping and stiffness with ramped coefficient refresh (A6-R1).
//
// SPEC sections implemented: SPEC-DMP "Damper model" (Diagonal); SPEC-OSC "Time-varying
// parameters", "Bandwidth"; SPEC-STR "Forces and observations" (raised-cosine window).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "pme/dampers.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/osc/ramp.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::dmp {

/// phi-bar_{d,n} = phi_n(x_d) W(k_n) in float64 for n = 1 .. N, x_d = xRatio L, raised-cosine
/// window of width `width` (SPEC-DMP "Damper model"; SPEC-STR "Forces and observations"). Same
/// formula as A2's `windowedShapes` but kept in float64 for coefficient work. Unit m^(-1/2).
/// Thread: non-audio. Allocates: yes.
[[nodiscard]] std::vector<double> padShape(const StringModes& modes, const DamperParams& params);

/// Result of the overdamping check over e in [0, 1].
struct OverdampingReport {
    Status status{Status::Ok};  ///< `Ok`, or `Rejected` if some mode is rejected at some e
                                ///< (sigma_d >= omega_d, or omega_d dt > 0.95 pi).
    double worstRatio{0.0};     ///< Largest sigma_{n,d} / omega_{n,d} over the modes and the e
                                ///< sampled (omega_{n,d} = sqrt(omega_n^2 + e K_d phi^2 / rhoA)).
    std::size_t worstMode{0};   ///< Zero-based mode index of `worstRatio`.
    double worstE{0.0};         ///< e at which it occurs.
    std::size_t rejectedMode{static_cast<std::size_t>(-1)};  ///< First rejected mode, or -1.
    double rejectedE{0.0};      ///< e at which it was rejected.
};

/// Diagonal damper of one string's vertical polarisation (SPEC-DMP "Diagonal").
///
/// For engagement e, per mode (SPEC-DMP "Damper model"):
///   sigma_{n,d} = sigma_n + e R_d phi-bar_{d,n}^2 / (2 rhoA),
///   omega_{n,d}^2 = omega_n^2 + e K_d phi-bar_{d,n}^2 / rhoA.
/// Only the vertical polarisation is touched; the horizontal tables of `StringModes` are not
/// read. The mode count is fixed: a configuration that would reject any mode is itself rejected.
///
/// Per-sub-block use (audio thread, SPEC-EXEC step 10): `plan(eEnd, S, ramps)` then, for each
/// sample s of the sub-block, `osc::applyRampsAt(bank, firstMode, ramps, s)`, then
/// `commit(bank, firstMode)`.
class DiagonalDamper {
public:
    /// Analyse a damper without keeping it: the largest sigma_d / omega_d and whether any mode
    /// is rejected, over `steps + 1` values of e from 0 to 1. `InvalidArgument` in `status`
    /// is not used: invalid input gives status `Rejected` with `rejectedMode` 0.
    /// Thread: non-audio. Allocates: yes.
    [[nodiscard]] static OverdampingReport analyse(const StringModes& modes,
                                                   const DamperParams& params, int steps = 16);

    /// Build the damper from the string's mode tables (vertical polarisation). Rest state is
    /// e = 0 (the coefficients equal the string's own). `InvalidArgument` for invalid
    /// parameters or empty tables, `Rejected` if `analyse` finds a rejected mode (nothing is
    /// clamped or skipped; the object is unchanged). Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const StringModes& modes, const DamperParams& params);

    [[nodiscard]] std::size_t modes() const noexcept { return n_; }
    [[nodiscard]] const std::vector<double>& phiBar() const noexcept { return phiBar_; }

    /// sigma_{n,d}(e), omega_{n,d}(e) (n < modes()). Thread: any. Allocates: no.
    [[nodiscard]] double sigmaAt(std::size_t n, double e) const noexcept;
    [[nodiscard]] double omegaAt(std::size_t n, double e) const noexcept;

    /// Float64 oscillator coefficients of mode n at engagement e (compileOscillatorF64 of
    /// omega_{n,d}, sigma_{n,d}). Thread: audio (no allocation). Allocates: no.
    [[nodiscard]] Status coefficients(std::size_t n, double e,
                                      osc::OscCoeffsF64& out) const noexcept;

    /// Set mode `firstMode + n` of `bank` to the coefficients at engagement `e` for every n and
    /// remember e as the value in force (no ramp; state kept). Thread: non-audio or between
    /// blocks. Allocates: no.
    [[nodiscard]] Status initialise(double e, osc::ModeBank& bank, std::size_t firstMode) noexcept;

    /// Plan one sub-block of `subBlock` samples (16 or 32) from the coefficients in force to
    /// those at engagement `eEnd` (e at the end of the sub-block): for each mode, A1's
    /// `exceedsRampThreshold` decides between an immediate switch and a linear ramp. `ramps`
    /// must hold modes() entries. Returns false when eEnd equals the value in force (nothing to
    /// apply; `ramps` untouched). A mode whose target fails to compile keeps its coefficients
    /// (counted in `refreshFailures()`; unreachable after a successful `configure`).
    /// Cost: one `compileOscillatorF64` plus one `planRamp` per mode (about 0.2 us per mode on
    /// the development machine, see docs/dev/dampers.md). Thread: audio. Allocates: no.
    [[nodiscard]] bool plan(double eEnd, int subBlock, std::span<osc::CoeffRamp> ramps) noexcept;

    /// After the sub-block: make the planned target the value in force and write it to the bank
    /// (including omegaHat2 and sigmaHat, which the ramp does not touch). No-op if the last
    /// `plan` returned false. Thread: audio. Allocates: no.
    void commit(osc::ModeBank& bank, std::size_t firstMode) noexcept;

    /// Engagement in force and the number of refresh failures so far.
    [[nodiscard]] double engagementInForce() const noexcept { return eForce_; }
    [[nodiscard]] std::uint32_t refreshFailures() const noexcept { return failures_; }

private:
    std::size_t n_{0};
    double fs_{0.0};
    double rhoA_{0.0};
    DamperParams params_{};
    std::vector<double> phiBar_, omega_, sigma_;
    std::vector<double> dSigma_, dOmega2_;  // R_d phi^2 / (2 rhoA), K_d phi^2 / rhoA
    std::vector<OscCoeffs> force_, target_;  // in force and planned (float32 coefficients)
    std::vector<double> omegaForce_, sigmaForce_, omegaTarget_, sigmaTarget_;
    double eForce_{0.0};
    double eTarget_{0.0};
    bool planned_{false};
    std::uint32_t failures_{0};
};

}  // namespace pme::dmp
