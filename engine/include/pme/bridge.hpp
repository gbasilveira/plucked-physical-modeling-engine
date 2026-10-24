// SPDX-License-Identifier: MIT
#pragma once

/// @file bridge.hpp
/// Bridge coupling terms and bridge-point description (SPEC-BRG; SPEC-API).

#include <vector>

#include "pme/status.hpp"
#include "pme/string.hpp"
#include "pme/types.hpp"

namespace pme {

/// Per-segment bridge coupling terms (SPEC-BRG "Contract": g_n, a_n and T/L). Immutable once
/// built. Provisional layout: SPEC-BRG names the contents but not the members.
struct SegmentBridgeTerms {
    BridgeEnd end{BridgeEnd::AtL};  ///< Lifting-function end (SPEC-BRG "Decomposition").
    std::vector<float> g;  ///< g_n = <psi, phi_n> in m^(1/2), computed float64, stored float32.
    std::vector<float> a;  ///< a_n = rhoA omegaHat_n^2 g_n (SPEC-BRG "Bridge force"), float32.
    double tOverL{};       ///< T / L in N/m (SPEC-BRG "Bridge force").
};

/// A bridge point of an instrument (SPEC-BRG "Polarisation coupling", SPEC-BODY "Local bridge
/// model", SPEC-INST). Provisional members: SPEC-API names the type only.
struct BridgePointDesc {
    Point position;   ///< Location on the body (m), SPEC-BODY. Frame: see `Point`.
    double thetaB{};  ///< Bridge frame rotation theta_b against the string polarisations (rad),
                      ///< SPEC-BRG.
    double conductanceH{};  ///< Horizontal-axis conductance G_h (s/kg), SPEC-BRG "Polarisation
                            ///< coupling".
};

/// Compile the bridge terms of one segment (SPEC-BRG "Contract", SPEC-API).
///
/// @param modes The segment's compiled modes.
/// @param end   Which end of the string is attached to the bridge.
/// @param out   Receives g_n, a_n and T/L on success.
/// Thread: non-audio. Allocates: yes. Failures: `InvalidArgument` if `modes` is empty or
/// inconsistent; `NotImplemented` in the P0.4 stub.
[[nodiscard]] Status compileBridgeTerms(const StringModes& modes, BridgeEnd end,
                                        SegmentBridgeTerms& out);

}  // namespace pme
