// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <numbers>
namespace piano_mvp_v4 {
// Series bridge-top mass m on a board point with real mobility Y: the board receives
// F/(1 + i*omega*m*Y), a first-order low pass with corner 1/(2*pi*m*Y).
// Y is the infinite-plate value 1/(8*sqrt(sqrt(Dx*Dy)*mu)) for the MVP 3 board
// (Dx 500 N m, Dy 4457 N m, mu 4.86 kg/m^2 -> 1.47e-3 m/(N s)). The 55 g effective
// bridge mass is an assumption, not a measurement (README 0.4.9).
inline constexpr double kBoardPointMobility = 1.47e-3;
inline constexpr double kBridgeEffectiveMass = 0.055;
inline constexpr double kBridgeMassCornerHz =
    1.0 / (2 * std::numbers::pi * kBridgeEffectiveMass * kBoardPointMobility);
struct BridgeMassTransmission {
    void prepare(double fs) noexcept { a_ = 1 - std::exp(-2 * std::numbers::pi * kBridgeMassCornerHz / fs); reset(); }
    void reset() noexcept { s_[0] = s_[1] = 0; }
    void process(double& l, double& r) noexcept {
        s_[0] += a_ * (l - s_[0]); s_[1] += a_ * (r - s_[1]);
        l = s_[0]; r = s_[1];
    }
private:
    double a_{1}, s_[2]{};
};
}
