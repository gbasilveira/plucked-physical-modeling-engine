// SPDX-License-Identifier: MIT
#include "grand_live.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace piano_mvp_v4 {
namespace {
struct Raw {
    char magic[4]; std::uint32_t version, lowPoles, lowLight, hfModes, sets; float outScale; std::uint32_t pad;
    std::uint64_t off[14]; std::int32_t refDelay[6]; std::uint32_t pad2[2]; std::uint64_t size;
};
static_assert(sizeof(Raw) == 184, "PGL2 header layout");
template <class T> const T* at(const std::uint8_t* blob, std::size_t size, std::uint64_t off, std::size_t count) {
    if (off % alignof(T) || off > size || count > (size - off) / sizeof(T)) return nullptr;
    return reinterpret_cast<const T*>(blob + off);
}
}
bool GrandTable::parse(const std::uint8_t* blob, std::size_t size) {
    valid_ = false;
    if (!blob || size < sizeof(Raw)) return false;
    Raw r; std::memcpy(&r, blob, sizeof r);
    if (std::memcmp(r.magic, "PGL2", 4) != 0 || r.version != 2 || r.size != size || r.sets != kGrandSets) return false;
    if (r.lowPoles == 0 || r.lowPoles % 8 || r.lowLight > r.lowPoles || r.lowLight % 8 || r.hfModes == 0 || r.hfModes % 32) return false;
    lowPoles = r.lowPoles; lowLight = r.lowLight; hfModes = r.hfModes; outScale = r.outScale;
    const std::size_t P = lowPoles, Nh = hfModes, planes = std::size_t{kGrandSets} * 2 * 88 * P;
    lfOmega = at<double>(blob, size, r.off[0], std::size_t{kGrandLids} * P); lfSigma = at<double>(blob, size, r.off[1], std::size_t{kGrandLids} * P);
    lfRe = at<float>(blob, size, r.off[2], planes); lfIm = at<float>(blob, size, r.off[3], planes);
    hfOmega = at<double>(blob, size, r.off[4], Nh); hfSigma = at<double>(blob, size, r.off[5], Nh);
    hfPhiT = at<float>(blob, size, r.off[6], Nh * 88);
    hfWre = at<float>(blob, size, r.off[7], std::size_t{kGrandSets} * 2 * Nh); hfWim = at<float>(blob, size, r.off[8], std::size_t{kGrandSets} * 2 * Nh);
    hfWe = at<float>(blob, size, r.off[9], std::size_t{kGrandSets} * 2 * Nh);
    hfDelay = at<std::uint16_t>(blob, size, r.off[10], kGrandSets * 2);
    if (!lfOmega || !lfSigma || !lfRe || !lfIm || !hfOmega || !hfSigma || !hfPhiT || !hfWre || !hfWim || !hfWe || !hfDelay) return false;
    for (int i = 0; i < kGrandMicChannels; ++i) refDelay[i] = r.refDelay[i];
    // direct radiation
    std::uint64_t o = r.off[11];
    const auto u32 = [&](std::uint32_t& x) { if (o + 4 > size) return false; std::memcpy(&x, blob + o, 4); o += 4; return true; };
    for (int k = 0; k < 88; ++k) {
        std::uint32_t ns; if (!u32(ns) || ns > 3) return false;
        for (std::uint32_t j = 0; j < 3; ++j) direct[k][j] = {};
        for (std::uint32_t j = 0; j < ns; ++j) {
            auto& d = direct[k][j];
            if (!u32(d.nV) || !u32(d.nH)) return false;
            d.idxV = at<std::uint16_t>(blob, size, o, d.nV); if (!d.idxV && d.nV) return false;
            o += (std::uint64_t{d.nV} * 2 + 3) / 4 * 4;
            d.idxH = at<std::uint16_t>(blob, size, o, d.nH); if (!d.idxH && d.nH) return false;
            o += (std::uint64_t{d.nH} * 2 + 3) / 4 * 4;
            d.gV = at<float>(blob, size, o, std::size_t{d.nV} * 12); if (!d.gV && d.nV) return false;
            o += std::uint64_t{d.nV} * 48;
            d.gH = at<float>(blob, size, o, std::size_t{d.nH} * 12); if (!d.gH && d.nH) return false;
            o += std::uint64_t{d.nH} * 48;
        }
    }
    // strings' board (Body Grand): 512 omega, 512 sigma, 88 x 512 phi, doubles
    const double* b = at<double>(blob, size, r.off[12], 1024 + 88 * 512);
    if (!b) return false;
    board.omega.assign(b, b + 512); board.sigma.assign(b + 512, b + 1024); board.phi.assign(b + 1024, b + 1024 + 88 * 512);
    valid_ = true; return true;
}

