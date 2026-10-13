// SPDX-License-Identifier: MIT
// Covers: A8-R1, A8-R2, A8-R3, A8-R4, A8-T3
// Throughput of every SIMD kernel on each instruction set this CPU supports and of the scalar
// reference. Nightly (perf). Its JSON (`--out`) is the stored baseline
// tools/bench/baselines/<machine-id>.json (docs/dev/simd.md, A8-T3).
//
// Benchmark names are <kernel>_<size>_<target>, where <target> is a Highway target name or
// "scalar" for the reference of the same kernel.
#include <cstddef>
#include <functional>
#include <string>

#include "pme/bench/bench.hpp"
#include "pme/simd/dispatch.hpp"
#include "test_support.hpp"

namespace {

constexpr std::size_t kN = 1024;  // modes / vector length
constexpr std::size_t kP = 100;   // bridge points
constexpr std::size_t kK = 4096;  // body modes
constexpr std::size_t kS = 32;    // samples per block

struct Fixture {
    simdtest::Coefficients co{kN};
    simdtest::Buf<float> q{kN}, u{kN}, force{kN}, w{kN}, a{kN}, h{kN}, z{kN};
    simdtest::Buf<double> ad{kN}, bd{kN};
    simdtest::Buf<float> projA{kK * kP}, projB{kP * kS}, projC{kK * kS};  // (K x P)(P x S)
    simdtest::Buf<float> velA{kP * kK}, velB{kK * kS}, velC{kP * kS};     // (P x K)(K x S)
    Fixture() {
        simdtest::Rng rng(11);
        simdtest::fillRandom(q, rng, -1.0, 1.0);
        simdtest::fillRandom(force, rng, 0.5, 1.0);
        simdtest::fillRandom(w, rng, 0.0, 1.0);
        simdtest::fillRandom(a, rng, -1.0, 1.0);
        simdtest::fillRandom(h, rng, 0.0, 1.0);
        simdtest::fillRandom(ad, rng, -1.0, 1.0);
        simdtest::fillRandom(bd, rng, -1.0, 1.0);
        simdtest::fillRandom(projA, rng, -1.0, 1.0);
        simdtest::fillRandom(projB, rng, -1.0, 1.0);
        simdtest::fillRandom(velA, rng, -1.0, 1.0);
        simdtest::fillRandom(velB, rng, -1.0, 1.0);
    }
};

Fixture& fx() {
    static Fixture f;
    return f;
}

// Make the dispatch select target `t` (or leave it alone for the scalar reference, t < 0).
void select(int t) {
    static int current = -2;
    if (t >= 0 && current != t) {
        pme::simd::testSelectTarget(static_cast<std::size_t>(t));
        current = t;
    }
}

struct Kernel {
    const char* name;
    const char* unit;
    double work;
    std::function<void()> simd;
    std::function<void()> scalar;
};

}  // namespace

