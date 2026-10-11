// SPDX-License-Identifier: MIT
#pragma once

/// @file param_model.hpp
/// Parameter model built from `pme::paramRegistry()` (D1-R3). One entry per registry entry; the
/// current values are atomics so that the audio thread (host automation) and the main thread
/// (get_value, state) can both touch them without a lock.

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include "pme/pme.hpp"

namespace pme::plugin {

class ParamModel {
public:
    /// Non-audio thread. Allocates.
    ParamModel();

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    /// Registry entry of the `index`-th parameter (registry order, ascending id).
    [[nodiscard]] const ParamInfo& info(std::size_t index) const noexcept;
    /// Index of `id`, or `count()` when the id is unused. Real-time safe.
    [[nodiscard]] std::size_t indexOf(ParamId id) const noexcept;

    /// Current value. Real-time safe.
    [[nodiscard]] double value(std::size_t index) const noexcept;
    /// Store a value, clamped to [min, max]; NaN is ignored. Real-time safe. Returns the stored
    /// value.
    double setValue(std::size_t index, double v) noexcept;
    /// Set every parameter to its registry default.
    void resetToDefaults() noexcept;

    /// Clamp to [min, max] (D1-R3); NaN becomes the default.
    [[nodiscard]] static double clamp(const ParamInfo& info, double v) noexcept;
    /// Text for the value: the number with 9 significant digits and the unit from the registry
    /// ("1" is dimensionless and shows no unit). Returns the length written (without the NUL);
    /// the output is always NUL-terminated when `capacity` > 0.
    static std::size_t valueToText(const ParamInfo& info, double v, char* out,
                                   std::size_t capacity) noexcept;
    /// Parse a number, optionally followed by the unit. Returns false if there is no number.
    [[nodiscard]] static bool textToValue(const ParamInfo& info, const char* text,
                                          double& out) noexcept;
    /// CLAP-style module path from the dotted name: "damper.tauUp" gives "damper"; structural
    /// parameters get the prefix "structural/" (D1-R3: marks "requires recompilation").
    [[nodiscard]] static std::string modulePath(const ParamInfo& info);

private:
    std::span<const ParamInfo> registry_;
    std::size_t count_{0};
    std::unique_ptr<std::atomic<double>[]> values_;
};

}  // namespace pme::plugin
