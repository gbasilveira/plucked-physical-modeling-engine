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
    std::int32_t key{};  ///< MIDI note number 0..127 (A4 = 69) of the target group (SPEC-STR
                         ///< "Tension for pitch", SI-0004). No group with this key: ignored.
    float velocity{};    ///< Normalised key velocity nu in (0, 1] (SPEC-HAM "Velocity map").
};

/// Key damper landing with tau_down from the release velocity (SPEC-API "Events", SPEC-DMP).
struct NoteOff {
    std::int32_t key{};       ///< MIDI note number 0..127 (A4 = 69), as for `NoteOn`.
    float releaseVelocity{};  ///< Release velocity nu_rel in [0, 1] (SPEC-DMP "Timing").
};

/// Continuous key depth for half-key damper control (SPEC-API "Events", SPEC-DMP).
struct KeyDepth {
    std::int32_t key{};  ///< MIDI note number 0..127 (A4 = 69), as for `NoteOn`.
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

// PluckKind removed (ICR-0001, SI-0005): the group's ExciterParams fixes the model.

/// Start the group's plucked exciter on one segment (SPEC-API "Events", SPEC-PLK "Exciters").
/// The model and its parameters come from the group's ExciterParams; the event chooses only
/// where and how hard (SI-0005).
struct Pluck {
    std::int32_t segment{};  ///< Flat segment index: 0.. across groups in InstrumentDesc order,
                             ///< then within each group in `segments` order (SPEC-API, SI-0004).
                             ///< Out of range, or a group without a plucked exciter: ignored.
    double position{};       ///< x_p (m) on the segment's own x axis (as compileString's
                             ///< registered points); 0 means the description's x_p. Outside
                             ///< [0, L): event ignored (SPEC-PLK).
    double angle{};          ///< Polarisation angle theta_p (rad); replaces the description's.
    float strength{};        ///< Normalised strength nu in (0, 1]; <= 0 ignores the event, > 1
                             ///< is treated as 1 (SPEC-PLK "Strength map").
};

/// Time-varying tension (SPEC-API "Events", SPEC-PLK "Bending and meend").
struct Bend {
    std::int32_t segment{};  ///< Flat segment index, as for `Pluck`; out of range: ignored.
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