int main(int argc, char** argv) {
    pme::simd::initialise();
    Fixture& f = fx();
    namespace ps = pme::simd;
    namespace ref = pme::simd::reference;
    float sinkF = 0.0F;
    double sinkD = 0.0;
    const std::size_t pk = kP * kK;
    const std::size_t ks = kK * kS;
    static_cast<void>(pk);
    static_cast<void>(ks);

    const std::vector<Kernel> kernels{
        {"stepModes_1024", "mode-samples", static_cast<double>(kN),
         [&] {
             ps::stepModes(f.q.data(), f.u.data(), f.co.beta.data(), f.co.kappa.data(),
                           f.co.gamma.data(), f.force.data(), kN);
         },
         [&] {
             ref::stepModes(f.q.data(), f.u.data(), f.co.beta.data(), f.co.kappa.data(),
                            f.co.gamma.data(), f.force.data(), kN);
         }},
        {"stepModesPrev_1024", "mode-samples", static_cast<double>(kN),
         [&] {
             ps::stepModes(f.q.data(), f.u.data(), f.co.beta.data(), f.co.kappa.data(),
                           f.co.gamma.data(), f.force.data(), kN, f.z.data(), nullptr);
         },
         [&] {
             ref::stepModes(f.q.data(), f.u.data(), f.co.beta.data(), f.co.kappa.data(),
                            f.co.gamma.data(), f.force.data(), kN, f.z.data(), nullptr);
         }},
        {"dot_f32_1024", "elements", static_cast<double>(kN),
         [&] { sinkF += ps::dot(f.a.data(), f.w.data(), kN); },
         [&] { sinkF += ref::dot(f.a.data(), f.w.data(), kN); }},
        {"dot_f64_1024", "elements", static_cast<double>(kN),
         [&] { sinkD += ps::dot(f.ad.data(), f.bd.data(), kN); },
         [&] { sinkD += ref::dot(f.ad.data(), f.bd.data(), kN); }},
        {"dotF64_1024", "elements", static_cast<double>(kN),
         [&] { sinkD += ps::dotF64(f.a.data(), f.w.data(), kN); },
         [&] { sinkD += ref::dotF64(f.a.data(), f.w.data(), kN); }},
        {"dot2_f32_1024", "elements", static_cast<double>(kN),
         [&] {
             float wa = 0.0F;
             float wh = 0.0F;
             ps::dot2(f.w.data(), f.a.data(), f.h.data(), kN, wa, wh);
             sinkF += wa + wh;
         },
         [&] {
             float wa = 0.0F;
             float wh = 0.0F;
             ref::dot2(f.w.data(), f.a.data(), f.h.data(), kN, wa, wh);
             sinkF += wa + wh;
         }},
        {"axpyNeg_f32_1024", "elements", static_cast<double>(kN),
         [&] { ps::axpyNeg(f.z.data(), f.a.data(), f.h.data(), 0.25F, kN); },
         [&] { ref::axpyNeg(f.z.data(), f.a.data(), f.h.data(), 0.25F, kN); }},
        {"rankOneUpdate_f32_1024", "elements", static_cast<double>(kN),
         [&] { sinkF += ps::rankOneUpdate(f.z.data(), f.a.data(), f.h.data(), f.w.data(), kN); },
         [&] { sinkF += ref::rankOneUpdate(f.z.data(), f.a.data(), f.h.data(), f.w.data(), kN); }},
        {"matmul_KxP_PxS_4096x100x32", "multiply-adds", static_cast<double>(kK * kP * kS),
         [&] {
             ps::matmul(f.projA.data(), kP, f.projB.data(), kS, f.projC.data(), kS, kK, kS, kP);
         },
         [&] {
             ref::matmul(f.projA.data(), kP, f.projB.data(), kS, f.projC.data(), kS, kK, kS, kP);
         }},
        {"matmul_PxK_KxS_100x4096x32", "multiply-adds", static_cast<double>(kP * kK * kS),
         [&] { ps::matmul(f.velA.data(), kK, f.velB.data(), kS, f.velC.data(), kS, kP, kS, kK); },
         [&] { ref::matmul(f.velA.data(), kK, f.velB.data(), kS, f.velC.data(), kS, kP, kS, kK); }},
    };

    auto& registry = pme::bench::registry();
    for (const Kernel& k : kernels) {
        registry.push_back(
            pme::bench::Benchmark{std::string(k.name) + "_scalar", k.unit, k.work, [fn = k.scalar] {
                                      fn();
                                      pme::bench::do_not_optimize(fx().z[0]);
                                  }});
        for (std::size_t t = 0; t < pme::simd::testTargetCount(); ++t) {
            registry.push_back(
                pme::bench::Benchmark{std::string(k.name) + "_" + pme::simd::testTargetName(t),
                                      k.unit, k.work, [fn = k.simd, t] {
                                          select(static_cast<int>(t));
                                          fn();
                                          pme::bench::do_not_optimize(fx().z[0]);
                                      }});
        }
    }
    const int rc = pme::bench::run_main(argc, argv);
    pme::bench::do_not_optimize(sinkF);
    pme::bench::do_not_optimize(sinkD);
    return rc;
}
