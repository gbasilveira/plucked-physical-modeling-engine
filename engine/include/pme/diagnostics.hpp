// SPDX-License-Identifier: MIT
#pragma once

/// @file diagnostics.hpp
/// Lock-free diagnostics from the audio thread (SPEC-API "Diagnostics").

#include <cstdint>

namespace pme {

/// What a diagnostic reports (SPEC-API "Diagnostics").
enum class DiagnosticKind : std::uint8_t {
    GroupEnergy,     ///< Per-group energy in joules (SPEC-OSC "Discrete energy").
    ActiveContacts,  ///< Active contact count of a group.
    KappaSHalved,    ///< The sympathetic gain guard halved kappa_s (SPEC-BODY "Sympathetic path").
    SavReset,        ///< A SAV reset-at-separation fired (SPEC-NL "Drift control").
};

/// One diagnostic record. Trivially copyable so it can cross a lock-free ring buffer.
/// Provisional layout: SPEC-API lists the quantities, not the record.
struct Diagnostic {
    DiagnosticKind kind{DiagnosticKind::GroupEnergy};  ///< What is reported.
    std::int32_t group{};   ///< Note group (key) index the record refers to.
    std::int64_t sample{};  ///< Engine sample counter at the sub-block boundary that produced it.
    double value{};         ///< Energy (J), contact count, or new kappa_s, per `kind`.
};

}  // namespace pme
