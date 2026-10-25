// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <vector>
#include "grand_board_table.hpp"
#include "pme/osc/mode_bank.hpp"
#include "kernels.hpp"

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
    // Radiation state only (drive, modes, delay line): safe while the strings keep stepping.
    void resetRadiation() noexcept;
    void resetPort(std::size_t port) noexcept { lowpass_[port].v = 0; }
    // Per sample: addForce(F^n) for every active port, then advanceCoupled(); the strings advance
    // and then radiate(port, F^{n+1}, ...) for every port and radiation(L, R) closes the sample.
    // (0.5.0 fed the radiating modes the force after the string step; so does this.)
    void addForce(std::size_t port, double force) noexcept;
    void advanceCoupled() noexcept;
    // `modes` limits the radiating modes driven (unstruck strings carry nothing above ~2 kHz).
    void radiate(std::size_t port, double force, std::size_t modes = kBoardModeCount) noexcept;
    void radiation(double& pressureL, double& pressureR) noexcept;
    // 0.5.4 split of radiate()/radiation() for the worker pool; the arithmetic is the same.
    // Bridge-mass low pass of a port force (state per port): the drive amplitude g of radiate().
    double lowpassStep(std::size_t port, double force) noexcept {
        if (!mass_) return force;
        lowpass_[port].v += lowA_ * (force - lowpass_[port].v);
        return lowpass_[port].v;
    }
    // drive[lo,hi) += phi g: ports must be applied in a fixed order for a reproducible sum.
    void accumulateDrive(std::size_t port, double g, std::size_t lo, std::size_t hi) noexcept {
        kern::axpy(drive_.data() + lo, participation(port) + lo, g, hi - lo);
    }
    // Upper bound of the output amplitude and of the coupled-mode speed: true when both are below `eps`.
    bool quiet(double eps) const noexcept;
    double outputBound() const noexcept;      // the bound quiet() tests (output amplitude bound)
    double coupledBound() const noexcept;
    // Update radiating modes [lo,hi) (independent of each other) and store their pressure terms.
    void radiationModes(std::size_t lo, std::size_t hi) noexcept;
    // Sum the terms in index order and apply the radiation delay line (call after all chunks).
    void radiationFinish(double& pressureL, double& pressureR) noexcept;
    // After advanceCoupled(): w_s^{n+1}-w_s^n (coupled modes), the port displacement and velocity.
    double portIncrement(std::size_t port) const noexcept;
    // 0.5.4: eight-accumulator dot product for the port motion (not bit-identical to the four-accumulator reference).
    void setFast(bool f) noexcept { fast_ = f; }
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
    bool mass_{true}, fast_{false};
    std::vector<double> phi_;
    // Coupled modes (difference form).
    std::vector<double> beta_, kappa_, gamma_, omegaHat2_, r_, u_, force_;
    // Radiating modes (zero-order-hold, MVP 3 formulation).
    std::vector<double> termL_, termR_;
    std::vector<double> omega_, sigma_, c00_, c01_, c10_, c11_, q_, v_, drive_, reL_, imL_, reR_, imR_;
    struct alignas(64) PaddedDouble { double v{}; };   // per-port state on its own cache line (threads write ports)
    std::array<PaddedDouble, kBoardPortCount> lowpass_{};
    std::array<std::array<double, 1024>, 2> delayLine_{};
    std::size_t cursor_{};
    bool ready_{};
};
}
