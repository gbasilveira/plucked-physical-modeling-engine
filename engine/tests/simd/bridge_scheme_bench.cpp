// SPDX-License-Identifier: MIT
// Covers: A8-R2, A8-R3 (SI-0021: the cost of the two bridge schemes before G1)
// Cost per sample of one bridge point built from the A8 primitives, explicit and implicit scheme,
// each with the SIMD kernels and with the scalar references. A4's benchmark configuration: 3
// segments x 59 modes x 2 polarisations on one bridge point, 64 samples per call, 48 kHz. Nightly
// (perf).
//
//  explicit (SPEC-BRG "Discrete coupling order"):
//      dot(a, q) per bank -> scalar bridge advance -> force = -g delta_tt w by axpyNeg ->
//      stepModes with that force
//  implicit (SPEC-BRG "Bridge scheme"):
//      stepModes without the bridge force (gives b) -> dot(a, b) per bank -> scalar solve for
//      w^{n+1} -> axpyNeg on q and on u (q^{n+1} = b - h~ w^{n+1}, u^{n+1} likewise)
//
// The bridge model here is a scalar mass-spring-damper with fixed constants: the figure is the
// cost of the kernels and of the data flow, not a model of a real bridge.
#include <array>
#include <cstddef>
#include <numbers>

#include "pme/bench/bench.hpp"
#include "pme/simd/denormals.hpp"
#include "pme/simd/dispatch.hpp"
#include "test_support.hpp"

