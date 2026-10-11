// SPDX-License-Identifier: MIT
// Covers: ORC-08
//
// ORC-08 Impact time (SPEC-ORC; SPEC-NL "Discrete scheme", SPEC-HAM "Oracle").
//
// Setup: mass 8 g at 3 m/s on a rigid contact V(z) = K/(p+1) [z - z_w]_+^(p+1), p = 2.5, starting
// 1 mm before the wall (A3: rigid-wall test potential; A5 repeats the test with the hammer
// potential). One free lumped coordinate: omegaHat = sigmaHat = 0, so beta = 1, kappa = 0,
// gamma = dt^2; no dashpot; epsilon_0 = 1e-12 J.
// Expected: t_c = 2 (delta_m / v) sqrt(pi) Gamma(1 + 1/(p+1)) / Gamma(1/2 + 1/(p+1)),
// delta_m = ((p+1) m v^2 / (2K))^(1/(p+1)), computed here in float64 with std::tgamma; the rebound
// speed is -v.
// Tolerance: |t_c_hat - t_c| <= max(1 % of t_c, 1 sample); |v_out / (-v) - 1| <= 0.001 (SPEC-ORC;
// SPEC's own run reached 0.4 % on t_c and an exact rebound).
//
// Cases: K = 2.7608e9 N/m^p (t_c = 1.000 ms) and K = 2.1569e7 N/m^p (t_c = 4.000 ms), fs = 48 kHz
// and 96 kHz, float32 and float64 state types: eight TEST_CASEs.
//
// Method: contact time is the span between the interpolated entry and exit crossings of z = z_w
// (V > 0 exactly between them); the rebound is the half-step velocity u/dt after the psi reset.
// Recorded, not pass criteria: relative energy drift of m u^2/(2 dt^2) + psi^2/2 - eps0 during
// contact, and the number of psi resets (expected exactly 1: separation).
//
// STEPPERS. Every case runs through ReferenceSavStepper<T> (SPEC-NL formulas written in this file)
// and through A3Stepper<T> (the engine's pme::nl::savStep, same Wall as a Potential), with the same
// tolerances. The scenario is templated on a Stepper type with
//   Stepper(double m, double dt, double eps0, const Wall& wall, double z0, double v0)
//   void step();                 // one sample, n -> n+1
//   double z() const;            // z^n (position, metres)
//   double u() const;            // u^n = z^n - z^(n-1) (metres per sample)
//   double psi() const;
//   int resets() const;          // times the separation reset fired

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <pme/nonlinear/sav.hpp>
#include <span>

namespace {

constexpr double kMass = 0.008;  // kg
constexpr double kSpeed = 3.0;   // m/s
constexpr double kP = 2.5;
constexpr double kEps0 = 1e-12;     // J
constexpr double kStartGap = 1e-3;  // m before the wall
constexpr double kPi = std::numbers::pi;

/// Rigid-wall test potential V(z) = K/(p+1) [z - zw]_+^(p+1), wall at z = 0.
struct Wall {
    double k;
    [[nodiscard]] double value(double z) const {
        return z > 0.0 ? k / (kP + 1.0) * std::pow(z, kP + 1.0) : 0.0;
    }
    [[nodiscard]] double gradient(double z) const { return z > 0.0 ? k * std::pow(z, kP) : 0.0; }
};

/// SPEC-HAM "Oracle": contact time of mass m at speed v on K [delta]_+^p.
double analyticContactTime(double k) {
    const double a = 1.0 / (kP + 1.0);
    const double deltaM = std::pow((kP + 1.0) * kMass * kSpeed * kSpeed / (2.0 * k), a);
    return 2.0 * (deltaM / kSpeed) * std::sqrt(kPi) * std::tgamma(1.0 + a) / std::tgamma(0.5 + a);
}

/// SPEC-NL scheme for one free coordinate, written from the formulas in the specification:
///   z^(n+1) - 2 z^n + z^(n-1) = -(dt^2/m) g^n (psi^(n+1/2) + psi^(n-1/2)) / 2
///   psi^(n+1/2) = psi^(n-1/2) + (1/2) g^n (z^(n+1) - z^(n-1)),  g = dV/dz / sqrt(2 (V + eps0)).
/// Solving the two for u^(n+1) = z^(n+1) - z^n (difference form, beta = 1, kappa = 0):
///   u^(n+1) (1 + c g^2/4) = u^n (1 - c g^2/4) - c g psi^(n-1/2),  c = dt^2/m
/// (the rank-1 Woodbury solve for N = 1). Skip rule: V^n = 0 gives the free update. Reset rule:
/// V^n = V^(n-1) = 0 sets psi to sqrt(2 eps0). T is the stored state type (SPEC-CONV: float32 at
/// run time); the scalar solve runs in float64 and the stored increment is rounded to T.
template <typename T>
class ReferenceSavStepper {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SPEC-NL order
    ReferenceSavStepper(double m, double dt, double eps0, const Wall& wall, double z0, double v0)
        : m_(m),
          dt_(dt),
          eps0_(eps0),
          wall_(wall),
          z_(static_cast<T>(z0)),
          u_(static_cast<T>(v0 * dt)),
          psi_(std::sqrt(2.0 * eps0)) {}

