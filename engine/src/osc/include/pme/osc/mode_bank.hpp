// SPDX-License-Identifier: MIT
// Structure-of-arrays mode bank, scalar difference-form kernel, derived quantities and discrete
// energy (A1-R3, A1-R4, A1-R5). The scalar kernel is the reference that A8 vectorises.
//
// SPEC sections implemented: SPEC-OSC "Difference form", "Derived quantities", "Discrete energy";
// SPEC-CONV (float64 compile time, float32 run-time states).
#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <span>

#include "pme/osc.hpp"

namespace pme::osc {

/// Alignment in bytes of every ModeBank array (one cache line; whole AVX-512 vectors).
inline constexpr std::size_t kBankAlignment = 64;
/// Lane granularity of the padded length: arrays hold a multiple of 16 floats.
inline constexpr std::size_t kBankLane = 16;

/// Deleter for 64-byte-aligned arrays.
struct AlignedDeleter {
    void operator()(void* p) const noexcept { ::operator delete(p, std::align_val_t{kBankAlignment}); }
};

/// Zero-initialised, 64-byte-aligned array of trivially destructible T. Allocation happens only
/// in `allocate` (non-real-time).
template <class T>
class AlignedArray {
public:
    /// Replace the storage by `count` zeroed elements. Thread: non-audio. Allocates: yes.
    void allocate(std::size_t count) {
        void* raw = ::operator new(count * sizeof(T), std::align_val_t{kBankAlignment});
        T* typed = static_cast<T*>(raw);
        for (std::size_t i = 0; i < count; ++i) {
            typed[i] = T{};
        }
        data_.reset(typed);
        count_ = count;
    }
    [[nodiscard]] T* data() noexcept { return data_.get(); }
    [[nodiscard]] const T* data() const noexcept { return data_.get(); }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    std::unique_ptr<T[], AlignedDeleter> data_;
    std::size_t count_{0};
};

/// Scalar reference kernel of the difference form, one sample for every mode i < n.
///
/// SPEC-OSC difference form: u^{n+1} = beta u^n - kappa q^n + gamma f^n, q^{n+1} = q^n + u^{n+1}.
/// The arithmetic order is fixed (Numerics rule 2): (beta u - kappa q) + gamma f. `S` is the
/// state type (float in the engine, double in tests); coefficients are converted to `S` first.
/// If `qPrevOut` / `uPrevOut` are non-null they receive q^n / u^n (the values before the step),
/// from which callers form the centred quantities. Thread: audio. Allocates: no.
template <class S, class C>
void stepModes(S* q, S* u, const C* beta, const C* kappa, const C* gamma, const S* force,
               std::size_t n, S* qPrevOut = nullptr, S* uPrevOut = nullptr) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const S qn = q[i];
        const S un = u[i];
        if (qPrevOut != nullptr) {
            qPrevOut[i] = qn;
        }
        if (uPrevOut != nullptr) {
            uPrevOut[i] = un;
        }
        const S uNext = static_cast<S>(beta[i]) * un - static_cast<S>(kappa[i]) * qn +
                        static_cast<S>(gamma[i]) * force[i];
        u[i] = uNext;
        q[i] = qn + uNext;
    }
}

/// N modes in structure-of-arrays form (A1-R3). q, u, beta, kappa, gamma are float32;
/// omegaHat2 and sigmaHat are float64. Every array is 64-byte aligned and padded to a multiple
/// of 16 elements; padding lanes hold beta = kappa = gamma = 0 and zero state, and stay zero.
class ModeBank {
public:
    /// Allocate for `modes` modes, all state and coefficients zero. Thread: non-audio.
    /// Allocates: yes.
    void resize(std::size_t modes);

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t paddedSize() const noexcept { return padded_; }

    [[nodiscard]] float* q() noexcept { return q_.data(); }
    [[nodiscard]] float* u() noexcept { return u_.data(); }
    [[nodiscard]] float* beta() noexcept { return beta_.data(); }
    [[nodiscard]] float* kappa() noexcept { return kappa_.data(); }
    [[nodiscard]] float* gamma() noexcept { return gamma_.data(); }
    [[nodiscard]] double* omegaHat2() noexcept { return omegaHat2_.data(); }
    [[nodiscard]] double* sigmaHat() noexcept { return sigmaHat_.data(); }
    [[nodiscard]] const float* q() const noexcept { return q_.data(); }
    [[nodiscard]] const float* u() const noexcept { return u_.data(); }
    [[nodiscard]] const float* beta() const noexcept { return beta_.data(); }
    [[nodiscard]] const float* kappa() const noexcept { return kappa_.data(); }
    [[nodiscard]] const float* gamma() const noexcept { return gamma_.data(); }
    [[nodiscard]] const double* omegaHat2() const noexcept { return omegaHat2_.data(); }
    [[nodiscard]] const double* sigmaHat() const noexcept { return sigmaHat_.data(); }

