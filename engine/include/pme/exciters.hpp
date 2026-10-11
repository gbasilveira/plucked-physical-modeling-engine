// SPDX-License-Identifier: MIT
#pragma once

/// @file exciters.hpp
/// Hammer, plucked exciters and una corda (SPEC-HAM, SPEC-PLK; SPEC-API).

#include <cstdint>
#include <variant>

namespace pme {

/// Felt dissipation law of a hammer (SPEC-HAM "Felt dissipation", "Alternative: Stulov").
enum class FeltLaw : std::uint8_t {
    HuntCrossley,  ///< Hunt-Crossley dashpot R = w K mu_HC [delta]_+^p (default).
    Stulov,        ///< Stulov hereditary felt; not provably passive, monitors must check it.
};

/// Hammer parameters (SPEC-API, SPEC-HAM). Defaults are SPEC-HAM's typical values.
struct HammerParams {
    double mass{};         ///< Hammer mass m_H (kg); typical 4 to 12 g (SPEC-HAM).
    double K{};            ///< Felt stiffness K (N/m^p) (SPEC-HAM "Contact").
    double p{};            ///< Felt exponent p (dimensionless); typical 2 to 4 (SPEC-HAM).
    double muHC{};         ///< Hunt-Crossley hysteresis mu_HC (s/m) (SPEC-HAM).
    double width{};        ///< Contact window width b_H (m); typical 5 to 15 mm (SPEC-HAM).
    double strikeRatio{};  ///< Strike position beta_c = x_c / L (dimensionless) (SPEC-HAM).
    FeltLaw law{FeltLaw::HuntCrossley};  ///< Felt dissipation law (SPEC-HAM).
    double stulovEps{};    ///< Stulov hereditary amplitude epsilon (dimensionless) (SPEC-HAM).
    double stulovTau{};    ///< Stulov relaxation time tau (s) (SPEC-HAM).
    double vMin{0.3};      ///< v_min of the velocity map (m/s); typical 0.3 (SPEC-HAM).
    double vMax{6.0};      ///< v_max of the velocity map (m/s); typical 6 (SPEC-HAM).
    double gammaV{1.5};    ///< gamma_v of the velocity map (dimensionless); typical 1.5 (SPEC-HAM).
    double dCheck{0.005};  ///< Release distance d_check (m); default 5 mm (SPEC-HAM).
};

/// Una corda and soft-pedal parameters (SPEC-API, SPEC-HAM "Una corda (grand)" and "Soft pedal
/// (upright)"). Defaults are SPEC-INST's piano defaults.
struct UnaCordaParams {
    double s0{0.3};        ///< Smoothstep lower bound s_0 (dimensionless), default 0.3.
    double s1{0.9};        ///< Smoothstep upper bound s_1 (dimensionless), default 0.9.
    double kappaUC{0.6};   ///< Stiffness factor kappa_uc (dimensionless), typical 0.6.
    double deltaPUC{0.0};  ///< Exponent shift Delta p_uc (dimensionless), default 0.
    double rhoUpright{
        0.3};  ///< Upright soft-pedal velocity scale rho_up (dimensionless), typical 0.3.
    int leavingString{0};  ///< Zero-based index of the string that leaves the hammer; -1 for none
                           ///< (single string). SPEC-HAM table "String that leaves the hammer".
};

/// Generic force-pulse pluck (SPEC-PLK "Exciters", row "Force pulse").
struct PluckPulse {
    double duration{};      ///< Pulse duration t_p (s); 0.5 to 5 ms.
    double peakForceMax{};  ///< F_p,max (N): peak force at strength 1 (SPEC-PLK "Strength map").
    double position{};      ///< Window centre x_p (m) on the segment's x axis; default position.
    double width{};         ///< Window width b_p (m).
    double angle{};         ///< Polarisation angle theta_p (rad) used by NoteOn launches.
    double strengthGamma{1.0};  ///< gamma_p of the strength map (dimensionless); default 1.
};

/// Plectrum: pick, quill, mizrab or bachi (SPEC-PLK "Exciters", row "Plectrum").
struct Plectrum {
    double tipMass{};          ///< Tip mass m_p (kg).
    double driverStiffness{};  ///< Spring K_p between tip and driver (N/m).
    double driverSpeedMax{};   ///< v_d,max (m/s): driver speed at strength 1 (SPEC-PLK).
    double slipHeight{};       ///< Release threshold h_slip (m).
    double contactK{};         ///< Contact stiffness K of the one-sided power law (N/m^alpha).
    double contactAlpha{};     ///< Contact exponent alpha (dimensionless).
    double position{};         ///< Default pluck position x_p (m) on the segment's x axis.
    double angle{};            ///< Pluck direction theta_p (rad) used by NoteOn launches.
    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
};

/// Finger (SPEC-PLK "Exciters", row "Finger"): the plectrum's structure with a wide soft
/// Hunt-Crossley flesh contact (SI-0005).
struct Finger {
    double tipMass{};          ///< Finger tip mass m_f (kg).
    double driverStiffness{};  ///< Driver spring K_f between tip and driver (N/m).
    double driverSpeedMax{};   ///< v_d,max (m/s): driver speed at strength 1.
    double slipHeight{};       ///< Release threshold h_slip,f (m); larger than a plectrum's.
    double contactK{};         ///< Flesh contact stiffness K_c,f (N/m^alpha_f).
    double contactAlpha{};     ///< Contact exponent alpha_f (dimensionless).
    double muFlesh{};          ///< Hunt-Crossley flesh damping mu_f (s/m).
    double width{};            ///< Raised-cosine contact width b_f (m); approximately 10 mm.
    double position{};         ///< Default pluck position x_p (m) on the segment's x axis.
    double angle{};            ///< Pluck direction theta_p (rad) used by NoteOn launches.
    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
};

/// Initial displacement (SPEC-PLK "Exciters", row "Initial displacement"): the segment is set
/// to the closed-form triangle of height A at x_p, at rest, at the event's sample.
struct InitialDisplacement {
    double amplitudeMax{};     ///< A_max (m): triangle height at strength 1.
    double position{};         ///< Default apex position x_p (m) on the segment's x axis.
    double angle{};            ///< Polarisation angle theta_p (rad) used by NoteOn launches.
    double strengthGamma{1.0}; ///< gamma_p of the strength map (dimensionless); default 1.
};

/// An exciter attached to a note group (SPEC-API `ExciterParams`).
struct ExciterParams {
    std::variant<HammerParams, PluckPulse, Plectrum, Finger, InitialDisplacement>
        e;  ///< The exciter model.
};

}  // namespace pme
