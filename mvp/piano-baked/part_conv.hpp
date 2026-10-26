// SPDX-License-Identifier: MIT
// 0.6 uniform-partitioned FFT convolution, many inputs (ports) to two outputs, zero added latency.
// Written for this MVP (radix-2 complex FFT, float). prepare() allocates; step() never allocates, locks or throws.
//
// Layout: partition size P (power of two). Taps [0,P) of each IR run as a direct FIR per sample (zero latency);
// taps [P, N) run as P-sample partitions through FFTs of size 2P: at every P-th sample the last P input samples of
// each live input are transformed once and stored in a frequency-delay line; the spectra are multiplied with the
// partition spectra of the IR and summed over inputs and partitions, and one inverse FFT (L in the real part, R in
// the imaginary part) gives the tail of the next P samples (overlap-add). The summation order is fixed (live
// list order, then partition), so the result does not depend on the host block size or on threads.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace pb {
class Fft {   // in-place radix-2, forward (e^-i) and inverse (e^+i, unscaled)
public:
    void prepare(int n) {
        n_ = n; tw_.resize(n / 2); rev_.resize(n);
        for (int i = 0; i < n / 2; ++i) tw_[i] = {float(std::cos(2 * std::numbers::pi * i / n)), float(-std::sin(2 * std::numbers::pi * i / n))};
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b); rev_[i] = r; }
    }
    int size() const noexcept { return n_; }
    void run(std::complex<float>* a, bool inverse) const noexcept {
        for (int i = 0; i < n_; ++i) if (rev_[i] > i) std::swap(a[i], a[rev_[i]]);
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len / 2, step = n_ / len;
            for (int s = 0; s < n_; s += len)
                for (int k = 0; k < half; ++k) {
                    std::complex<float> w = tw_[k * step]; if (inverse) w = std::conj(w);
                    const std::complex<float> u = a[s + k], t = a[s + k + half] * w;
                    a[s + k] = u + t; a[s + k + half] = u - t;
                }
        }
    }
private:
    int n_{}; std::vector<std::complex<float>> tw_; std::vector<int> rev_;
};

