// SPDX-License-Identifier: MIT
#pragma once

/// @file string.hpp
/// Stiff string description and compiled mode tables (SPEC-STR; SPEC-API).

#include <cstdint>
#include <span>
#include <vector>

#include "pme/osc.hpp"
#include "pme/status.hpp"

namespace pme {

/// End conditions of a string (SPEC-STR "Modes").
enum class Boundary : std::uint8_t {
    Pinned,   ///< Pinned-pinned shapes and frequencies (default).
    Clamped,  ///< Pinned shapes with Fletcher's frequency correction.
};

/// Which end of the string moves with the bridge (SPEC-BRG "Decomposition", SPEC-API).
enum class BridgeEnd : std::uint8_t {
    AtL,  ///< Bridge at x = L: speaking segment, lifting function x / L.
    At0,  ///< Bridge at x = 0: rear or duplex segment, lifting function 1 - x / L.
};

/// Physical description of one string (SPEC-API, SPEC-STR). Sample-rate independent. SI units.
struct StringParams {
    double L{};        ///< Speaking length (m), SPEC-STR. A rear or duplex segment holds its own L_d.
    double dCore{};    ///< Core diameter d_c (m), SPEC-STR "Wound strings".
    double rhoCore{};  ///< Core density rho_c (kg/m^3), SPEC-STR.
    double ECore{};    ///< Core Young's modulus E_c (Pa), SPEC-STR.
    double dOuter{};   ///< Outer (winding) diameter d_o (m), SPEC-STR.
    double rhoWind{};  ///< Winding density rho_w (kg/m^3), SPEC-STR.
    double packing{};  ///< Winding packing factor eta_w, 0.75 to 0.9 for round wire (SPEC-STR).
    double epsWind{};  ///< Winding stiffness fraction eps_w of the core's EI (SPEC-STR).
    double f1Target{};  ///< Target first-partial frequency f_1 from the tuning curve (Hz), SPEC-STR.
    double b1{};  ///< Intrinsic loss b_1 (1/s) in sigma_n = b_1 + b_2 sqrt(omega_n) + b_3 omega_n^2.
    double b2{};  ///< Intrinsic loss b_2 (s^(-1/2)) (SPEC-STR "Losses").
    double b3{};  ///< Intrinsic loss b_3 (s) (SPEC-STR "Losses").
    std::vector<double> sigmaOverride;  ///< Optional per-partial sigma_n (1/s); empty = use b1..b3.
    Boundary boundary{Boundary::Pinned};  ///< End conditions (SPEC-STR "Modes").
    double detuneH{};  ///< Horizontal-polarisation detune Delta_h in cents (SPEC-STR "Polarisations").
    BridgeEnd bridgeEnd{BridgeEnd::AtL};  ///< Which end moves with the bridge (SPEC-BRG).
};

/// Compiled mode tables of one string (SPEC-STR "Contract": T, B, k, omega, sigma tables,
/// oscillator coefficients for both polarisations, shape vectors at registered points).
/// Immutable once built. Provisional layout: SPEC-STR names the contents but not the members.
struct StringModes {
    double fs{};                  ///< Sample rate the coefficients were compiled for (Hz).
    double L{};                   ///< Length L (m), copied from the description.
    double tension{};             ///< Tension T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2 (N), SPEC-STR.
    double rhoA{};                ///< Linear mass density rho A (kg/m), SPEC-STR.
    double EI{};                  ///< Bending stiffness EI (N m^2), SPEC-STR.
    double inharmonicity{};       ///< B = pi^2 EI / (T L^2), dimensionless, SPEC-STR.
    std::vector<double> k;        ///< Wavenumbers k_n = n pi / L (rad/m), n = 1..N, SPEC-STR.
    std::vector<double> omega;    ///< Angular frequencies omega_n (rad/s), SPEC-STR.
    std::vector<double> sigma;    ///< Decay rates sigma_n (1/s), SPEC-STR.
    std::vector<OscCoeffs> oscV;  ///< Vertical-polarisation oscillator coefficients per mode.
    std::vector<OscCoeffs> oscH;  ///< Horizontal-polarisation coefficients (detuned) per mode.
    /// Window-averaged shape values phiBar_n at every registered point, computed float64 and
    /// stored float32 (SPEC-STR "Forces and observations"). phiBar[p][n] is point p (order of the
    /// `registeredPoints` argument of `compileString`), mode n; unit m^(-1/2).
    std::vector<std::vector<float>> phiBar;
};

/// Compile a string's mode tables (SPEC-STR "Contract", SPEC-API).
///
/// @param params           The string description.
/// @param fs               Sample rate (Hz).
/// @param fMax             Highest kept frequency (Hz); modes above it are dropped (SPEC-OSC).
/// @param registeredPoints Points x_0 (m) at which phiBar is evaluated (exciter, damper, barrier,
///                         bridge).
/// @param out              Receives the tables on success.
/// Thread: non-audio. Allocates: yes. Failures: `InvalidArgument` for non-physical parameters
/// (fs <= 0, L <= 0 ...); `Rejected` if no mode survives the bandwidth rule; `NotImplemented` in
/// the P0.4 stub.
[[nodiscard]] Status compileString(const StringParams& params, double fs, double fMax,
                                   std::span<const double> registeredPoints, StringModes& out);

}  // namespace pme
