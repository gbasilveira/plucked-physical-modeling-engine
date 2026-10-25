// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <vector>
#include "grand_board_table.hpp"
#include "pme/osc/mode_bank.hpp"

namespace piano_mvp_v4 {
inline constexpr std::size_t kBoardModeCount = 512;   // radiating modes (MVP 3 table)
inline constexpr std::size_t kBoardPortCount = 88;    // one bridge point per key
inline constexpr std::size_t kLightRadiatingModes = 176;  // modes below 2.6 kHz
// 0.5.1 geometry assumptions (plate frame of generate_soundboard.py, metres, x bass->treble,
// y keyboard->tail). Nominal and unmeasured; see README 0.5.1 "Sources and assumptions".
inline constexpr int kFirstBassBridgeKey = 21, kLastBassBridgeKey = 44, kLastKey = 108;
// Bridge point of a key (21..108): bass bridge for keys <= kLastBassBridgeKey, else the long
// treble bridge (quadratic Bezier). Always inside the plate with a margin of at least 0.15 m.
void bridgePoint(int key, double& x, double& y) noexcept;
// Mass-normalised shape of board mode i (row of the MVP 3 table) at (x, y).
double boardModeShape(std::size_t i, double x, double y) noexcept;
// Mass-normalised mode participation phi_i(key) for all 88 ports x 512 modes ([port*512+i]).
// grandGeometry: sin(m pi x/lx) sin(n pi y/ly) N at bridgePoint(key). Otherwise the MVP 3 /
// 0.5.0 two-column pan blend (1-pan)*A + pan*B, pan = 0.2 + 0.6 (key-21)/87.
void buildParticipation(bool grandGeometry, std::vector<double>& phi);

// One shared soundboard. The lowest kCoupledModes modes are mechanically coupled to every
// attached string port (same difference-form scheme and coefficients as pme::bridge::BridgePoint,
// generalised to many ports: the mode force is sum_s phi_s F_s and the port motion is
// w_s = sum_i phi_s,i r_i). All 512 modes radiate (zero-order-hold modal dynamics as MVP 3);
// modes above the coupled subset are driven one way and appear to the strings only as the
// passive residual loss. Radiation is driven by the bridge-mass low-passed port forces.
class GrandBoard {
public:
    // Non-audio. Allocates. `lossless` zeroes the coupled-mode damping (energy tests).
    bool configure(double fs, std::vector<double> phi, std::size_t coupledModes, bool lossless,
                   bool massLoading);
    void reset() noexcept;
    void resetPort(std::size_t port) noexcept { lowpass_[port] = 0; }
    // Per sample: addForce(F^n) for every active port, then advanceCoupled(); the strings advance
    // and then radiate(port, F^{n+1}, ...) for every port and radiation(L, R) closes the sample.
    // (0.5.0 fed the radiating modes the force after the string step; so does this.)
    void addForce(std::size_t port, double force) noexcept;
    void advanceCoupled() noexcept;
    // `modes` limits the radiating modes driven (unstruck strings carry nothing above ~2 kHz).
    void radiate(std::size_t port, double force, std::size_t modes = kBoardModeCount) noexcept;
    void radiation(double& pressureL, double& pressureR) noexcept;
    // After advanceCoupled(): w_s^{n+1}-w_s^n (coupled modes), the port displacement and velocity.
    double portIncrement(std::size_t port) const noexcept;
    double portDisplacement(std::size_t port) const noexcept;
    double portVelocity(std::size_t port) const noexcept { return portIncrement(port) * fs_; }
    // Discrete energy of the coupled modes (as BridgePoint::modeEnergy), unit modal mass.
    double coupledEnergy() const noexcept;
    const double* participation(std::size_t port) const noexcept { return phi_.data() + port * kBoardModeCount; }
    std::size_t coupledModes() const noexcept { return coupled_; }
    double dt() const noexcept { return 1.0 / fs_; }
private:
    std::size_t coupled_{};
    double fs_{48000}, delay_{}, lowA_{1};
    bool mass_{true};
    std::vector<double> phi_;
    // Coupled modes (difference form).
    std::vector<double> beta_, kappa_, gamma_, omegaHat2_, r_, u_, force_;
    // Radiating modes (zero-order-hold, MVP 3 formulation).
    std::vector<double> omega_, sigma_, c00_, c01_, c10_, c11_, q_, v_, drive_, reL_, imL_, reR_, imR_;
    std::array<double, kBoardPortCount> lowpass_{};
    std::array<std::array<double, 1024>, 2> delayLine_{};
    std::size_t cursor_{};
    bool ready_{};
};
}