bool GrandRadiation::configure(const GrandTable* table, double fs) {
    if (!table || !table->valid() || !(fs >= 8000 && fs <= 192000)) return false;
    table_ = table; fs_ = fs; ratio_ = fs / 48000.0;
    P_ = table->lowPoles; Nh_ = table->hfModes; hfChunks_ = Nh_ / 8;
    // poles are stored as continuous a = -sigma + j omega; the one-sample factor is exp(a / fs)
    lamRe_.assign(std::size_t{kGrandLids} * P_, 0.0); lamIm_ = lamRe_;
    for (std::size_t i = 0; i < lamRe_.size(); ++i) {
        const double s = table->lfSigma[i], w = table->lfOmega[i];
        const double mag = std::exp(-s / fs);
        if (!(w > 0) || w >= 0.95 * std::numbers::pi * fs) continue;     // above the Nyquist limit: dropped (zero state)
        lamRe_[i] = mag * std::cos(w / fs); lamIm_[i] = mag * std::sin(w / fs);
    }
    hfLamRe_.assign(Nh_, 0.f); hfLamIm_ = hfLamRe_; hxre_ = hfLamRe_; hxim_ = hfLamRe_; hfDrive_ = hfLamRe_;
    for (std::size_t j = 0; j < Nh_; ++j) {
        const double s = table->hfSigma[j], w = table->hfOmega[j];
        if (!(w > 0) || w >= 0.95 * std::numbers::pi * fs) continue;
        const double mag = std::exp(-s / fs);
        hfLamRe_[j] = static_cast<float>(mag * std::cos(w / fs)); hfLamIm_[j] = static_cast<float>(mag * std::sin(w / fs));
    }
    s0_ = std::make_unique<Stream>(); s1_ = std::make_unique<Stream>();
    for (Stream* s : {s0_.get(), s1_.get()}) {
        s->xre.assign(2 * P_, 0.0); s->xim = s->xre; s->dre.assign(2 * P_, 0.f); s->dim = s->dre;
    }
    fadeLen_ = std::max<std::size_t>(1, static_cast<std::size_t>(0.02 * fs));
    lid_ = 0; mic_ = 0; fading_ = false; s0_->set = 0; s0_->lid = 0; s0_->mic = 0;
    slot_ = {presetMic(0, 0), presetMic(0, 1), -1, -1};
    reset(); return true;
}
void GrandRadiation::reset() noexcept {
    std::fill(hxre_.begin(), hxre_.end(), 0.f); std::fill(hxim_.begin(), hxim_.end(), 0.f); std::fill(hfDrive_.begin(), hfDrive_.end(), 0.f);
    for (Stream* s : {s0_.get(), s1_.get()}) if (s) {
        std::fill(s->xre.begin(), s->xre.end(), 0.0); std::fill(s->xim.begin(), s->xim.end(), 0.0);
        std::fill(s->dre.begin(), s->dre.end(), 0.f); std::fill(s->dim.begin(), s->dim.end(), 0.f);
        for (auto& r : s->hf) r.fill(0.f);
        for (auto& r : s->dir) r.fill(0.f);
        s->cursor = 0;
    }
}
void GrandRadiation::accumulateStream(Stream& s, const std::uint8_t* port, const float* g, const std::uint8_t* light, int count) noexcept {
    const GrandTable& t = *table_;
    for (int ch = 0; ch < 2; ++ch) {
        float* dre = s.dre.data() + ch * P_; float* dim = s.dim.data() + ch * P_;
        for (int k = 0; k < count; ++k) {
            const std::size_t n = light[k] ? t.lowLight : P_;
            kern::axpyC(dre, dim, t.re(s.set, ch, port[k]), t.im(s.set, ch, port[k]), g[k], n);
        }
    }
}
void GrandRadiation::accumulate(const std::uint8_t* port, const float* g, const std::uint8_t* light, int count) noexcept {
    kern::bankDrive(hfDrive_.data(), table_->hfPhiT, port, g, light, count, hfChunks_, 0);   // unstruck strings carry nothing above 2 kHz
    accumulateStream(*s0_, port, g, light, count);
    if (fading_) accumulateStream(*s1_, port, g, light, count);
}
void GrandRadiation::step() noexcept {
    // the feedthrough of the high range uses the drive of this sample (hfStep consumes it)
    for (Stream* s : {s0_.get(), fading_ ? s1_.get() : nullptr}) {
        if (!s) continue;
        for (int ch = 0; ch < 2; ++ch) s->ff[ch] = kern::dotF(table_->hfWe + (static_cast<std::size_t>(s->set) * 2 + ch) * Nh_, hfDrive_.data(), Nh_);
    }
    kern::hfStep(hfLamRe_.data(), hfLamIm_.data(), hxre_.data(), hxim_.data(), hfDrive_.data(), Nh_);
    for (Stream* s : {s0_.get(), fading_ ? s1_.get() : nullptr}) {
        if (!s) continue;
        for (int ch = 0; ch < 2; ++ch)
            kern::lfStep(lamRe_.data() + s->lid * P_, lamIm_.data() + s->lid * P_, s->xre.data() + ch * P_, s->xim.data() + ch * P_,
                         s->dre.data() + ch * P_, s->dim.data() + ch * P_, P_);
    }
}
void GrandRadiation::outputOf(Stream& s, const float* direct, double& left, double& right) noexcept {
    const GrandTable& t = *table_;
    double out[2];
    for (int ch = 0; ch < 2; ++ch) {
        const double lo = kern::sumLanes(s.xre.data() + ch * P_, P_);
        const std::size_t wo = (static_cast<std::size_t>(s.set) * 2 + ch) * Nh_;
        const double h = kern::hfDot(t.hfWre + wo, t.hfWim + wo, hxre_.data(), hxim_.data(), Nh_) + s.ff[ch];
        s.hf[ch][s.cursor & (ringN - 1)] = static_cast<float>(h);
        const std::size_t d = std::min<std::size_t>(ringN - 1, static_cast<std::size_t>(t.hfDelay[s.set * 2 + ch] * ratio_ + 0.5));
        out[ch] = lo + s.hf[ch][(s.cursor - d) & (ringN - 1)];
    }
    // direct radiation of the struck strings: pushed per microphone, read at the microphone's arrival offset
    if (directOn_) {
        for (int ch = 0; ch < 2; ++ch) {
            const int m = presetMic(s.mic, ch);
            float x = 0.f;
            if (direct) for (int i = 0; i < 4; ++i) if (slot_[i] == m) x = direct[i];
            s.dir[m][s.cursor & (ringN - 1)] = x;
            const std::size_t d = std::min<std::size_t>(ringN - 1, static_cast<std::size_t>(std::max(0, t.refDelay[m]) * ratio_ + 0.5));
            out[ch] += s.dir[m][(s.cursor - d) & (ringN - 1)];
        }
    }
    ++s.cursor;
    left = out[0] * t.outScale; right = out[1] * t.outScale;
}
void GrandRadiation::finish(double& left, double& right, const float* direct) noexcept {
    outputOf(*s0_, direct, left, right);
    if (fading_) {
        double l1, r1; outputOf(*s1_, direct, l1, r1);
        const double a = static_cast<double>(++fadePos_) / static_cast<double>(fadeLen_);
        left += a * (l1 - left); right += a * (r1 - right);
        if (fadePos_ >= fadeLen_) promote();
    }
}
void GrandRadiation::promote() noexcept {
    std::swap(s0_, s1_);
    fading_ = false; fadePos_ = 0; lid_ = s0_->lid; mic_ = s0_->mic;
    slot_ = {presetMic(mic_, 0), presetMic(mic_, 1), -1, -1};
}
void GrandRadiation::request(int lid, int mic) noexcept {
    lid = std::clamp(lid, 0, kGrandLids - 1); mic = std::clamp(mic, 0, kGrandMics - 1);
    if (fading_) promote();
    if (lid == lid_ && mic == mic_) return;
    Stream& t = *s1_;
    t.lid = lid; t.mic = mic; t.set = lid * 3 + mic;
    std::fill(t.xre.begin(), t.xre.end(), 0.0); std::fill(t.xim.begin(), t.xim.end(), 0.0);
    std::fill(t.dre.begin(), t.dre.end(), 0.f); std::fill(t.dim.begin(), t.dim.end(), 0.f);
    for (auto& r : t.hf) r.fill(0.f);
    for (auto& r : t.dir) r.fill(0.f);
    t.cursor = s0_->cursor;
    fading_ = true; fadePos_ = 0;
    slot_ = {presetMic(mic_, 0), presetMic(mic_, 1), mic == mic_ ? -1 : presetMic(mic, 0), mic == mic_ ? -1 : presetMic(mic, 1)};
}
double GrandRadiation::outputBound() const noexcept {
    const GrandTable& t = *table_; double b = 0;
    for (const Stream* s : {s0_.get(), fading_ ? s1_.get() : nullptr}) {
        if (!s) continue;
        for (int ch = 0; ch < 2; ++ch) {
            double x = 0;
            for (std::size_t i = 0; i < P_; ++i) x += std::abs(s->xre[ch * P_ + i]);
            const std::size_t wo = (static_cast<std::size_t>(s->set) * 2 + ch) * Nh_;
            for (std::size_t j = 0; j < Nh_; ++j) x += (std::abs(t.hfWre[wo + j]) + std::abs(t.hfWim[wo + j])) * (std::abs(hxre_[j]) + std::abs(hxim_[j]));
            for (float f : s->hf[ch]) x = std::max(x, static_cast<double>(std::abs(f)));
            b = std::max(b, x);
        }
        for (const auto& r : s->dir) for (float f : r) b = std::max(b, static_cast<double>(std::abs(f)));
    }
    return b * std::abs(t.outScale);
}
}
