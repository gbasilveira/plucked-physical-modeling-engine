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
    double duration{};   ///< Pulse duration t_p (s); 0.5 to 5 ms.
    double peakForce{};  ///< Peak force F_p (N).
    double position{};   ///< Window centre x_p (m).
    double width{};      ///< Window width b_p (m).
    double angle{};      ///< Polarisation angle theta_p (rad).
};

/// Plectrum: pick, quill, mizrab or bachi (SPEC-PLK "Exciters", row "Plectrum").
struct Plectrum {
    double tipMass{};          ///< Tip mass m_p (kg).
    double driverStiffness{};  ///< Spring K_p between tip and driver (N/m).
    double driverSpeed{};      ///< Driver speed v_d along the pluck direction (m/s).
    double slipHeight{};       ///< Release threshold h_slip (m).
    double contactK{};         ///< Contact stiffness K of the one-sided power law (N/m^alpha).
    double contactAlpha{};     ///< Contact exponent alpha (dimensionless).
};

/// Finger (SPEC-PLK "Exciters", row "Finger"). Provisional: the table lists m_f, K_f, alpha_f,
/// mu_f, b_f; the driver speed and h_slip follow "As plectrum".
struct Finger {
    double mass{};         ///< Finger tip mass m_f (kg).
    double stiffness{};    ///< Contact stiffness K_f (N/m^alpha_f).
    double alpha{};        ///< Contact exponent alpha_f (dimensionless).
    double muFlesh{};      ///< Hunt-Crossley flesh damping mu_f (s/m).
    double width{};        ///< Contact width b_f (m); approximately 10 mm.
    double driverSpeed{};  ///< Driver speed v_d (m/s). Provisional ("as plectrum").
    double slipHeight{};   ///< Release threshold h_slip (m); larger than a plectrum's.
};

/// An exciter attached to a note group (SPEC-API `ExciterParams`).
struct ExciterParams {
    std::variant<HammerParams, PluckPulse, Plectrum, Finger> e;  ///< The exciter model.
};

}  // namespace pme
