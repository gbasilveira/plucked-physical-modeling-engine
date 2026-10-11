// SPDX-License-Identifier: MIT
#pragma once

/// @file version.hpp
/// Interface version and compatibility statement (P0.4-R6).
///
/// Compatibility policy (interfaces-v1): source compatibility holds within a major version;
/// ABI is not promised. Every change to a header in this directory after the `interfaces-v1`
/// tag goes through the interface change request process (docs/icr/README.md). See
/// docs/dev/interfaces.md for what counts as a breaking change.

namespace pme {

/// Major version of the public interfaces. Bumped only by an approved ICR that breaks source
/// compatibility.
inline constexpr int interfacesVersionMajor = 1;

}  // namespace pme
