// SPDX-License-Identifier: MIT
#pragma once

/// @file instrument.hpp
/// Instrument description and compilation (SPEC-INST; SPEC-API).

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pme/barriers.hpp"
#include "pme/body.hpp"
#include "pme/bridge.hpp"
#include "pme/dampers.hpp"
#include "pme/exciters.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme {

struct EngineConfig;  // engine.hpp

/// One string segment of a note group (SPEC-API `SegmentDesc`; SPEC-INST, SPEC-STR, SPEC-NL).
struct SegmentDesc {
    StringParams string;         ///< The string (SPEC-STR).
    int bridgePoint{};           ///< Index into InstrumentDesc::bridgePoints (SPEC-BRG).
    double polarisationAngle{};  ///< Angle of the vertical polarisation axis (rad), SPEC-BRG.
    std::vector<BarrierParams> barriers;  ///< Barriers this segment collides with (SPEC-PLK).
    std::optional<DamperParams> damper;   ///< Damper, if any (SPEC-DMP).
    double muteRatio{};  ///< Rear-segment mute m_d in [0, 1] (SPEC-STR "Duplex and rear segments").
    bool tensionModulation{};  ///< Enable Kirchhoff-Carrier tension modulation (SPEC-NL, SPEC-GEO).
    int longitudinalModes{8};  ///< Longitudinal modes M_L; default 8 (SPEC-INST piano defaults).
    double kappaL{0.026};  ///< Longitudinal transmission kappa_L (dimensionless); about sin 1.5 deg
                           ///< (SPEC-GEO).
    double kappaT{};       ///< Tension-modulation transmission kappa_T (dimensionless) (SPEC-GEO).
};

/// One key's strings, exciter and una corda map (SPEC-API `NoteGroupDesc`).
struct NoteGroupDesc {
    int key{};                          ///< Key number (see TuningCurve for the numbering).
    std::vector<SegmentDesc> segments;  ///< Speaking, rear and duplex segments.
    std::optional<ExciterParams>
        exciter;              ///< Exciter; absent for sympathetic-only groups (SPEC-PLK).
    UnaCordaParams unaCorda;  ///< Una corda and soft-pedal map (SPEC-HAM).
};

/// Pitch map (SPEC-STR "Tension for pitch": equal temperament plus per-key stretch plus unison
/// detune). Provisional members: SPEC-API names the type only.
struct TuningCurve {
    double referencePitch{440.0};  ///< Frequency of MIDI key 69 (A4) in Hz; default 440 (SPEC-STR).
    std::vector<double>
        stretchCents;  ///< Per-key stretch in cents, indexed by MIDI key number
                       ///< (key 0 first); keys beyond the vector have 0. Provisional.
};

/// Instrument-wide default values (SPEC-INST "Piano defaults"). Provisional composition:
/// SPEC-API names the type only; members are the instrument-wide values the table gives.
struct Defaults {
    double halfPedalP0{0.35};           ///< Half-pedal region start P_0 (dimensionless) (SPEC-DMP).
    double halfPedalP1{0.65};           ///< Half-pedal region end P_1 (dimensionless) (SPEC-DMP).
    double sympatheticGain{1.0};        ///< kappa_s in [0, 1] (SPEC-BODY "Sympathetic path").
    double sympatheticLowPass{5000.0};  ///< f_symp (Hz) (SPEC-BODY).
    int localBridgeModes{64};           ///< M_b (SPEC-BODY, SPEC-INST).
    double localBridgeFSplit{
        2000.0};  ///< f_split (Hz) for buildLocalBridge. Provisional: no SPEC default.
};

/// Complete instrument description (SPEC-API `InstrumentDesc`). Data, not code (SPEC-INST).
struct InstrumentDesc {
    std::string name;                           ///< Instrument name (no trademarks).
    std::vector<NoteGroupDesc> groups;          ///< Note groups.
    std::vector<BridgePointDesc> bridgePoints;  ///< Bridge points (SPEC-BRG).
    BodyParams body;                            ///< Body (SPEC-BODY).
    std::vector<ChannelDesc> channels;          ///< Radiation output channels (SPEC-BODY).
    TuningCurve tuning;                         ///< Pitch map (SPEC-STR).
    Defaults defaults;                          ///< Instrument-wide defaults (SPEC-INST).
};

/// Immutable compiled instrument (SPEC-API). Opaque: declared here, defined privately by the
/// runtime module and never in a public header. Create with `compileInstrument`.
class CompiledInstrument;

/// Compile an instrument for an engine configuration (SPEC-API "Compile functions"). Compiled
/// data is immutable once built; recompiling one string does not rebuild the body.
///
/// @param desc   The instrument description.
/// @param config The engine configuration (sample rate, f_max of strings, ...).
/// @param body   The compiled body (from `compileBody`).
/// @param out    Receives the compiled instrument on success.
/// Thread: non-audio. Allocates: yes. Failures: `InvalidArgument` for an inconsistent
/// description (for example a bridge point index out of range); `Rejected` if a SPEC rule
/// refuses a mode set; `NotImplemented` in the P0.4 stub.
[[nodiscard]] Status compileInstrument(const InstrumentDesc& desc, const EngineConfig& config,
                                       const BodyModes& body,
                                       std::shared_ptr<const CompiledInstrument>& out);

}  // namespace pme