class PartConv {
public:
    // irL/irR: per input, `len` taps each. Non-audio; allocates.
    bool prepare(int P, int inputs, const std::vector<const float*>& irL, const std::vector<const float*>& irR, std::size_t len) {
        if (P < 8 || (P & (P - 1)) || inputs < 1 || std::size_t(inputs) != irL.size() || irL.size() != irR.size()) return false;
        P_ = P; in_ = inputs; B_ = ((P + 1 + 7) / 8) * 8;
        J_ = std::max(2, int((len + P - 1) / P));
        fft_.prepare(2 * P); buf_.assign(2 * P, {});
        headL_.assign(std::size_t(in_) * P, 0.f); headR_ = headL_;
        hist_.assign(std::size_t(in_) * 2 * P, 0.f);
        const std::size_t specSize = std::size_t(J_ - 1) * B_;
        for (int ch = 0; ch < 2; ++ch) { hRe_[ch].assign(in_ * specSize, 0.f); hIm_[ch] = hRe_[ch]; accRe_[ch].assign(B_, 0.f); accIm_[ch] = accRe_[ch]; }
        std::vector<std::complex<float>> t(2 * P);
        for (int c = 0; c < in_; ++c)
            for (int ch = 0; ch < 2; ++ch) {
                const float* ir = ch == 0 ? irL[c] : irR[c];
                float* head = (ch == 0 ? headL_ : headR_).data() + std::size_t(c) * P;
                for (int n = 0; n < P; ++n) head[P - 1 - n] = std::size_t(n) < len ? ir[n] : 0.f;
                for (int j = 1; j < J_; ++j) {
                    std::fill(t.begin(), t.end(), std::complex<float>{});
                    for (int n = 0; n < P; ++n) { const std::size_t k = std::size_t(j) * P + n; if (k < len) t[n] = ir[k]; }
                    fft_.run(t.data(), false);
                    float* re = hRe_[ch].data() + std::size_t(c) * specSize + std::size_t(j - 1) * B_;
                    float* im = hIm_[ch].data() + std::size_t(c) * specSize + std::size_t(j - 1) * B_;
                    for (int k = 0; k <= P; ++k) { re[k] = t[k].real(); im[k] = t[k].imag(); }
                }
            }
        xRe_.assign(std::size_t(in_) * J_ * B_, 0.f); xIm_ = xRe_;
        slotValid_.assign(std::size_t(in_) * J_, 0); lastDriven_.assign(in_, 0); inLive_.assign(in_, 0);
        blockDriven_.assign(in_, 0); cur_.assign(in_, 0.f); live_.assign(in_, 0);
        tailL_.assign(P, 0.f); tailR_ = tailL_; prevL_ = tailL_; prevR_ = tailL_;
        reset(); return true;
    }
    void reset() noexcept {
        std::fill(hist_.begin(), hist_.end(), 0.f); std::fill(xRe_.begin(), xRe_.end(), 0.f); std::fill(xIm_.begin(), xIm_.end(), 0.f);
        std::fill(slotValid_.begin(), slotValid_.end(), 0); std::fill(inLive_.begin(), inLive_.end(), 0);
        std::fill(blockDriven_.begin(), blockDriven_.end(), 0); std::fill(cur_.begin(), cur_.end(), 0.f);
        for (auto* v : {&tailL_, &tailR_, &prevL_, &prevR_}) std::fill(v->begin(), v->end(), 0.f);
        liveCount_ = 0; pos_ = 0; block_ = 0;
    }
    int partitions() const noexcept { return J_; }
    int liveInputs() const noexcept { return liveCount_; }
    std::size_t bytes() const noexcept {
        return (headL_.size() * 2 + hist_.size() + xRe_.size() * 2 + hRe_[0].size() * 4) * sizeof(float);
    }
    // One sample: g[k] is the drive of input ids[k] (distinct), k < count. Output L, R.
    void step(const int* ids, const float* g, int count, double& outL, double& outR) noexcept {
        const std::size_t P = P_;
        for (int k = 0; k < count; ++k) {
            const int c = ids[k];
            cur_[c] = g[k];
            if (g[k] != 0.f) { blockDriven_[c] = 1; lastDriven_[c] = block_; }
            if (!inLive_[c]) { inLive_[c] = 1; live_[liveCount_++] = c; }
        }
        float l = 0, r = 0;
        for (int k = 0; k < liveCount_; ++k) {
            const int c = live_[k];
            float* h = hist_.data() + std::size_t(c) * 2 * P;
            h[histPos()] = h[histPos() + P] = cur_[c]; cur_[c] = 0.f;
            const float* w = h + histPos() + 1;
            const float* hl = headL_.data() + std::size_t(c) * P; const float* hr = headR_.data() + std::size_t(c) * P;
            float sl[8] = {}, sr[8] = {};
            for (std::size_t n = 0; n < P; n += 8)
                for (int u = 0; u < 8; ++u) { sl[u] += hl[n + u] * w[n + u]; sr[u] += hr[n + u] * w[n + u]; }
            l += ((sl[0] + sl[1]) + (sl[2] + sl[3])) + ((sl[4] + sl[5]) + (sl[6] + sl[7]));
            r += ((sr[0] + sr[1]) + (sr[2] + sr[3])) + ((sr[4] + sr[5]) + (sr[6] + sr[7]));
        }
        outL = double(l) + double(tailL_[pos_]); outR = double(r) + double(tailR_[pos_]);
        if (++pos_ == P) { pos_ = 0; boundary(); ++block_; }
    }
private:
    std::size_t histPos() const noexcept { return pos_; }
    void boundary() noexcept {
        const std::size_t P = P_, specSize = std::size_t(J_ - 1) * B_;
        const int slot = block_ % J_;
        // 1. transform the block just completed for every live input (zero if it was silent)
        for (int k = 0; k < liveCount_; ++k) {
            const int c = live_[k];
            std::uint8_t& valid = slotValid_[std::size_t(c) * J_ + slot];
            valid = blockDriven_[c];
            if (!valid) continue;
            const float* h = hist_.data() + std::size_t(c) * 2 * P + (P - 1) + 1;   // last write was index P-1: window = h[P .. 2P-1]
            for (std::size_t n = 0; n < P; ++n) buf_[n] = h[n];
            for (std::size_t n = P; n < 2 * P; ++n) buf_[n] = 0.f;
            fft_.run(buf_.data(), false);
            float* re = xRe_.data() + (std::size_t(c) * J_ + slot) * B_; float* im = xIm_.data() + (std::size_t(c) * J_ + slot) * B_;
            for (std::size_t b = 0; b <= P; ++b) { re[b] = buf_[b].real(); im[b] = buf_[b].imag(); }
        }
        // 2. Z for the next block = sum over inputs and partitions j >= 1 of X_{m+1-j} H_j
        for (int ch = 0; ch < 2; ++ch) { std::fill(accRe_[ch].begin(), accRe_[ch].end(), 0.f); std::fill(accIm_[ch].begin(), accIm_[ch].end(), 0.f); }
        bool any = false;
        for (int k = 0; k < liveCount_; ++k) {
            const int c = live_[k];
            for (int j = 1; j < J_; ++j) {
                const int s = ((block_ - (j - 1)) % J_ + J_) % J_;
                if (block_ - (j - 1) < 0 || !slotValid_[std::size_t(c) * J_ + s]) continue;
                any = true;
                const float* xr = xRe_.data() + (std::size_t(c) * J_ + s) * B_; const float* xi = xIm_.data() + (std::size_t(c) * J_ + s) * B_;
                for (int ch = 0; ch < 2; ++ch) {
                    const float* hr = hRe_[ch].data() + std::size_t(c) * specSize + std::size_t(j - 1) * B_;
                    const float* hi = hIm_[ch].data() + std::size_t(c) * specSize + std::size_t(j - 1) * B_;
                    float* ar = accRe_[ch].data(); float* ai = accIm_[ch].data();
                    for (int b = 0; b < B_; ++b) { ar[b] += xr[b] * hr[b] - xi[b] * hi[b]; ai[b] += xr[b] * hi[b] + xi[b] * hr[b]; }
                }
            }
        }
        if (any) {
            // pack: Z[k] = YL[k] + i YR[k]; Z[N-k] = conj(YL[k]) + i conj(YR[k])
            const std::size_t N = 2 * P;
            for (std::size_t b = 0; b <= P; ++b) {
                const std::complex<float> yl{accRe_[0][b], accIm_[0][b]}, yr{accRe_[1][b], accIm_[1][b]};
                buf_[b] = yl + std::complex<float>(0, 1) * yr;
                if (b > 0 && b < P) buf_[N - b] = std::conj(yl) + std::complex<float>(0, 1) * std::conj(yr);
            }
            fft_.run(buf_.data(), true);
            const float scale = 1.f / float(N);
            for (std::size_t n = 0; n < P; ++n) {
                tailL_[n] = buf_[n].real() * scale + prevL_[n]; tailR_[n] = buf_[n].imag() * scale + prevR_[n];
                prevL_[n] = buf_[n + P].real() * scale; prevR_[n] = buf_[n + P].imag() * scale;
            }
        } else {
            for (std::size_t n = 0; n < P; ++n) { tailL_[n] = prevL_[n]; tailR_[n] = prevR_[n]; prevL_[n] = prevR_[n] = 0.f; }
        }
        // 3. retire inputs with nothing left (history P samples, FDL J blocks)
        for (int k = 0; k < liveCount_;) {
            const int c = live_[k];
            blockDriven_[c] = 0;
            if (block_ - lastDriven_[c] > J_ + 1) {
                inLive_[c] = 0; live_[k] = live_[--liveCount_];
                // note: swap changes the summation order deterministically (a function of the input stream only)
                std::fill_n(hist_.data() + std::size_t(c) * 2 * P, 2 * P, 0.f);
                for (int s = 0; s < J_; ++s) slotValid_[std::size_t(c) * J_ + s] = 0;
            } else ++k;
        }
    }
    int P_{}, in_{}, J_{}, B_{}, liveCount_{}, block_{};
    Fft fft_; std::vector<std::complex<float>> buf_;
    std::vector<float> headL_, headR_, hist_, hRe_[2], hIm_[2], xRe_, xIm_, accRe_[2], accIm_[2], tailL_, tailR_, prevL_, prevR_, cur_;
    std::vector<std::uint8_t> slotValid_, inLive_, blockDriven_; std::vector<int> lastDriven_, live_;
    std::size_t pos_{};
};
}
