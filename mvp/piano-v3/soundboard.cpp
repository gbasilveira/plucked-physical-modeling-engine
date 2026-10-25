// SPDX-License-Identifier: MIT
#include "soundboard.hpp"
#include <cmath>
#include <numbers>

namespace piano_mvp_v3 {
bool Soundboard::prepare(double fs) noexcept {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000) return false;
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        const auto& r = kSoundboardModes[i];
        auto& m = modes_[i];
        m = {};
        m.omega = r[0]; m.sigma = r[1];
        if (m.omega >= 0.95 * std::numbers::pi * fs) continue;
        const double wd = std::sqrt(m.omega*m.omega - m.sigma*m.sigma);
        const double decay = std::exp(-m.sigma/fs);
        const double s = std::sin(wd/fs)/wd, c = std::cos(wd/fs);
        m.c00 = decay*(c+m.sigma*s); m.c01 = decay*s;
        m.c10 = -decay*m.omega*m.omega*s; m.c11 = decay*(c-m.sigma*s);
        m.driveA = r[2]; m.driveB = r[3];
        m.reL = r[4]; m.imL = r[5]; m.reR = r[6]; m.imR = r[7];
    }
    delay_ = fs*kRadiationDelaySeconds;
    ready_ = true; reset(); return true;
}
void Soundboard::reset() noexcept {
    for (auto& m : modes_) m.q = m.v = 0;
    for (auto& ch : delayLine_) ch.fill(0);
    cursor_ = 0;
}
void Soundboard::sample(double forceA, double forceB, double& left, double& right) noexcept {
    left = right = 0;
    if (!ready_) return;
    for (auto& m : modes_) {
        const double forcing = m.driveA*forceA + m.driveB*forceB;
        const double equilibrium = forcing/(m.omega*m.omega);
        const double shifted = m.q-equilibrium;
        const double q = m.c00*shifted+m.c01*m.v+equilibrium;
        const double v = m.c10*shifted+m.c11*m.v;
        m.q = q; m.v = v;
        const double acceleration = forcing-2*m.sigma*v-m.omega*m.omega*q;
        // Narrowband complex radiation weight, with the e^(+i*omega*t) phase convention.
        left += modalPressure(m.reL,m.imL,m.omega,acceleration,v);
        right += modalPressure(m.reR,m.imR,m.omega,acceleration,v);
    }
    delayLine_[0][cursor_] = left; delayLine_[1][cursor_] = right;
    const auto whole = static_cast<std::size_t>(delay_);
    const double fraction = delay_-static_cast<double>(whole);
    const auto a = (cursor_+delayLine_[0].size()-whole)%delayLine_[0].size();
    const auto b = (a+delayLine_[0].size()-1)%delayLine_[0].size();
    left = (1-fraction)*delayLine_[0][a]+fraction*delayLine_[0][b];
    right = (1-fraction)*delayLine_[1][a]+fraction*delayLine_[1][b];
    cursor_ = (cursor_+1)%delayLine_[0].size();
}
}