namespace {

constexpr std::size_t kSegments = 3;
constexpr std::size_t kPolarisations = 2;
constexpr std::size_t kBanks = kSegments * kPolarisations;
constexpr std::size_t kModes = 59;
constexpr int kSamplesPerCall = 64;
constexpr double kFs = 48000.0;

struct Bank {
    simdtest::Buf<float> q{kModes}, u{kModes}, beta{kModes}, kappa{kModes}, gamma{kModes};
    simdtest::Buf<float> a{kModes};       // force coefficients a_n
    simdtest::Buf<float> g{kModes};       // lifting overlaps g_n
    simdtest::Buf<float> hTilde{kModes};  // gamma g / dt^2 (implicit scheme)
    simdtest::Buf<float> force{kModes};
    simdtest::Buf<float> zero{kModes};
    float aH{0.0F};  // a^T h~
    Bank() = default;
};

struct Simd {
    static void step(Bank& b, const float* force) {
        pme::simd::stepModes(b.q.data(), b.u.data(), b.beta.data(), b.kappa.data(), b.gamma.data(),
                             force, kModes);
    }
    static float dot(const float* x, const float* y) { return pme::simd::dot(x, y, kModes); }
    static void axpyNeg(float* z, const float* a, const float* h, float c) {
        pme::simd::axpyNeg(z, a, h, c, kModes);
    }
};

struct Scalar {
    static void step(Bank& b, const float* force) {
        pme::simd::reference::stepModes(b.q.data(), b.u.data(), b.beta.data(), b.kappa.data(),
                                        b.gamma.data(), force, kModes);
    }
    static float dot(const float* x, const float* y) {
        return pme::simd::reference::dot(x, y, kModes);
    }
    static void axpyNeg(float* z, const float* a, const float* h, float c) {
        pme::simd::reference::axpyNeg(z, a, h, c, kModes);
    }
};

struct Point {
    std::array<Bank, kBanks> banks;
    double w{0.0}, wPrev{0.0}, wNext{0.0};  // bridge displacement n, n-1, n+1
    double dt{1.0 / kFs};
    Point() {
        simdtest::Rng rng(21);
        for (Bank& b : banks) {
            for (std::size_t n = 0; n < kModes; ++n) {
                pme::OscCoeffs c;
                const double f = 110.0 * static_cast<double>(n + 1) * rng.uniform(0.99, 1.01);
                if (pme::compileOscillator(2.0 * std::numbers::pi * f, rng.uniform(1.0, 5.0), kFs,
                                           c) != pme::Status::Ok) {
                    c = pme::OscCoeffs{0.99F, 0.01F, 1e-9F, 0.0, 0.0};
                }
                b.beta[n] = c.beta;
                b.kappa[n] = c.kappa;
                b.gamma[n] = c.gamma;
                b.a[n] = static_cast<float>(rng.uniform(-1.0, 1.0) * 1e-3);
                b.g[n] = static_cast<float>(rng.uniform(0.0, 1.0));
                b.hTilde[n] = static_cast<float>(static_cast<double>(c.gamma) * b.g[n] /
                                                 (1.0 / kFs) / (1.0 / kFs));
                b.q[n] = static_cast<float>(rng.uniform(-1.0, 1.0));
            }
        }
        // a^T h~ for the implicit solve, precomputed at compile time in the engine
        for (Bank& b : banks) {
            for (std::size_t n = 0; n < kModes; ++n) {
                b.aH += b.a[n] * b.hTilde[n];
            }
        }
    }
    void pluck() {  // keeps the amplitude steady so that nothing decays to denormals
        for (Bank& b : banks) {
            b.u[0] += 1e-3F;
        }
    }
};

template <class K>
void explicitSample(Point& p) {
    // 1. forces from q^n, w^n: F_b = sum_s a^T q - (T/L) w   (scalar term outside the kernel)
    float fb = 0.0F;
    for (Bank& b : p.banks) {
        fb += K::dot(b.a.data(), b.q.data());
    }
    fb -= 0.05F * static_cast<float>(p.w) * static_cast<float>(kBanks);
    // 2. bridge advance (scalar mass-spring-damper): w^{n+1}
    p.wNext =
        2.0 * p.w - p.wPrev + 0.5 * static_cast<double>(fb) - 1e-3 * p.w - 1e-3 * (p.w - p.wPrev);
    const double dtt = (p.wNext - 2.0 * p.w + p.wPrev) / (p.dt * p.dt);
    // 3. modal force -g delta_tt w by axpyNeg(zero - c g), then the difference-form update
    for (Bank& b : p.banks) {
        K::axpyNeg(b.force.data(), b.zero.data(), b.g.data(), static_cast<float>(dtt * 1e-9));
        K::step(b, b.force.data());
    }
    p.wPrev = p.w;
    p.w = p.wNext;
}

template <class K>
void implicitSample(Point& p) {
    // 3'. each bank: b = update without the bridge force; s = a^T b
    float s = 0.0F;
    float hSum = 0.0F;
    for (Bank& b : p.banks) {
        K::step(b, nullptr);
        s += K::dot(b.a.data(), b.q.data());
        hSum += b.aH;
    }
    // 4'. scalar solve: F^{n+1} is affine in w^{n+1}; one scalar equation for w^{n+1}
    const double denom =
        1.0 + 1e-9 * static_cast<double>(hSum) + 0.05 * static_cast<double>(kBanks) * 1e-3;
    p.wNext = (0.9 * p.w + 1e-3 * static_cast<double>(s)) / denom;
    // 5'. complete q^{n+1} = b - h~ w^{n+1} and u^{n+1} = u_b - h~ w^{n+1}
    const auto wn = static_cast<float>(p.wNext * 1e-9);
    for (Bank& b : p.banks) {
        K::axpyNeg(b.q.data(), b.q.data(), b.hTilde.data(), wn);
        K::axpyNeg(b.u.data(), b.u.data(), b.hTilde.data(), wn);
    }
    p.wPrev = p.w;
    p.w = p.wNext;
}

template <class K, bool kImplicit>
void run(Point& p) {
    const pme::simd::ScopedFlushDenormals flush;
    p.pluck();
    for (int s = 0; s < kSamplesPerCall; ++s) {
        if constexpr (kImplicit) {
            implicitSample<K>(p);
        } else {
            explicitSample<K>(p);
        }
    }
    pme::bench::do_not_optimize(p.banks[0].q[0]);
    pme::bench::do_not_optimize(p.w);
}

}  // namespace

PME_BENCHMARK(bridge_scheme_explicit_simd, "samples", kSamplesPerCall) {
    static Point p;
    run<Simd, false>(p);
}

PME_BENCHMARK(bridge_scheme_explicit_scalar, "samples", kSamplesPerCall) {
    static Point p;
    run<Scalar, false>(p);
}

PME_BENCHMARK(bridge_scheme_implicit_simd, "samples", kSamplesPerCall) {
    static Point p;
    run<Simd, true>(p);
}

PME_BENCHMARK(bridge_scheme_implicit_scalar, "samples", kSamplesPerCall) {
    static Point p;
    run<Scalar, true>(p);
}

int main(int argc, char** argv) {
    pme::simd::initialise();
    return pme::bench::run_main(argc, argv);
}
