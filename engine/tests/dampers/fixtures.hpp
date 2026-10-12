// SPDX-License-Identifier: MIT
// Shared strings, dampers and helpers for the A6 tests.
#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

#include "pme/dampers.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/string.hpp"
#include "pme/string/string_model.hpp"

namespace pme::dmp_test {

inline constexpr double kFs = 48000.0;
inline constexpr double kFMax = 16000.0;

/// Plain steel string, d = 1.0 mm (as the A2 fixtures), with the given losses.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1, b1
inline StringParams plainSteel(double L, double f1, double b1 = 0.4) {
    StringParams p;
    p.L = L;
    p.dCore = 1.0e-3;
    p.dOuter = 1.0e-3;
    p.rhoCore = 7850.0;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    p.b1 = b1;
    p.b2 = 1.0e-3;
    p.b3 = 1.0e-11;
    return p;
}

/// Wound bass string (as the A2 fixtures).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1
inline StringParams woundBass(double L, double f1) {
    StringParams p;
    p.L = L;
    p.dCore = 1.6e-3;
    p.dOuter = 4.7e-3;
    p.rhoCore = 7850.0;
    p.rhoWind = 8960.0;
    p.packing = 0.85;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    p.b1 = 0.3;
    p.b2 = 5.0e-4;
    p.b3 = 5.0e-12;
    return p;
}

/// A lossless string: b1 = b2 = b3 = 0.
inline StringParams lossless(double L, double f1) {
    StringParams p = plainSteel(L, f1, 0.0);
    p.b2 = 0.0;
    p.b3 = 0.0;
    return p;
}

/// Pad of the example instruments (docs/dev/instrument-format.md), at an x_d that is not a node
/// of any low mode.
inline DamperParams examplePad() {
    DamperParams d;
    d.xRatio = 0.0537;
    d.width = 0.02;
    d.Kd = 5000.0;
    d.Rd = 5.0;
    return d;
}

/// Compile a string at kFs / kFMax; returns the tables (aborts the test via empty tables on
/// failure).
inline StringModes compile(const StringParams& p, double fMax = kFMax) {
    StringModes m;
    static_cast<void>(compileString(p, kFs, fMax, {}, m));
    return m;
}

/// Modal coordinates (orthonormal shapes) of a triangular pluck of height h at x_p:
/// q_n = sqrt(L/2) * 2 h L^2 sin(n pi x_p/L) / (pi^2 n^2 x_p (L - x_p)).
inline std::vector<double> pluckModal(const StringModes& m, double xpRatio, double h = 1.0e-3) {
    std::vector<double> q(m.omega.size());
    const double xp = xpRatio * m.L;
    for (std::size_t i = 0; i < q.size(); ++i) {
        const auto n = static_cast<double>(i + 1);
        const double b = 2.0 * h * m.L * m.L * std::sin(n * std::numbers::pi * xpRatio) /
                         (std::numbers::pi * std::numbers::pi * n * n * xp * (m.L - xp));
        q[i] = std::sqrt(m.L / 2.0) * b;
    }
    return q;
}

/// Point shapes phi_n(x0) (W = 1) in float32 for the output observation.
inline std::vector<float> pointShape(const StringModes& m, double x0Ratio) {
    std::vector<float> phi(m.omega.size());
    string::windowedShapes(m, x0Ratio * m.L, string::WindowKind::Point, 0.0, phi);
    return phi;
}

/// Deterministic generator.
class Rng {
public:
    explicit Rng(unsigned long long seed) : s_(seed) {}
    double uniform() {
        s_ = s_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(s_ >> 11U) * 0x1.0p-53;
    }

private:
    unsigned long long s_;
};

}  // namespace pme::dmp_test
