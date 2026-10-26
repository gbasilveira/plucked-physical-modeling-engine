// SPDX-License-Identifier: MIT
#include "fitted_string.hpp"
#include "kernels.hpp"
#include <cmath>
#include <fstream>
#include <numbers>

namespace piano_baked {
namespace {
// One step of all rotors; returns the sum of the real parts before the step (eight lanes, fixed combine order).
PIANO_CLONES float rotorStep(float* __restrict sr, float* __restrict si, const float* __restrict zr, const float* __restrict zi, std::size_t n) noexcept {
    float acc[8] = {};
    for (std::size_t i = 0; i < n; i += 8)
        for (int l = 0; l < 8; ++l) {
            const float r = sr[i + l], q = si[i + l];
            acc[l] += r;
            sr[i + l] = zr[i + l] * r - zi[i + l] * q;
            si[i + l] = zr[i + l] * q + zi[i + l] * r;
        }
    return ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
}
}
bool FittedString::load(const std::string& path) {
    std::ifstream in(path);
    std::string magic; int version = 0;
    if (!(in >> magic >> version) || magic != "FIT" || version != 1) return false;
    int fs = 0, tableN = 0; std::size_t comps = 0;
    if (!(in >> key_ >> fs >> ta_ >> nfade_ >> tableN >> comps) || tableN < ta_ + nfade_) return false;
    table_.resize(static_cast<std::size_t>(tableN));
    for (auto& x : table_) if (!(in >> x)) return false;
    count_ = comps; padded_ = (comps + 7) / 8 * 8;
    zr_.assign(padded_, 0.f); zi_ = sr0_ = si0_ = zr_;
    for (std::size_t m = 0; m < comps; ++m) {
        double f, sigma, re, im;
        if (!(in >> f >> sigma >> re >> im)) return false;
        const double rad = std::exp(-sigma / fs), th = 2 * std::numbers::pi * f / fs;
        zr_[m] = static_cast<float>(rad * std::cos(th)); zi_[m] = static_cast<float>(rad * std::sin(th));
        sr0_[m] = static_cast<float>(re); si0_[m] = static_cast<float>(im);
    }
    sr_ = sr0_; si_ = si0_;
    reset();
    return true;
}
void FittedString::reset() noexcept { n_ = 0; sr_ = sr0_; si_ = si0_; }
float FittedString::next() noexcept {
    const std::size_t n = n_++;
    if (n < static_cast<std::size_t>(ta_)) return table_[n];
    const float m = rotorStep(sr_.data(), si_.data(), zr_.data(), zi_.data(), padded_);
    const std::size_t k = n - static_cast<std::size_t>(ta_);
    if (k >= static_cast<std::size_t>(nfade_)) return m;
    const float w = 0.5f * (1.f - std::cos(static_cast<float>(std::numbers::pi) * (static_cast<float>(k) + 0.5f) / static_cast<float>(nfade_)));
    return (1.f - w) * table_[n] + w * m;
}
}