    /// Set all five coefficients of mode `i` (< size()); the state is kept. Thread: any.
    void setMode(std::size_t i, const OscCoeffs& c) noexcept;
    /// Set beta, kappa, gamma of mode `i`; omegaHat2 and sigmaHat and the state are kept.
    /// Used per sample while a coefficient ramp is running (A1-R6).
    void setCoefficients(std::size_t i, float beta, float kappa, float gamma) noexcept;
    /// Zero q and u of every mode (not the padding's coefficients, which are always zero).
    void clearState() noexcept;

    /// One scalar difference-form sample for every mode (SPEC-OSC difference form).
    /// `force` holds the per-mode force per unit modal mass and must have size() elements.
    /// `qPrev` / `uPrev`, if non-empty, must have size() elements and receive q^n / u^n.
    /// Thread: audio. Allocates: no.
    void step(std::span<const float> force, std::span<float> qPrev = {},
              std::span<float> uPrev = {}) noexcept;

private:
    AlignedArray<float> q_, u_, beta_, kappa_, gamma_;
    AlignedArray<double> omegaHat2_, sigmaHat_;
    std::size_t count_{0};
    std::size_t padded_{0};
};

/// Half-step velocity v^{n+1/2} = u^{n+1} / dt (SPEC-OSC derived quantities), float64.
[[nodiscard]] inline double halfStepVelocity(double uNext, double dt) noexcept {
    return uNext / dt;
}

/// Centred velocity delta_t. q^n = (u^{n+1} + u^n) / (2 dt) (SPEC-OSC derived quantities).
[[nodiscard]] inline double centredVelocity(double uNext, double uPrev, double dt) noexcept {
    return (uNext + uPrev) / (2.0 * dt);
}

/// Centred acceleration delta_tt q^n = (u^{n+1} - u^n) / dt^2 (SPEC-OSC derived quantities).
[[nodiscard]] inline double centredAcceleration(double uNext, double uPrev, double dt) noexcept {
    return (uNext - uPrev) / (dt * dt);
}

/// Discrete energy of one unit-mass mode (SPEC-OSC discrete energy):
/// H^{n+1/2} = 1/2 (u^{n+1}/dt)^2 + 1/2 omegaHat^2 q^n q^{n+1}. Callers multiply by the modal mass.
[[nodiscard]] inline double modeEnergy(double omegaHat2, double qPrev, double qNext,
                                       double uNext, double dt) noexcept {
    const double v = uNext / dt;
    return 0.5 * v * v + 0.5 * omegaHat2 * qPrev * qNext;
}

/// Right-hand side of the energy balance (SPEC-OSC discrete energy):
/// H^{n+1/2} - H^{n-1/2} = dt [ -2 sigmaHat (delta_t. q^n)^2 + f^n delta_t. q^n ].
/// Returns the bracket times dt, for the step that produced uNext from uPrev with force `force`.
[[nodiscard]] inline double energyBalance(double sigmaHat, double force, double uNext,
                                          double uPrev, double dt) noexcept {
    const double v = centredVelocity(uNext, uPrev, dt);
    return dt * (-2.0 * sigmaHat * v * v + force * v);
}

/// Sum of modeEnergy over modes 0..n-1 in index order, accumulated in float64 (A1-R5).
/// `qPrev` holds q^n and the bank holds q^{n+1}, u^{n+1}.
[[nodiscard]] double bankEnergy(const ModeBank& bank, std::span<const float> qPrev,
                                double dt) noexcept;

/// Sum of energyBalance over modes 0..n-1 in index order, in float64 (A1-R5).
[[nodiscard]] double bankEnergyBalance(const ModeBank& bank, std::span<const float> force,
                                       std::span<const float> uPrev, double dt) noexcept;

}  // namespace pme::osc
