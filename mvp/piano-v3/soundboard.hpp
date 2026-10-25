// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include "soundboard_table.hpp"

namespace piano_mvp_v3 {
// Re(A*q'' phasor), evaluated near the mode's natural frequency. The minus sign
// follows from q''' ~= -omega^2*q' and the e^(+i*omega*t) convention.
inline double modalPressure(double re, double im, double omega,
                            double acceleration, double velocity) noexcept {
    return re*acceleration-im*omega*velocity;
}
// Force (N) at two nominal bridge points -> approximate radiated pressure (Pa).
// Linear, stable zero-order-hold modal dynamics; no direct audio bypass or saturation.
class Soundboard {
public:
    bool prepare(double fs) noexcept;
    void reset() noexcept;
    void sample(double forceA, double forceB, double& left, double& right) noexcept;
    double delaySamples() const noexcept { return delay_; }
private:
    struct Mode {
        double omega{}, sigma{}, c00{}, c01{}, c10{}, c11{}, q{}, v{};
        double driveA{}, driveB{}, reL{}, imL{}, reR{}, imR{};
    };
    std::array<Mode, kSoundboardModes.size()> modes_{};
    std::array<std::array<double, 1024>, 2> delayLine_{};
    std::size_t cursor_{};
    double delay_{};
    bool ready_{};
};
}
