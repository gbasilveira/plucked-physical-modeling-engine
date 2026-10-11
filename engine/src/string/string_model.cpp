// SPDX-License-Identifier: MIT
// Stiff string compiler (A2): SPEC-STR. See docs/dev/string.md.
#include "pme/string/string_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

#include "pme/osc/oscillator.hpp"

namespace pme::string {

namespace {

constexpr double kPi = std::numbers::pi;

bool finitePositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }
bool finiteNonNegative(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

/// Quantities derived once per string.
struct Derived {
    double rhoA{};
    double EI{};
    double tension{};
    double inharmonicity{};
    double fletcher{1.0};  // multiplies omega_n for clamped ends
};

// SPEC-STR "Wound strings", "Tension for pitch", "Modes".
Status derive(const StringParams& p, double fs, Derived& d) noexcept {
    if (!finitePositive(fs) || validate(p) != Status::Ok) {
        return Status::InvalidArgument;
    }
    d.rhoA = linearDensity(p);
    d.EI = bendingStiffness(p);
    d.tension = tensionForPitch(p, p.f1Target);
    if (!finitePositive(d.rhoA) || !finitePositive(d.EI) || !finitePositive(d.tension)) {
        return Status::InvalidArgument;
    }
    // SPEC-STR "Modes": B = pi^2 EI / (T L^2)
    d.inharmonicity = kPi * kPi * d.EI / (d.tension * p.L * p.L);
    d.fletcher = 1.0;
    if (p.boundary == Boundary::Clamped) {
        // SPEC-STR "Modes": Fletcher's factor 1 + 2 sqrt(B)/pi + 4 B/pi^2, applied to every n.
        const double sb = std::sqrt(d.inharmonicity);
        d.fletcher = 1.0 + 2.0 * sb / kPi + 4.0 * d.inharmonicity / (kPi * kPi);
    }
    return Status::Ok;
}

double modeK(const StringParams& p, int n) noexcept {
    return static_cast<double>(n) * kPi / p.L;  // SPEC-STR "Modes": k_n = n pi / L
}

// SPEC-STR "Modes": omega_n^2 = (T k_n^2 + EI k_n^4) / rhoA, a sum of two positive terms (no
// cancellation), times Fletcher's factor for clamped ends.
double modeOmega(const Derived& d, double k) noexcept {
    const double k2 = k * k;
    return d.fletcher * std::sqrt((d.tension * k2 + d.EI * k2 * k2) / d.rhoA);
}

// SPEC-STR "Losses": sigma_n = b1 + b2 sqrt(omega_n) + b3 omega_n^2, or the override table.
double modeSigma(const StringParams& p, int n, double omega) noexcept {
    const auto idx = static_cast<std::size_t>(n - 1);
    if (idx < p.sigmaOverride.size()) {
        return p.sigmaOverride[idx];
    }
    return p.b1 + p.b2 * std::sqrt(omega) + p.b3 * omega * omega;
}

// SPEC-STR "Polarisations": omega_h,n = omega_n 2^(delta_h/1200), same sigma_n.
double horizontalOmega(double omega, double detuneCents) noexcept {
    return omega * std::exp2(detuneCents / 1200.0);
}

bool bothPass(double omega, double sigma, double detuneCents, double fs, double fMax) noexcept {
    using osc::BandwidthVerdict;
    return osc::passesBandwidthRule(omega, sigma, fs, fMax) == BandwidthVerdict::Accepted &&
           osc::passesBandwidthRule(horizontalOmega(omega, detuneCents), sigma, fs, fMax) ==
               BandwidthVerdict::Accepted;
}

// sin(pi s) for s >= 0 without the rounding of pi * s: s is reduced exactly (fmod and the
// reflections are exact in binary floating point), so integer and half-integer s give exact
// 0 and +-1.
double sinPi(double s) noexcept {
    double a = std::fmod(s, 2.0);
    double sign = 1.0;
    if (a >= 1.0) {
        a -= 1.0;
        sign = -1.0;
    }
    if (a > 0.5) {
        a = 1.0 - a;
    }
    const double v = (a > 0.25) ? std::cos(kPi * (0.5 - a)) : std::sin(kPi * a);
    return sign * v;
}

// sin(x)/x with the series near 0.
double sinc(double x) noexcept {
    if (std::abs(x) < 1e-4) {
        const double x2 = x * x;
        return 1.0 - x2 / 6.0 + x2 * x2 / 120.0;
    }
    return std::sin(x) / x;
}

double raisedCosineFactor(double u) noexcept {
    // pi^2 - u^2 = (pi - u)(pi + u). Near u = pi use sin(u) = sin(pi - u):
    // W = sinc(pi - u) pi^2 / (u (pi + u)), finite at u = pi (W = 1/2).
    if (std::abs(u - kPi) < 0.5) {
        return sinc(kPi - u) * (kPi * kPi) / (u * (kPi + u));
    }
    return sinc(u) * (kPi * kPi) / ((kPi - u) * (kPi + u));
}

}  // namespace

Status validate(const StringParams& p) noexcept {
    // ADR-0006: InvalidArgument for non-physical input.
    if (!finitePositive(p.L) || !finitePositive(p.dCore) || !finitePositive(p.rhoCore) ||
        !finitePositive(p.ECore) || !finitePositive(p.f1Target)) {
        return Status::InvalidArgument;
    }
    if (!std::isfinite(p.dOuter) || p.dOuter < p.dCore) {
        return Status::InvalidArgument;
    }
    if (p.dOuter > p.dCore) {
        if (!finitePositive(p.rhoWind) || !std::isfinite(p.packing) || !(p.packing > 0.0) ||
            p.packing > 1.0) {
            return Status::InvalidArgument;
        }
    }
    if (!finiteNonNegative(p.epsWind) || !finiteNonNegative(p.b1) || !finiteNonNegative(p.b2) ||
        !finiteNonNegative(p.b3) || !std::isfinite(p.detuneH)) {
        return Status::InvalidArgument;
    }
    for (const double s : p.sigmaOverride) {
        if (!finiteNonNegative(s)) {
            return Status::InvalidArgument;
        }
    }
    return Status::Ok;
}

double linearDensity(const StringParams& p) noexcept {
    // SPEC-STR "Wound strings": rho A = pi/4 [rho_c d_c^2 + eta_w rho_w (d_o^2 - d_c^2)]
    const double core = p.rhoCore * p.dCore * p.dCore;
    double winding = 0.0;
    if (p.dOuter > p.dCore) {
        winding = p.packing * p.rhoWind * (p.dOuter * p.dOuter - p.dCore * p.dCore);
    }
    return 0.25 * kPi * (core + winding);
}

double bendingStiffness(const StringParams& p) noexcept {
    // SPEC-STR "Wound strings": EI = (1 + eps_w) pi E_c d_c^4 / 64
    const double d2 = p.dCore * p.dCore;
    return (1.0 + p.epsWind) * kPi * p.ECore * d2 * d2 / 64.0;
}

double tuningF1(const TuningCurve& curve, int key, double unisonDetuneCents) noexcept {
    // SPEC-STR "Tension for pitch" (SI-0004)
    double stretch = 0.0;
    if (key >= 0 && static_cast<std::size_t>(key) < curve.stretchCents.size()) {
        stretch = curve.stretchCents[static_cast<std::size_t>(key)];
    }
    return curve.referencePitch * std::exp2(static_cast<double>(key - 69) / 12.0) *
           std::exp2((stretch + unisonDetuneCents) / 1200.0);
}

double tensionForPitch(const StringParams& p, double f1) noexcept {
    // SPEC-STR "Tension for pitch": T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2
    const double rhoA = linearDensity(p);
    const double EI = bendingStiffness(p);
    return 4.0 * p.L * p.L * rhoA * f1 * f1 - kPi * kPi * EI / (p.L * p.L);
}

double f1ForTension(const StringParams& p, double tension) noexcept {
    // SPEC-STR "Tension for pitch" inverted (SI-0010 item 1)
    const double rhoA = linearDensity(p);
    const double EI = bendingStiffness(p);
    return std::sqrt((tension + kPi * kPi * EI / (p.L * p.L)) / (4.0 * p.L * p.L * rhoA));
}

int modeCount(const StringParams& p, double fs, double fMax) noexcept {
    Derived d;
    if (derive(p, fs, d) != Status::Ok || std::isnan(fMax)) {
        return 0;
    }
    int n = 0;
    while (n < kMaxModes) {
        const int m = n + 1;
        const double omega = modeOmega(d, modeK(p, m));
        if (!bothPass(omega, modeSigma(p, m, omega), p.detuneH, fs, fMax)) {
            break;
        }
        n = m;
    }
    return n;
}

double windowFactor(WindowKind kind, double k, double b) noexcept {
    if (kind == WindowKind::Point) {
        return 1.0;
    }
    const double u = std::abs(0.5 * k * b);  // SPEC-STR "Forces and observations": u = k b / 2
    if (kind == WindowKind::Rectangular) {
        return sinc(u);
    }
    return raisedCosineFactor(u);
}

void windowedShapes(const StringModes& modes, double x0, WindowKind kind, double b,
                    std::span<float> out) noexcept {
    const double amp = std::sqrt(2.0 / modes.L);
    const double t = x0 / modes.L;
    const std::size_t n = std::min(out.size(), modes.k.size());
    for (std::size_t i = 0; i < n; ++i) {
        // SPEC-STR "Forces and observations": phi-bar_n = phi_n(x0) W(k_n)
        const double phi = amp * sinPi(static_cast<double>(i + 1) * t);
        out[i] = static_cast<float>(phi * windowFactor(kind, modes.k[i], b));
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), 0.0F);
}

