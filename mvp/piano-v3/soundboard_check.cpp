// SPDX-License-Identifier: MIT
#include "soundboard.hpp"
#include "pme/simd/denormals.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

static void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main() {
    pme::simd::ScopedFlushDenormals denormals;
    try {
        using piano_mvp_v3::Soundboard;
        // A delayed unit cosine must become cos(theta-phi), not cos(theta+phi).
        const double omega = 200, theta = 1.1, phi = 0.7;
        const double pressure = piano_mvp_v3::modalPressure(std::cos(phi),-std::sin(phi),omega,
                                   -omega*omega*std::cos(theta),-omega*std::sin(theta));
        require(std::abs(pressure + omega*omega*std::cos(theta-phi)) < 1e-10,
                "radiation phase gives an advance instead of a delay");
        const double firstHz = piano_mvp_v3::kSoundboardModes.front()[0]/(2*std::numbers::pi);
        require(std::abs(firstHz-20.6674613474) < 1e-9, "plate fundamental changed");
        for (const auto& row : piano_mvp_v3::kSoundboardModes) {
            require(row[0] > 0 && row[1] > 0 && row[1] < row[0], "invalid generated mode");
            for (double v : row) require(std::isfinite(v), "non-finite generated coefficient");
        }
        for (double fs : {44100., 48000., 96000.}) {
            Soundboard a, b, sum, scaled;
            require(a.prepare(fs) && b.prepare(fs) && sum.prepare(fs) && scaled.prepare(fs), "prepare failed");
            double peak = 0, error = 0, early = 0, late = 0;
            for (int i = 0; i < static_cast<int>(fs*4); ++i) {
                const double x = i == 0 ? 1.0 : 0.0;
                const double y = i == 53 ? -0.3 : 0.0;
                double al,ar,bl,br,sl,sr,kl,kr;
                a.sample(x,0,al,ar); b.sample(0,y,bl,br);
                sum.sample(x,y,sl,sr); scaled.sample(2*x,2*y,kl,kr);
                require(std::isfinite(sl) && std::isfinite(sr), "unstable impulse response");
                if (i < static_cast<int>(a.delaySamples()))
                    require(al == 0 && ar == 0, "pressure arrives before propagation delay");
                peak = std::max(peak,std::max(std::abs(sl),std::abs(sr)));
                error = std::max({error,std::abs(sl-al-bl),std::abs(sr-ar-br),
                                  std::abs(kl-2*sl),std::abs(kr-2*sr)});
                const double e = sl*sl+sr*sr;
                if (i < fs/4) early += e;
                if (i > fs*3.75) late += e;
            }
            require(peak > 1e-7 && error/peak < 1e-10, "soundboard is nonlinear or silent");
            require(early > 0 && late/early < 1e-3, "impulse tail does not decay");
            sum.reset(); double l,r; sum.sample(0,0,l,r);
            require(l == 0 && r == 0, "soundboard reset is not silent");
            std::cout << fs << " Hz: relative superposition error " << error/peak
                      << ", late/early impulse energy " << late/early << '\n';
        }
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