    void step() {
        const auto z = static_cast<double>(z_);
        const double v = wall_.value(z);
        auto uNext = static_cast<double>(u_);
        if (v > 0.0) {
            const double c = dt_ * dt_ / m_;
            const double g = wall_.gradient(z) / std::sqrt(2.0 * (v + eps0_));
            const double cg2 = c * g * g / 4.0;
            uNext = (static_cast<double>(u_) * (1.0 - cg2) - c * g * psi_) / (1.0 + cg2);
            psi_ += 0.5 * g * (uNext + static_cast<double>(u_));
            inContact_ = true;
        } else if (prevV_ == 0.0 && inContact_) {
            psi_ = std::sqrt(2.0 * eps0_);
            inContact_ = false;
            ++resets_;
        }
        prevV_ = v;
        u_ = static_cast<T>(uNext);
        z_ = static_cast<T>(z + static_cast<double>(u_));
    }
    [[nodiscard]] double z() const { return static_cast<double>(z_); }
    [[nodiscard]] double u() const { return static_cast<double>(u_); }
    [[nodiscard]] double psi() const { return psi_; }
    [[nodiscard]] int resets() const { return resets_; }

private:
    double m_, dt_, eps0_;
    Wall wall_;
    T z_, u_;
    double psi_;
    double prevV_ = 0.0;
    bool inContact_ = false;
    int resets_ = 0;
};

/// The same wall as an A3 Potential (SPEC-NL "Contracts"), one lumped coordinate.
template <typename T>
class WallPotential final : public pme::nl::Potential<T> {
public:
    explicit WallPotential(const Wall& w) : wall_(w) {}
    [[nodiscard]] double value(std::span<const T> z) const noexcept override {
        return wall_.value(static_cast<double>(z[0]));
    }
    void gradient(std::span<const T> z, std::span<double> grad) const noexcept override {
        grad[0] = wall_.gradient(static_cast<double>(z[0]));
    }

private:
    Wall wall_;
};

/// The engine's savStep (A3) driving one free lumped mass; same interface as the reference.
template <typename T>
class A3Stepper {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SPEC-NL order
    A3Stepper(double m, double dt, double eps0, const Wall& wall, double z0, double v0)
        : potential_(wall) {
        pme::nl::SavParams params;
        params.eps0 = eps0;
        const bool configured = seg_.configure(0, 1, 0, 1.0 / dt, params) == pme::Status::Ok;
        const bool lumped = seg_.setLumped(0, m) == pme::Status::Ok;
        REQUIRE(configured);
        REQUIRE(lumped);
        seg_.q()[0] = static_cast<T>(z0);
        seg_.u()[0] = static_cast<T>(v0 * dt);
    }
    void step() {
        const std::array<const pme::nl::Potential<T>*, 1> pots{&potential_};
        pme::nl::savStep<T>(
            seg_, std::span<const pme::nl::Potential<T>* const>(pots.data(), pots.size()), {}, {});
    }
    [[nodiscard]] double z() const { return static_cast<double>(seg_.q()[0]); }
    [[nodiscard]] double u() const { return static_cast<double>(seg_.u()[0]); }
    [[nodiscard]] double psi() const { return seg_.psi(); }
    [[nodiscard]] int resets() const { return static_cast<int>(seg_.counters().resets); }

private:
    pme::nl::SavSegment<T> seg_;
    WallPotential<T> potential_;
};

struct Measured {
    double contactTime = 0.0;
    double rebound = 0.0;  // v_out in m/s
    double energyDrift = 0.0;
    int resets = 0;
    bool separated = false;
};

template <typename Stepper>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): (K, fs)
Measured runCase(double k, double fs) {
    const double dt = 1.0 / fs;
    const Wall wall{k};
    Stepper s(kMass, dt, kEps0, wall, -kStartGap, kSpeed);
    const double e0 = 0.5 * kMass * kSpeed * kSpeed;
    Measured out;
    double tIn = -1.0;
    double tOut = -1.0;
    const auto maxSteps = static_cast<long>(0.03 * fs);  // 30 ms covers 4 ms contact + flight back
    for (long n = 0; n < maxSteps; ++n) {
        const double zn = s.z();
        s.step();
        const double znext = s.z();
        const double un = s.u();
        // Crossings of the wall at z = 0 (before the step the sample was z^n, after it z^(n+1)).
        if (tIn < 0.0 && zn <= 0.0 && znext > 0.0) {
            tIn = static_cast<double>(n) + (-zn) / (znext - zn);
        } else if (tIn >= 0.0 && tOut < 0.0 && zn > 0.0 && znext <= 0.0) {
            tOut = static_cast<double>(n) + zn / (zn - znext);
        }
        if (tIn >= 0.0 && tOut < 0.0) {
            const double e = 0.5 * kMass * (un / dt) * (un / dt) + 0.5 * s.psi() * s.psi() - kEps0;
            out.energyDrift = std::max(out.energyDrift, std::fabs(e - e0) / e0);
        }
        if (tOut >= 0.0 && s.resets() > 0) {
            out.rebound = un / dt;
            out.separated = true;
            break;
        }
    }
    out.resets = s.resets();
    if (tIn >= 0.0 && tOut >= 0.0) {
        out.contactTime = (tOut - tIn) * dt;
    }
    return out;
}