Status applyMute(StringModes& m, double muteRatio, double sigmaFelt, double detuneHCents) {
    if (!(muteRatio >= 0.0 && muteRatio <= 1.0) || !finiteNonNegative(sigmaFelt) ||
        !std::isfinite(detuneHCents)) {
        return Status::InvalidArgument;
    }
    const double sigmaMute = muteRatio * sigmaFelt;  // SPEC-STR "Duplex and rear segments"
    if (sigmaMute == 0.0) {
        return Status::Ok;
    }
    const std::size_t total = m.k.size();
    std::vector<double> sigma(total);
    std::vector<OscCoeffs> v;
    std::vector<OscCoeffs> h;
    v.reserve(total);
    h.reserve(total);
    for (std::size_t i = 0; i < total; ++i) {
        sigma[i] = m.sigma[i] + sigmaMute;
        OscCoeffs cv;
        OscCoeffs ch;
        if (compileOscillator(m.omega[i], sigma[i], m.fs, cv) != Status::Ok ||
            compileOscillator(horizontalOmega(m.omega[i], detuneHCents), sigma[i], m.fs, ch) !=
                Status::Ok) {
            break;  // overdamped: keep the prefix
        }
        v.push_back(cv);
        h.push_back(ch);
    }
    const std::size_t n = v.size();
    if (n == 0) {
        return Status::Rejected;
    }
    sigma.resize(n);
    m.sigma = std::move(sigma);
    m.oscV = std::move(v);
    m.oscH = std::move(h);
    m.k.resize(n);
    m.omega.resize(n);
    for (auto& row : m.phiBar) {
        row.resize(n);
    }
    return Status::Ok;
}

}  // namespace pme::string

