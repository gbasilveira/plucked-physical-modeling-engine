// SPDX-License-Identifier: MIT
// Parallel-axis bridge models (A4-R2): conductance, mass-spring-damper and modal set, all as one
// representation (a set of bridge modes plus a conductance).
//
// SPEC sections implemented: SPEC-BODY "Local bridge model (per bridge point, per sample, exact)"
// and "Contracts" (the modal-set interface used by B3's `buildLocalBridge`); SPEC-OSC "Bandwidth".
#pragma once

#include <vector>

#include "pme/body.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/status.hpp"

namespace pme::bridge {

/// One bridge mode: r'' + 2 sigma r' + Omega^2 r = Phi F (unit modal mass, SPEC-OSC), with the
/// bridge displacement w = sum Phi r + w_R (SPEC-BODY "Local bridge model").
struct BridgeMode {
    double omega{};  ///< Omega_k in rad/s (>= 0; 0 is a free mass, handled explicitly).
    double sigma{};  ///< sigma_k in 1/s (>= 0).
    double phi{};    ///< Phi_k in kg^(-1/2).
};

/// Model of the bridge's parallel (soundboard) axis. Sample-rate independent; `BridgePoint`
/// compiles the oscillator coefficients at the engine's fs, like every other fs-dependent
/// quantity (SPEC-BODY "Contracts").
struct ParallelModel {
    /// How the model was made; decides what happens to a mode the bandwidth rule rejects.
    enum class Kind : unsigned char {
        Conductance,       ///< No modes; pure conductance (M = 0).
        MassSpringDamper,  ///< One mode (M = 1); a rejected mode rejects the model.
        ModalSet,          ///< M modes of a body; modes failing the bandwidth rule are dropped.
    };
    Kind kind{Kind::Conductance};
    std::vector<BridgeMode> modes;  ///< The M bridge modes.
    double conductance{};           ///< G (s/kg): w_R^{n+1} = w_R^n + dt G F^n.
};

/// Conductance G >= 0 only (SPEC-BODY "Local bridge model" with M = 0). `G = 0` is a rigid bridge.
/// `InvalidArgument` (out unchanged) for G < 0 or not finite. Thread: non-audio.
[[nodiscard]] Status conductanceModel(double conductance, ParallelModel& out);

/// Mass-spring-damper (m, k_b, r_b) as one mode: Omega = sqrt(k_b/m), sigma = r_b/(2m),
/// Phi = 1/sqrt(m), G = 0, so that w'' + 2 sigma w' + Omega^2 w = F/m. k_b = 0 is a free mass.
/// `InvalidArgument` (out unchanged) for m <= 0, k_b < 0, r_b < 0 or a non-finite value.
/// Thread: non-audio. Allocates: yes.
[[nodiscard]] Status massSpringDamperModel(double mass, double stiffness, double damping,
                                           ParallelModel& out);

/// Modal set from a `LocalBridge` (B3's `buildLocalBridge` output): modes (Omega_k, sigma_k,
/// Phi_k) and G = G_hf,b. `InvalidArgument` (out unchanged) for inconsistent vector lengths, a
/// negative or non-finite Omega, sigma or G_hf, or a non-finite Phi. Thread: non-audio.
[[nodiscard]] Status modalSetModel(const LocalBridge& local, ParallelModel& out);

/// Oscillator coefficients of one bridge mode at fs (SPEC-OSC "Discrete scheme"). Identical to
/// `osc::compileOscillatorF64` except for Omega = 0, a free mass, which A1 refuses as overdamped:
/// there omegaHat^2 = 0 and sigmaHat dt = tanh(sigma dt), which keeps the pole at 1 and the decay
/// pole exact (the overdamped continuation of the SPEC-OSC formulas with Omega -> 0). `out` is
/// unchanged unless the result is `Ok`. Thread: non-audio. Allocates: no.
[[nodiscard]] Status compileBridgeMode(double omega, double sigma, double fs,
                                       osc::OscCoeffsF64& out) noexcept;

}  // namespace pme::bridge