template <typename Stepper>
void checkCase(const char* label, double k, double fs) {
    const double tc = analyticContactTime(k);
    const Measured m = runCase<Stepper>(k, fs);
    REQUIRE(m.separated);
    const double tolTime = std::max(0.01 * tc, 1.0 / fs);
    const double errTime = std::fabs(m.contactTime - tc);
    const double errReb = std::fabs(m.rebound / (-kSpeed) - 1.0);
    std::fprintf(stderr,  // NOLINT(cppcoreguidelines-pro-type-vararg)
                 "[ORC-08] %s K=%.4e fs=%.0f: t_c expected %.9e measured %.9e err %.3e s "
                 "(%.4f %%, %.4f samples; tol %.3e s); rebound %.9f m/s err %.3e (tol 1e-3); "
                 "energy drift %.3e; resets %d\n",
                 label, k, fs, tc, m.contactTime, errTime, 100.0 * errTime / tc, errTime * fs,
                 tolTime, m.rebound, errReb, m.energyDrift, m.resets);
    CHECK(errTime <= tolTime);
    CHECK(errReb <= 1e-3);
    CHECK(m.resets == 1);
}

constexpr double kK1ms = 2.7607996651603e9;  // t_c = 1.000 ms for m = 8 g, v = 3 m/s, p = 2.5
constexpr double kK4ms = 2.1568747384e7;     // t_c = 4.000 ms

}  // namespace

TEST_CASE("ORC-08 sanity analytic contact times of the chosen K") {
    CHECK(analyticContactTime(kK1ms) == doctest::Approx(1.0e-3).epsilon(1e-6));
    CHECK(analyticContactTime(kK4ms) == doctest::Approx(4.0e-3).epsilon(1e-6));
}

TEST_CASE("ORC-08 impact time rigid wall float32 48 kHz 1 ms") {
    checkCase<ReferenceSavStepper<float>>("float32", kK1ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float32 48 kHz 4 ms") {
    checkCase<ReferenceSavStepper<float>>("float32", kK4ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float32 96 kHz 1 ms") {
    checkCase<ReferenceSavStepper<float>>("float32", kK1ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float32 96 kHz 4 ms") {
    checkCase<ReferenceSavStepper<float>>("float32", kK4ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float64 48 kHz 1 ms") {
    checkCase<ReferenceSavStepper<double>>("float64", kK1ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float64 48 kHz 4 ms") {
    checkCase<ReferenceSavStepper<double>>("float64", kK4ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float64 96 kHz 1 ms") {
    checkCase<ReferenceSavStepper<double>>("float64", kK1ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall float64 96 kHz 4 ms") {
    checkCase<ReferenceSavStepper<double>>("float64", kK4ms, 96000.0);
}

// The same eight cases through the engine's savStep (A3).
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float32 48 kHz 1 ms") {
    checkCase<A3Stepper<float>>("A3 float32", kK1ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float32 48 kHz 4 ms") {
    checkCase<A3Stepper<float>>("A3 float32", kK4ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float32 96 kHz 1 ms") {
    checkCase<A3Stepper<float>>("A3 float32", kK1ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float32 96 kHz 4 ms") {
    checkCase<A3Stepper<float>>("A3 float32", kK4ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float64 48 kHz 1 ms") {
    checkCase<A3Stepper<double>>("A3 float64", kK1ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float64 48 kHz 4 ms") {
    checkCase<A3Stepper<double>>("A3 float64", kK4ms, 48000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float64 96 kHz 1 ms") {
    checkCase<A3Stepper<double>>("A3 float64", kK1ms, 96000.0);
}
TEST_CASE("ORC-08 impact time rigid wall A3 savStep float64 96 kHz 4 ms") {
    checkCase<A3Stepper<double>>("A3 float64", kK4ms, 96000.0);
}
