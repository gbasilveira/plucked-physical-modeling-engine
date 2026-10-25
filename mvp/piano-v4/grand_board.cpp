// SPDX-License-Identifier: MIT
#include "grand_board.hpp"
#include "bridge_mass.hpp"
#include "soundboard_table.hpp"
#include "pme/bridge/model.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace piano_mvp_v4 {
void bridgePoint(int key, double& x, double& y) noexcept {
    key = std::clamp(key, kFirstBassBridgeKey, kLastKey);
    if (key <= kLastBassBridgeKey) {   // bass bridge: straight segment, assumed endpoints
        const double t = (key - kFirstBassBridgeKey) / double(kLastBassBridgeKey - kFirstBassBridgeKey);
        x = 0.30 + t * (0.55 - 0.30); y = 1.15 + t * (0.82 - 1.15);
        return;
    }
    // Long (treble) bridge, quadratic Bezier P0 -> P2 with control P1; keys evenly spaced in t.
    const double t = (key - (kLastBassBridgeKey + 1)) / double(kLastKey - (kLastBassBridgeKey + 1));
    const double a = (1 - t) * (1 - t), b = 2 * t * (1 - t), c = t * t;
    x = a * 0.62 + b * 0.95 + c * 1.25; y = a * 0.90 + b * 0.78 + c * 0.25;
}
double boardModeShape(std::size_t i, double x, double y) noexcept {
    const double normal = 2 / std::sqrt(kBoardMu * kBoardLx * kBoardLy);
    return normal * std::sin(kBoardModeIndex[i][0] * std::numbers::pi * x / kBoardLx) *
                    std::sin(kBoardModeIndex[i][1] * std::numbers::pi * y / kBoardLy);
}
void buildParticipation(bool grandGeometry, std::vector<double>& phi) {
    phi.assign(kBoardPortCount * kBoardModeCount, 0.0);
    for (std::size_t port = 0; port < kBoardPortCount; ++port) {
        double* row = phi.data() + port * kBoardModeCount;
        if (grandGeometry) {
            double x, y; bridgePoint(static_cast<int>(port) + 21, x, y);
            for (std::size_t i = 0; i < kBoardModeCount; ++i) row[i] = boardModeShape(i, x, y);
        } else {
            const double pan = 0.2 + 0.6 * double(port) / 87.0;
            for (std::size_t i = 0; i < kBoardModeCount; ++i)
                row[i] = (1 - pan) * piano_mvp_v3::kSoundboardModes[i][2] + pan * piano_mvp_v3::kSoundboardModes[i][3];
        }
    }
}
bool GrandBoard::configure(double fs, std::vector<double> phi, std::size_t coupled, bool lossless,
                           bool massLoading) {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000 || phi.size() != kBoardPortCount * kBoardModeCount ||
        coupled > kBoardModeCount) return false;
    fs_ = fs; coupled_ = coupled; phi_ = std::move(phi); mass_ = massLoading;
    lowA_ = 1 - std::exp(-2 * std::numbers::pi * kBridgeMassCornerHz / fs);
    const auto n = kBoardModeCount;
    beta_.assign(coupled, 0); kappa_ = gamma_ = omegaHat2_ = r_ = u_ = force_ = beta_;
    for (std::size_t i = 0; i < coupled; ++i) {
        const auto& row = piano_mvp_v3::kSoundboardModes[i];
        pme::osc::OscCoeffsF64 c;
        if (pme::bridge::compileBridgeMode(row[0], lossless ? 0.0 : row[1], fs, c) != pme::Status::Ok) return false;
        beta_[i] = c.beta; kappa_[i] = c.kappa; gamma_[i] = c.gamma; omegaHat2_[i] = c.omegaHat2;
    }
    termL_.assign(n, 0.0); termR_.assign(n, 0.0);
    omega_.assign(n, 0); sigma_ = c00_ = c01_ = c10_ = c11_ = q_ = v_ = drive_ = reL_ = imL_ = reR_ = imR_ = omega_;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = piano_mvp_v3::kSoundboardModes[i];
        omega_[i] = r[0]; sigma_[i] = r[1];
        if (r[0] >= 0.95 * std::numbers::pi * fs) continue;   // as piano_mvp_v3::Soundboard
        const double wd = std::sqrt(r[0] * r[0] - r[1] * r[1]);
        const double decay = std::exp(-r[1] / fs);
        const double s = std::sin(wd / fs) / wd, c = std::cos(wd / fs);
        c00_[i] = decay * (c + r[1] * s); c01_[i] = decay * s;
        c10_[i] = -decay * r[0] * r[0] * s; c11_[i] = decay * (c - r[1] * s);
        reL_[i] = r[4]; imL_[i] = r[5]; reR_[i] = r[6]; imR_[i] = r[7];
    }
    delay_ = fs * piano_mvp_v3::kRadiationDelaySeconds;
    ready_ = true; reset(); return true;
}
void GrandBoard::reset() noexcept {
    std::fill(r_.begin(), r_.end(), 0.0); std::fill(u_.begin(), u_.end(), 0.0);
    std::fill(force_.begin(), force_.end(), 0.0); std::fill(drive_.begin(), drive_.end(), 0.0);
    std::fill(q_.begin(), q_.end(), 0.0); std::fill(v_.begin(), v_.end(), 0.0);
    for (auto& l : lowpass_) l.v = 0;
    for (auto& ch : delayLine_) ch.fill(0);
    cursor_ = 0;
}
void GrandBoard::resetRadiation() noexcept {
    std::fill(drive_.begin(), drive_.end(), 0.0); std::fill(q_.begin(), q_.end(), 0.0);
    std::fill(v_.begin(), v_.end(), 0.0);
    for (auto& ch : delayLine_) ch.fill(0);
    cursor_ = 0;
}
void GrandBoard::addForce(std::size_t port, double f) noexcept {
    kern::axpy(force_.data(), participation(port), f, coupled_);   // coupled: raw force
}
void GrandBoard::radiate(std::size_t port, double f, std::size_t modes) noexcept {
    const double* phi = participation(port);
    double g = f;
    if (mass_) { lowpass_[port].v += lowA_ * (f - lowpass_[port].v); g = lowpass_[port].v; }  // 0.4.9 bridge mass
    kern::axpy(drive_.data(), phi, g, modes);
}
void GrandBoard::advanceCoupled() noexcept {
    // SPEC-OSC difference form, explicit in the force at step n (as BridgePoint::advance).
    kern::modeStepClearForce(r_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(),
                             force_.data(), coupled_);
    std::fill(force_.begin() + static_cast<std::ptrdiff_t>(coupled_), force_.end(), 0.0);
}
double GrandBoard::outputBound() const noexcept {
    double out = 0;
    for (std::size_t i = 0; i < kBoardModeCount; ++i) {
        const double a = omega_[i] * omega_[i] * std::abs(q_[i]) + 2 * sigma_[i] * std::abs(v_[i]) + std::abs(drive_[i]);
        const double w = omega_[i] * std::abs(v_[i]);
        out += (std::abs(reL_[i]) + std::abs(reR_[i])) * a + (std::abs(imL_[i]) + std::abs(imR_[i])) * w;
    }
    for (const auto& ch : delayLine_) for (double x : ch) out = std::max(out, std::abs(x));
    return out;
}
double GrandBoard::coupledBound() const noexcept {
    double coupled = 0;
    for (std::size_t i = 0; i < coupled_; ++i) coupled += std::abs(u_[i]) * fs_ + std::abs(force_[i]);
    return coupled;
}
bool GrandBoard::quiet(double eps) const noexcept { return !ready_ || (outputBound() < eps && coupledBound() < eps); }
void GrandBoard::radiationModes(std::size_t lo, std::size_t hi) noexcept {
    kern::RadiationArrays a{omega_.data(), sigma_.data(), c00_.data(), c01_.data(), c10_.data(), c11_.data(),
                            reL_.data(), imL_.data(), reR_.data(), imR_.data(),
                            q_.data(), v_.data(), drive_.data(), termL_.data(), termR_.data()};
    kern::radiationModes(a, lo, hi);
}
void GrandBoard::radiationFinish(double& left, double& right) noexcept {
    left = right = 0;
    for (std::size_t i = 0; i < kBoardModeCount; ++i) { left += termL_[i]; right += termR_[i]; }
    delayLine_[0][cursor_] = left; delayLine_[1][cursor_] = right;
    const auto whole = static_cast<std::size_t>(delay_);
    const double fraction = delay_ - static_cast<double>(whole);
    const auto a = (cursor_ + delayLine_[0].size() - whole) % delayLine_[0].size();
    const auto b = (a + delayLine_[0].size() - 1) % delayLine_[0].size();
    left = (1 - fraction) * delayLine_[0][a] + fraction * delayLine_[0][b];
    right = (1 - fraction) * delayLine_[1][a] + fraction * delayLine_[1][b];
    cursor_ = (cursor_ + 1) % delayLine_[0].size();
}
void GrandBoard::radiation(double& left, double& right) noexcept {
    left = right = 0;
    if (!ready_) return;
    radiationModes(0, kBoardModeCount);
    radiationFinish(left, right);
}
static double dot4(const double* a, const double* b, std::size_t n) noexcept { return kern::dot4(a, b, n); }
double GrandBoard::portIncrement(std::size_t port) const noexcept {
    if (fast_) return kern::dotFast(participation(port), u_.data(), coupled_);
    return dot4(participation(port), u_.data(), coupled_);
}
double GrandBoard::portDisplacement(std::size_t port) const noexcept {
    return dot4(participation(port), r_.data(), coupled_);
}
double GrandBoard::coupledEnergy() const noexcept {
    double e = 0;
    for (std::size_t i = 0; i < coupled_; ++i) {
        const double v = u_[i] * fs_;
        e += 0.5 * v * v + 0.5 * omegaHat2_[i] * (r_[i] - u_[i]) * r_[i];
    }
    return e;
}
}
