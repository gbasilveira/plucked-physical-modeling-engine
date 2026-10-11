// SPDX-License-Identifier: MIT
// Internal stiff-string compilation API (A2). Public entry point: pme/string.hpp (`compileString`).
//
// SPEC sections implemented: SPEC-STR "Modes", "Wound strings", "Tension for pitch", "Losses",
// "Forces and observations", "Polarisations", "Duplex and rear segments", "Truncation",
// "Contract". All arithmetic is float64; only `phiBar` and the float32 fields of `OscCoeffs`
// are float32 (SPEC-CONV).
#pragma once

#include <cstdint>
#include <span>

#include "pme/instrument.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::string {

/// Window of a contact (SPEC-STR "Forces and observations").
enum class WindowKind : std::uint8_t {
    Point,         ///< W = 1.
    Rectangular,   ///< W = sin(u)/u, u = k b / 2.
    RaisedCosine,  ///< W = sin(u)/u * pi^2 / (pi^2 - u^2), u = k b / 2.
};

/// Upper bound on the number of modes of one string (guards against absurd inputs such as
/// f1 = 1e-6 Hz); a string needing more modes is truncated here.
inline constexpr int kMaxModes = 1 << 16;

/// Check the physical description (ADR-0006) without computing anything: `InvalidArgument` for
/// non-physical input, else `Ok`. Does not check fs or the tension; `compileString` does.
/// Thread: non-audio. Allocates: no.
[[nodiscard]] Status validate(const StringParams& params) noexcept;

/// rho A = (pi/4)[rho_c d_c^2 + eta_w rho_w (d_o^2 - d_c^2)] (SPEC-STR "Wound strings"). A plain
/// string (d_o == d_c) has a winding term of exactly 0 whatever rho_w and eta_w hold.
/// Requires valid parameters. Thread: any. Allocates: no.
[[nodiscard]] double linearDensity(const StringParams& params) noexcept;

/// EI = (1 + eps_w) pi E_c d_c^4 / 64 (SPEC-STR "Wound strings"). Requires valid parameters.
/// Thread: any. Allocates: no.
[[nodiscard]] double bendingStiffness(const StringParams& params) noexcept;

/// f1 = f_ref 2^((key - 69)/12) 2^((stretch[key] + d_s)/1200) (SPEC-STR "Tension for pitch",
/// SI-0004). stretch[key] = 0 for a key outside the vector (including key < 0).
/// compileInstrument calls this for groups with key >= 0. Thread: any. Allocates: no.
[[nodiscard]] double tuningF1(const TuningCurve& curve, int key,
                              double unisonDetuneCents) noexcept;

/// Tension T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2 for the given f1 (SPEC-STR "Tension for pitch").
/// Requires valid parameters. Thread: any. Allocates: no.
[[nodiscard]] double tensionForPitch(const StringParams& params, double f1) noexcept;

/// Inverse of `tensionForPitch`: f1 = sqrt((T + pi^2 EI / L^2) / (4 L^2 rhoA)). A caller that
/// must give a rear segment a prescribed tension sets `f1Target` to this value (SI-0010 item 1).
/// Requires valid parameters. Thread: any. Allocates: no.
[[nodiscard]] double f1ForTension(const StringParams& params, double tension) noexcept;

/// Number of modes N kept by the bandwidth rule for both polarisations (SPEC-STR "Truncation",
/// SPEC-OSC "Bandwidth"): the largest N such that modes 1..N all pass. Returns 0 if the
/// parameters or fs are invalid, the tension is not positive, or mode 1 fails the rule; at most
/// `kMaxModes`. Thread: non-audio. Allocates: no.
[[nodiscard]] int modeCount(const StringParams& params, double fs, double fMax) noexcept;

/// Window factor W(k) of a window of full width b at wavenumber k (SPEC-STR "Forces and
/// observations"), u = k b / 2. Point: 1. Rectangular: sin(u)/u. RaisedCosine:
/// sin(u)/u * pi^2/(pi^2 - u^2). The removable singularities at u = 0 and (raised cosine)
/// u = pi are handled without loss of accuracy. Even in u. Thread: any. Allocates: no.
[[nodiscard]] double windowFactor(WindowKind kind, double k, double b) noexcept;

/// phi-bar_n = phi_n(x0) W(k_n) for n = 1..min(out.size(), N), formed in float64 from L, k_n and
/// x0 (not from the float32 `phiBar`) and stored float32; entries of `out` beyond N are set
/// to 0. Used by compileInstrument for hammers, dampers and fingers (SPEC-STR "Contract").
/// Thread: non-audio. Allocates: no.
void windowedShapes(const StringModes& modes, double x0, WindowKind kind, double b,
                    std::span<float> out) noexcept;

/// Add the rear-segment mute damping sigma_mute = m_d sigma_felt to every sigma_n, recompile
/// `oscV` and `oscH` (SPEC-STR "Duplex and rear segments", SI-0010 item 2) and, if a mode becomes
/// overdamped, truncate all tables to the longest passing prefix. `Rejected` (tables unchanged)
/// if mode 1 would fail. `detuneHCents` is the string's `detuneH`, which `StringModes` does not
/// store and the horizontal coefficients need. `InvalidArgument` for muteRatio outside [0, 1],
/// sigmaFelt < 0 or not finite, or detuneHCents not finite; tables unchanged. With zero added
/// damping the tables are untouched. sigma_felt has no default (SI-0010 open; orchestrator
/// provisional value 20 1/s). Thread: non-audio. Allocates: yes.
[[nodiscard]] Status applyMute(StringModes& modes, double muteRatio, double sigmaFelt,
                               double detuneHCents);

}  // namespace pme::string
