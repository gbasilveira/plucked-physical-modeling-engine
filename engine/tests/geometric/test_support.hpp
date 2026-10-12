// SPDX-License-Identifier: MIT
// Shared fixtures of the A7 tests: string descriptions, a segment builder, a deterministic
// generator and small signal helpers.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "pme/bridge/terms.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"
#include "pme/string/longitudinal.hpp"
#include "pme/string/longitudinal_segment.hpp"

namespace pme::geometric_test {

inline constexpr double kFs = 48000.0;
inline constexpr double kPi = std::numbers::pi;

/// Scientific-notation text of a number for the evidence lines the tests print.
inline std::string sci(double v, int precision = 3) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(precision) << v;
    return os.str();
}

/// Fixed-point text.
inline std::string fix(double v, int precision = 3) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

/// Deterministic uniform generator (library distributions differ between standard libraries).
class Rng {
public:
    explicit Rng(std::uint64_t seed) : eng_(seed) {}
    /// Uniform in [0, 1).
    double uniform() { return static_cast<double>(eng_() >> 11U) * 0x1.0p-53; }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

private:
    std::mt19937_64 eng_;
};

/// Plain steel string: rho = 7850, E = 200 GPa, diameter `d`; `b1` is the intrinsic loss.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1, d, b1
inline StringParams plainSteel(double L, double f1, double d = 1.0e-3, double b1 = 0.0) {
    StringParams p;
    p.L = L;
    p.dCore = d;
    p.dOuter = d;
    p.rhoCore = 7850.0;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    p.b1 = b1;
    return p;
}

/// Wound bass string (core 1.6 mm, outer 4.7 mm).
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
    return p;
}

inline StringModes compile(const StringParams& p, double fMax = 14000.0, double fs = kFs) {
    StringModes m;
    if (compileString(p, fs, fMax, {}, m) != Status::Ok) {
        return StringModes{};
    }
    return m;
}

/// g_n and a_n of both polarisations (A4 `liftingOverlaps`, `forceCoefficients`). Empty vectors
/// on failure.
inline string::BridgeCoupling couplingFor(const StringModes& m, BridgeEnd end) {
    string::BridgeCoupling c;
    const bool ok =
        bridge::liftingOverlaps(m, end, c.g) == Status::Ok &&
        bridge::forceCoefficients(m, end, bridge::Polarisation::Vertical, c.aV) == Status::Ok &&
        bridge::forceCoefficients(m, end, bridge::Polarisation::Horizontal, c.aH) == Status::Ok;
    if (!ok) {
        return string::BridgeCoupling{};
    }
    return c;
}

/// Everything a test needs for one segment.
template <class S>
struct Built {
    StringParams params;
    StringModes modes;
    string::GeometricSegment<S> seg;
    Status status{Status::Ok};
};

/// Compile `params` at `fs` and configure a `GeometricSegment<S>` with `config`.
template <class S>
inline void build(Built<S>& b, const StringParams& params, const string::GeometricConfig& config,
                  double fMax = 14000.0, double fs = kFs) {
    b.params = params;
    b.modes = compile(params, fMax, fs);
    if (b.modes.k.empty()) {
        b.status = Status::Rejected;
        return;
    }
    b.status = b.seg.configure(b.modes, params, couplingFor(b.modes, params.bridgeEnd), config);
}

/// Triangle pluck released at rest (SPEC-PLK "Initial displacement"): peak height `amp` (m) at
/// `xp` (m), split into the polarisations by `theta`:
/// q_n = sqrt(2/L) amp L sin(k_n xp) / (k_n^2 xp (L - xp)). `qV` and `qH` receive N values each.
template <class S>
inline void trianglePluck(const StringModes& m, double amp, double xp, double theta, S* qV, S* qH) {
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const double k = m.k[i];
        const double q =
            std::sqrt(2.0 / m.L) * amp * m.L * std::sin(k * xp) / (k * k * xp * (m.L - xp));
        qV[i] = static_cast<S>(q * std::cos(theta));
        qH[i] = static_cast<S>(q * std::sin(theta));
    }
}

/// S = sum k_n^2 (q_v,n^2 + q_h,n^2) in float64.
template <class S>
inline double slopeIntegral(const StringModes& m, const S* qV, const S* qH) {
    double s = 0.0;
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        s += m.k[i] * m.k[i] *
             (static_cast<double>(qV[i]) * static_cast<double>(qV[i]) +
              static_cast<double>(qH[i]) * static_cast<double>(qH[i]));
    }
    return s;
}

}  // namespace pme::geometric_test
