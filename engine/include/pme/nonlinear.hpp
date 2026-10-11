// SPDX-License-Identifier: MIT
#pragma once

/// @file nonlinear.hpp
/// Nonlinear framework (SPEC-NL).
///
/// interfaces-v1 declares no nonlinear API. SPEC-API's descriptions and compile functions do
/// not mention any SPEC-NL type: the SAV state, potentials and dashpots (`Potential`,
/// `Dashpot`, `savStep`, SPEC-NL "Contracts") are internal to the engine, are consumed only by
/// the note-group runtime, and are declared privately by phase A5. Nonlinear behaviour is
/// configured through the parameters of other headers: hammer felt law (exciters.hpp), barriers
/// (barriers.hpp), tension modulation (`SegmentDesc::tensionModulation`, instrument.hpp).

namespace pme {

/// Version marker: true while interfaces-v1 declares no nonlinear API (SPEC-NL).
inline constexpr bool nonlinearApiDeclared = false;

}  // namespace pme
