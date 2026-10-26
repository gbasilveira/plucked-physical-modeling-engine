// SPDX-License-Identifier: MIT
// 0.6 step 2 (experiment, not part of the plug-in): a cheap live string fitted to a Full bridge-force capture.
// The force is the tabulated attack of the Full render (up to ta + nfade samples) crossfaded to a bank of damped
// complex rotors s_m <- z_m s_m (z_m = exp((-sigma + i 2 pi f)/fs)), force = sum Re s_m. Float state, fixed reduction
// order. Fitted by fit_string.py (matrix pencil on the band-limited analytic signal of each partial).
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace piano_baked {
class FittedString {
public:
    bool load(const std::string& path);     // non-audio; allocates
    void reset() noexcept;                  // restart from the note-on
    float next() noexcept;                  // bridge force (N) of the next sample; no allocation
    int key() const noexcept { return key_; }
    std::size_t components() const noexcept { return count_; }
    std::size_t tableSamples() const noexcept { return table_.size(); }
private:
    int key_{}, ta_{}, nfade_{};
    std::size_t count_{}, padded_{}, n_{};
    std::vector<float> table_, zr_, zi_, sr_, si_, sr0_, si0_;
    float fadeStep_{};
};
}
