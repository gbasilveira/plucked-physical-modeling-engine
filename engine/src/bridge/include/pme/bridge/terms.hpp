// SPDX-License-Identifier: MIT
// Internal bridge-term API (A4-R1): lifting-function overlaps g_n, force coefficients a_n and the
// bridge force of one segment. Public entry point: pme/bridge.hpp (`compileBridgeTerms`).
//
// SPEC sections implemented: SPEC-BRG "Modal equation with a moving support", "Bridge force",
// "Contract"; SPEC-CONV (float64 at compile time, float32 stored).
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::bridge {

/// Which polarisation's discrete omegaHat^2 enters a_n (SPEC-BRG "Bridge force": the discrete
/// omegaHat^2 of the polarisation that is coupled MUST be used).
enum class Polarisation : unsigned char {
    Vertical,    ///< `oscV[n].omegaHat2`.
    Horizontal,  ///< `oscH[n].omegaHat2` (detuned by the string's `detuneH`).
};

/// g_n in float64 (SPEC-BRG "Modal equation with a moving support"):
/// sqrt(2/L) (-1)^(n+1) / k_n for `AtL`, sqrt(2/L) / k_n for `At0`, n = 1..N.
/// `InvalidArgument` (`g` unchanged) under the conditions of `compileBridgeTerms`.
/// Thread: non-audio. Allocates: yes.
[[nodiscard]] Status liftingOverlaps(const StringModes& modes, BridgeEnd end,
                                     std::vector<double>& g);

/// a_n = rhoA omegaHat_n^2 g_n in float64 for the given polarisation (SPEC-BRG "Bridge force").
/// Same failure conditions as `compileBridgeTerms`. Thread: non-audio. Allocates: yes.
[[nodiscard]] Status forceCoefficients(const StringModes& modes, BridgeEnd end, Polarisation pol,
                                       std::vector<double>& a);

/// Float32 coefficient vector of the horizontal polarisation, a_h,n = rhoA omegaHat_h,n^2 g_n,
/// from `oscH` (SPEC-BRG "Bridge force"). `SegmentBridgeTerms` (frozen) holds only the vertical
/// vector; this is the horizontal companion until an ICR adds a member. `aH` is unchanged on
/// failure. Thread: non-audio. Allocates: yes.
[[nodiscard]] Status horizontalForceCoefficients(const StringModes& modes, BridgeEnd end,
                                                 std::vector<float>& aH);

/// Bridge force F_b = sum_n a_n q_n - (T/L) w_b with explicit coefficients (SPEC-BRG "Contract").
/// One dot product in fixed index order (scalar reference for the A8 kernel) plus the static
/// term. `S` is the state type (float in the engine, double in tests); `C` the coefficient type.
/// Thread: audio. Allocates: no.
template <class S, class C>
[[nodiscard]] S bridgeForce(std::span<const C> a, double tOverL, const S* q, S wb) noexcept {
    S acc = S{0};
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) {
        acc += static_cast<S>(a[i]) * q[i];  // SPEC-BRG Bridge force: sum a_n q_n
    }
    return acc - static_cast<S>(tOverL) * wb;  // SPEC-BRG Bridge force: - (T/L) w_b
}

/// F_b of one segment from its compiled terms (SPEC-BRG "Contract", vertical polarisation).
/// Thread: audio. Allocates: no.
template <class S>
[[nodiscard]] S bridgeForce(const SegmentBridgeTerms& terms, const S* q, S wb) noexcept {
    return bridgeForce<S, float>(std::span<const float>(terms.a), terms.tOverL, q, wb);
}

/// The engine's float instantiation (SPEC-BRG "Contract").
[[nodiscard]] inline float bridgeForce(const SegmentBridgeTerms& terms, const float* q,
                                       float wb) noexcept {
    return bridgeForce<float>(terms, q, wb);
}

}  // namespace pme::bridge
