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
    lowpass_.fill(0);
    for (auto& ch : delayLine_) ch.fill(0);
    cursor_ = 0;
}
void GrandBoard::addForce(std::size_t port, double f) noexcept {
    const double* phi = participation(port);
    double* force = force_.data();
    for (std::size_t i = 0; i < coupled_; ++i) force[i] += phi[i] * f;   // coupled: raw force
}
void GrandBoard::radiate(std::size_t port, double f, std::size_t modes) noexcept {
    const double* phi = participation(port);
    double g = f;
    if (mass_) { lowpass_[port] += lowA_ * (f - lowpass_[port]); g = lowpass_[port]; }  // 0.4.9 bridge mass
    double* drive = drive_.data();
    for (std::size_t i = 0; i < modes; ++i) drive[i] += phi[i] * g;
}
void GrandBoard::advanceCoupled() noexcept {
    // SPEC-OSC difference form, explicit in the force at step n (as BridgePoint::advance).
    pme::osc::stepModes<double, double>(r_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(),
                                        force_.data(), coupled_);
    std::fill(force_.begin(), force_.end(), 0.0);
}
void GrandBoard::radiation(double& left, double& right) noexcept {
    left = right = 0;
    if (!ready_) return;
    for (std::size_t i = 0; i < kBoardModeCount; ++i) {
        const double forcing = drive_[i];
        const double equilibrium = forcing / (omega_[i] * omega_[i]);
        const double shifted = q_[i] - equilibrium;
        const double q = c00_[i] * shifted + c01_[i] * v_[i] + equilibrium;
        const double v = c10_[i] * shifted + c11_[i] * v_[i];
        q_[i] = q; v_[i] = v;
        const double acceleration = forcing - 2 * sigma_[i] * v - omega_[i] * omega_[i] * q;
        // Re(A*q'' phasor) as piano_mvp_v3::modalPressure.
        left += reL_[i] * acceleration - imL_[i] * omega_[i] * v;
        right += reR_[i] * acceleration - imR_[i] * omega_[i] * v;
        drive_[i] = 0;
    }
    delayLine_[0][cursor_] = left; delayLine_[1][cursor_] = right;
    const auto whole = static_cast<std::size_t>(delay_);
    const double fraction = delay_ - static_cast<double>(whole);
    const auto a = (cursor_ + delayLine_[0].size() - whole) % delayLine_[0].size();
    const auto b = (a + delayLine_[0].size() - 1) % delayLine_[0].size();
    left = (1 - fraction) * delayLine_[0][a] + fraction * delayLine_[0][b];
    right = (1 - fraction) * delayLine_[1][a] + fraction * delayLine_[1][b];
    cursor_ = (cursor_ + 1) % delayLine_[0].size();
}
// Dot product with four independent accumulators in a fixed order ((a0+a1)+(a2+a3), then the
// remainder in index order): deterministic, and not latency bound like a single running sum.
static double dot4(const double* a, const double* b, std::size_t n) noexcept {
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i]; s1 += a[i+1] * b[i+1]; s2 += a[i+2] * b[i+2]; s3 += a[i+3] * b[i+3];
    }
    double s = (s0 + s1) + (s2 + s3);
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}
double GrandBoard::portIncrement(std::size_t port) const noexcept {
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
