// SPDX-License-Identifier: MIT
#pragma once

/// @file events.hpp
/// Events delivered to `Engine::process` (SPEC-API "Events"; SPEC-EXEC step 2.1).
///
/// All event types are trivially copyable so they can cross the lock-free single-producer
/// single-consumer queues between host and audio thread.

#include <cstdint>
#include <span>
#include <variant>

#include "pme/params.hpp"

namespace pme {

/// Launch or re-launch the group's exciter; key damper lift (SPEC-API "Events", SPEC-HAM).
struct NoteOn {
    std::int32_t key{};  ///< Key number of the note group.
    float velocity{};    ///< Normalised key velocity nu in (0, 1] (SPEC-HAM "Velocity map").
};

/// Key damper landing with tau_down from the release velocity (SPEC-API "Events", SPEC-DMP).
struct NoteOff {
    std::int32_t key{};       ///< Key number.
    float releaseVelocity{};  ///< Release velocity nu_rel in [0, 1] (SPEC-DMP "Timing").
};

/// Continuous key depth for half-key damper control (SPEC-API "Events", SPEC-DMP).
struct KeyDepth {
    std::int32_t key{};  ///< Key number.
    float depth{};       ///< l_key in [0, 1].
};

/// Which pedal a `Pedal` event addresses (SPEC-API "Events").
enum class PedalKind : std::uint8_t {
    Sustain,    ///< Damper lift (SPEC-DMP "Lift fraction").
    Sostenuto,  ///< Latch dampers lifted by their keys (SPEC-DMP "Sostenuto").
    Soft,       ///< Una corda shift (grand) or upright soft pedal (SPEC-HAM).
};

/// Pedal movement (SPEC-API "Events").
struct Pedal {
    PedalKind which{PedalKind::Sustain};  ///< Which pedal.
    float value{};                        ///< Pedal depth in [0, 1].
};

/// Kind of plucked exciter a `Pluck` event starts (SPEC-API "Events", SPEC-PLK "Exciters").
enum class PluckKind : std::uint8_t {
    ForcePulse,  ///< Generic force pulse (PluckPulse).
    Plectrum,    ///< Plectrum, pick, quill, mizrab or bachi.
    Finger,      ///< Finger.
};

/// Start a plucked exciter on a segment (SPEC-API "Events", SPEC-PLK).
struct Pluck {
    std::int32_t segment{};  ///< Segment index (flattened across note groups in description order).
    double position{};       ///< Pluck position x_p along the string (m).
    double force{};          ///< Peak force F_p (N) of a pulse, or driver force scale (N).
    double angle{};          ///< Polarisation angle theta_p (rad).
    PluckKind kind{PluckKind::ForcePulse};  ///< Exciter model to start.
};

/// Time-varying tension (SPEC-API "Events", SPEC-PLK "Bending and meend").
struct Bend {
    std::int32_t segment{};  ///< Segment index, as for `Pluck`.
    double cents{};          ///< Bend c in cents; T(t) = T_0 2^(c / 600).
};

/// Continuous parameter ramp, or recompile request for a structural one (SPEC-API "Events").
struct Param {
    ParamId id{};    ///< Registry id (params.hpp).
    double value{};  ///< New value in the unit of the registry entry.
};

/// One event with its sample offset inside the host block (SPEC-API "Events": each carries a
/// sample offset within the block).
struct Event {
    std::int32_t offset{};  ///< Sample offset within the host block, >= 0 and < frames.
    std::variant<NoteOn, NoteOff, KeyDepth, Pedal, Pluck, Bend, Param> payload;  ///< The event.
};

/// Events of one host block, sorted by non-decreasing `offset` (precondition of
/// `Engine::process`, SPEC-EXEC step 2.1). The span does not own the events.
using EventList = std::span<const Event>;

}  // namespace pme
