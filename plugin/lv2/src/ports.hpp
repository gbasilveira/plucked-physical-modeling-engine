// SPDX-License-Identifier: MIT
#pragma once

/// @file ports.hpp
/// Port indices of the LV2 plug-in (D3-R2). They are a public contract: hosts save them in
/// sessions. The adapter and the Turtle generator both read this header, so they cannot drift.
///
/// Index policy: indices 0..3 are fixed. Control ports follow in the order of
/// `kControlPortParams`, which lists the continuous registry entries. A parameter added to the
/// registry later may only be APPENDED to that table (its port then takes the next free index);
/// existing entries never move, are never removed and never change class. The generator and the
/// D3-R2 tests fail when a continuous registry entry is missing from the table.

#include <array>
#include <cstddef>
#include <cstdint>

#include "pme/pme.hpp"

namespace pme::lv2 {

inline constexpr std::uint32_t kPortIn = 0;      ///< atom in: MIDI and patch messages
inline constexpr std::uint32_t kPortNotify = 1;  ///< atom out: patch messages
inline constexpr std::uint32_t kPortOutLeft = 2;
inline constexpr std::uint32_t kPortOutRight = 3;
inline constexpr std::uint32_t kFirstControlPort = 4;

/// Registry ids of the continuous parameters, in port order (port index = kFirstControlPort + i).
inline constexpr std::array<ParamId, 8> kControlPortParams{1001, 1002, 1003, 2001,
                                                           3001, 4001, 4002, 4003};

inline constexpr std::size_t kControlPortCount = kControlPortParams.size();
inline constexpr std::uint32_t kPortCount =
    kFirstControlPort + static_cast<std::uint32_t>(kControlPortCount);

}  // namespace pme::lv2
