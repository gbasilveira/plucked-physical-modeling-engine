// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "grand_board_table.hpp"
#include "baked_importance.hpp"
#include "pme/osc/mode_bank.hpp"
#include "kernels.hpp"
#include "grand_live.hpp"

namespace piano_mvp_v4 {
inline constexpr std::size_t kBoardModeCount = 512;   // radiating modes (MVP 3 table)
inline constexpr std::size_t kBoardPortCount = 88;    // one bridge point per key
inline constexpr std::size_t kLightRadiatingModes = 176;  // modes below 2.6 kHz
inline constexpr double kBakedDefaultTolerance = 3e-9;      // dropped energy / total (0.6): about -85 dB rms
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
                   bool massLoading, double bakedTolerance = kBakedDefaultTolerance);
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
    // All ports of one sample in list order (`light` = unstruck string: only the modes below kLightRadiatingModes).
    // Modal bank: the same per-port axpy sequence as accumulateDrive (bit-exact). Baked bank: float drive, one
    // chunked pass (see kern::bankDrive).
    void accumulateDrives(const std::uint8_t* port, const double* g, const std::uint8_t* light, int count) noexcept {
        if (grand_) {   // 0.7c Body Grand: the 0.7b shared-pole bank
            std::array<float, kBoardPortCount> gf;
            for (int k = 0; k < count; ++k) gf[k] = static_cast<float>(g[k]);
            grand_->accumulate(port, gf.data(), light, count);
            return;
        }
        if (!baked_) {
            for (int k = 0; k < count; ++k) accumulateDrive(port[k], g[k], 0, light[k] ? kLightRadiatingModes : kBoardModeCount);
            return;
        }
        std::array<float, kBoardPortCount> gf;
        for (int k = 0; k < count; ++k) gf[k] = static_cast<float>(g[k]);
        kern::bankDrive(bank_.drive.data(), bank_.phiT.data(), port, gf.data(), light, count, bank_.chunks, bank_.lightChunks);
    }
    // ---- 0.6 baked radiation (README "Update 0.6") ----
    // The radiation stage is linear and time invariant in the port drives, so a reduced set of the 512 modes
    // reproduces it: the modes with zero radiation weight are dropped (exact), then the weakest by measured
    // output energy (kBakedImportance, from piano-baked-select) while the dropped energy stays below
    // `tolerance` of the total (relative energy, 1e-9 = -90 dB rms). Same arithmetic per mode as the modal
    // bank, so the error is only the dropped modes. Switching: the state of the bank being switched to is
    // cleared (a sounding tail of the other bank is lost); the delay line is kept.
    void setBaked(bool on) noexcept;
    bool baked() const noexcept { return baked_ || grand_ != nullptr; }
    // 0.7c Body Grand: radiation by the bank of grand_live.hpp (the 0.6 banks and the delay line are unused). Non-audio.
    void setGrand(GrandRadiation* g) noexcept { grand_ = g; }
    bool grand() const noexcept { return grand_ != nullptr; }
    // The direct string radiation of the sample (values of the slot microphones), consumed by the next radiationFinish().
    void setDirect(const float* d) noexcept { for (int i = 0; i < 4; ++i) direct_[i] = d ? d[i] : 0.f; }
    std::size_t bakedModes() const noexcept { return bank_.kept; }          // radiating modes kept
    std::size_t bakedPaddedModes() const noexcept { return bank_.count; }   // incl. zero padding to whole 8-mode chunks
    std::size_t bakedLightModes() const noexcept { return bank_.light; }
    // Kept original mode indices (ascending).
    const std::vector<std::uint16_t>& bakedIndex() const noexcept { return bank_.index; }
    // Per-mode pressure terms of the last radiationModes() (modal bank; tools).
    const double* termsL() const noexcept { return termL_.data(); }
    const double* termsR() const noexcept { return termR_.data(); }
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
    // 0.6 reduced radiation bank (same recursion, kept modes only; phi rows compacted per port).
    struct Bank {
        std::size_t count{}, kept{}, light{}, chunks{}, lightChunks{};   // count = chunks * 8 (padded)
        std::vector<std::uint16_t> index;                                 // original mode of each kept slot (0xffff: padding)
        std::vector<double> omega, sigma, c00, c01, c10, c11, reL, imL, reR, imR, q, v, termL, termR;
        std::vector<float> drive, phiT;                                   // phiT[(chunk * 88 + port) * 8 + lane]
    } bank_;
    bool baked_{};
    GrandRadiation* grand_{};
    std::array<float, 4> direct_{};
    void buildBank(double tolerance);
    struct alignas(64) PaddedDouble { double v{}; };   // per-port state on its own cache line (threads write ports)
    std::array<PaddedDouble, kBoardPortCount> lowpass_{};
    std::array<std::array<double, 1024>, 2> delayLine_{};
    std::size_t cursor_{};
    bool ready_{};
};
}