namespace pme {

Status compileString(const StringParams& p, double fs, double fMax,
                     std::span<const double> registeredPoints, StringModes& out) {
    string::Derived d;
    if (const Status s = string::derive(p, fs, d); s != Status::Ok) {
        return s;
    }
    if (std::isnan(fMax)) {
        return Status::InvalidArgument;
    }
    for (const double x0 : registeredPoints) {
        if (!(x0 >= 0.0 && x0 <= p.L)) {  // also rejects NaN
            return Status::InvalidArgument;
        }
    }

    StringModes t;
    t.fs = fs;
    t.L = p.L;
    t.tension = d.tension;
    t.rhoA = d.rhoA;
    t.EI = d.EI;
    t.inharmonicity = d.inharmonicity;

    // SPEC-STR "Truncation": keep n = 1..N, N the largest index passing the bandwidth rule for
    // the vertical and the horizontal polarisation.
    for (int n = 1; n <= string::kMaxModes; ++n) {
        const double k = string::modeK(p, n);
        const double omega = string::modeOmega(d, k);
        const double sigma = string::modeSigma(p, n, omega);
        if (!string::bothPass(omega, sigma, p.detuneH, fs, fMax)) {
            break;
        }
        OscCoeffs cv;
        OscCoeffs ch;
        if (compileOscillator(omega, sigma, fs, cv) != Status::Ok ||
            compileOscillator(string::horizontalOmega(omega, p.detuneH), sigma, fs, ch) !=
                Status::Ok) {
            break;
        }
        t.k.push_back(k);
        t.omega.push_back(omega);
        t.sigma.push_back(sigma);
        t.oscV.push_back(cv);
        t.oscH.push_back(ch);
    }
    if (t.k.empty()) {
        return Status::Rejected;
    }

    // SPEC-STR "Contract" (SI-0001): point shapes phi_n(x0), W = 1, float64 then float32.
    const double amp = std::sqrt(2.0 / p.L);
    t.phiBar.reserve(registeredPoints.size());
    for (const double x0 : registeredPoints) {
        const double frac = x0 / p.L;
        std::vector<float> row(t.k.size());
        for (std::size_t i = 0; i < row.size(); ++i) {
            row[i] = static_cast<float>(amp * string::sinPi(static_cast<double>(i + 1) * frac));
        }
        t.phiBar.push_back(std::move(row));
    }
    out = std::move(t);
    return Status::Ok;
}

}  // namespace pme
