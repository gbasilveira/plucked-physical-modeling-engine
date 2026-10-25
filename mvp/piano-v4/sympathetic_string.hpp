// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "pme/bridge/terms.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/string.hpp"
#include "kernels.hpp"

namespace piano_mvp_v4 {
// Unstruck string group on the shared board: the vertical modes below `cutHz` of up to three
// strings, no hammer (0.5.1). Same equations as pme::bridge::ModalSegment (SPEC-BRG Bridge force
// and Modal equation with a moving support; SPEC-OSC difference form), vertical polarisation only:
// the horizontal modes of a point with no perpendicular conductance are never driven. Modes above
// the board's coupled range are not driven by the board either, so they are dropped here (a
// truncation of a passive system is passive). The group is configured from the voice's own
// compiled StringModes, so tuning and losses match the struck voice.
class SympatheticString {
public:
    static constexpr std::size_t maxStrings = 3;
    bool configure(const pme::StringModes* modes, std::size_t strings, double cutHz) {
        if (strings < 1 || strings > maxStrings) return false;
        strings_ = strings; total_ = 0;
        for (std::size_t j = 0; j < strings; ++j) {
            std::vector<double> g, a;
            if (pme::bridge::liftingOverlaps(modes[j], pme::BridgeEnd::AtL, g) != pme::Status::Ok ||
                pme::bridge::forceCoefficients(modes[j], pme::BridgeEnd::AtL, pme::bridge::Polarisation::Vertical, a) != pme::Status::Ok)
                return false;
            std::size_t n = 0;
            while (n < modes[j].omega.size() && modes[j].omega[n] < 2 * 3.14159265358979323846 * cutHz) ++n;
            offset_[j] = total_; count_[j] = n; total_ += n;
            tOverL_[j] = modes[j].tension / modes[j].L;
            const double dt = 1.0 / modes[j].fs;
            for (std::size_t i = 0; i < n; ++i) {
                const double sd = modes[j].oscV[i].sigmaHat * dt, denom = 1.0 + sd;
                beta_.push_back((1.0 - sd) / denom);
                kappa_.push_back(modes[j].oscV[i].omegaHat2 * dt * dt / denom);
                gamma_.push_back(dt * dt / denom);
                a_.push_back(a[i]); g_.push_back(g[i]);
            }
        }
        q_.assign(total_, 0.0); u_.assign(total_, 0.0); f_.assign(total_, 0.0);
        return true;
    }
    void reset() noexcept { std::fill(q_.begin(), q_.end(), 0.0); std::fill(u_.begin(), u_.end(), 0.0); }
    // SPEC-BRG Bridge force: sum_n a_n q_n - (T/L) w, summed over the strings.
    double force(double w) const noexcept {
        double total = 0;
        for (std::size_t j = 0; j < strings_; ++j) {
            double s = 0;
            if (fast_) s = kern::dotFast(a_.data() + offset_[j], q_.data() + offset_[j], count_[j]);
            else for (std::size_t i = offset_[j]; i < offset_[j] + count_[j]; ++i) s += a_[i] * q_[i];
            total += s - tOverL_[j] * w;
        }
        return total;
    }
    // 0.5.4: reassociated (eight-accumulator) force sums; not bit-identical to the sequential sum.
    void setFast(bool f) noexcept { fast_ = f; }
    // Sum of T/L over the strings: the static part of force(w) is -staticStiffness() * w.
    double staticStiffness() const noexcept { double k = 0; for (std::size_t j = 0; j < strings_; ++j) k += tOverL_[j]; return k; }
    // SPEC-BRG discrete coupling order step 3: modal force -g_n delta_tt w^n.
    void advance(double acceleration) noexcept {
        kern::lightStep(q_.data(), u_.data(), beta_.data(), kappa_.data(), gamma_.data(), g_.data(), f_.data(),
                        acceleration, total_);   // f = -g acc, then SPEC-OSC difference form
    }
    void damp(double factor) noexcept { kern::scaleQU(q_.data(), u_.data(), factor, total_); }
    // 0.5.3 leaky damper: per-mode step factors exp(-sigma_d(f_n)/fs) <= 1 (SPEC-OSC losses plus the
    // felt's added modal damping); multiplying q and u by a factor <= 1 never adds energy.
    void configureLeak(const pme::StringModes* modes, double rate, double exponent, double refHz) {
        leak_.assign(total_, 1.0);
        for (std::size_t j = 0; j < strings_; ++j)
            for (std::size_t i = 0; i < count_[j]; ++i)
                leak_[offset_[j] + i] = leakFactor(modes[j].omega[i] / (2 * 3.14159265358979323846), modes[j].fs, rate, exponent, refHz);
    }
    // sigma_d(f) = rate * (f/refHz)^(-exponent), amplitude decay rate in 1/s added by the damper.
    static double leakFactor(double hz, double fs, double rate, double exponent, double refHz) noexcept {
        const double sigma = exponent == 0 ? rate : rate * std::pow(std::max(hz, 1.0) / refHz, -exponent);
        return std::exp(-sigma / fs);
    }
    void dampLeak() noexcept { kern::scaleQUPerMode(q_.data(), u_.data(), leak_.data(), total_); }
    std::size_t strings() const noexcept { return strings_; }
    std::size_t modeCount(std::size_t j) const noexcept { return count_[j]; }
    const double* q(std::size_t j) const noexcept { return q_.data() + offset_[j]; }
    const double* u(std::size_t j) const noexcept { return u_.data() + offset_[j]; }
private:
    std::size_t strings_{}, total_{};
    bool fast_{false};
    std::array<std::size_t, maxStrings> offset_{}, count_{};
    std::array<double, maxStrings> tOverL_{};
    std::vector<double> beta_, kappa_, gamma_, a_, g_, q_, u_, f_, leak_;
};
}
