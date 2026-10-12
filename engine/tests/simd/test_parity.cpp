// SPDX-License-Identifier: MIT
// Covers: A8-T1, A8-R1, A8-R2, A8-R3
// Parity of the mode-bank update, dot products and rank-one operations with the scalar references
// on every instruction set that is compiled in and that this CPU supports.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "pme/osc/mode_bank.hpp"
#include "test_support.hpp"

namespace {

using simdtest::Buf;
using simdtest::Rng;

constexpr double kTol = 1e-6;  // A8-T1: relative error bound

// Worst error per kernel and target, printed at the end of a test case for the report.
class WorstErrors {
public:
    void note(const std::string& kernel, const std::string& target, double e) {
        double& w = worst_[kernel + " / " + target];
        w = std::max(w, e);
    }
    void print(const char* label) const {
        for (const auto& [key, e] : worst_) {
            std::printf("[simd] %s worst error %s: %.3g\n", label, key.c_str(), e);
        }
    }

private:
    std::map<std::string, double> worst_;
};

}  // namespace

TEST_CASE("A8-T1 stepModes equals the scalar reference on every target") {
    WorstErrors worst;
    simdtest::forEachTarget("A8-T1 stepModes", [&](std::size_t, const char* name, bool) {
        for (const std::size_t n : simdtest::sizes()) {
            simdtest::Coefficients c(n);
            Rng rng(100 + n);
            for (int variant = 0; variant < 4; ++variant) {
                const bool withForce = (variant & 1) != 0;
                const bool withPrev = (variant & 2) != 0;
                Buf<float> q0(n);
                Buf<float> u0(n);
                Buf<float> force(n);
                simdtest::fillRandom(q0, rng, -1.0, 1.0);
                simdtest::fillRandom(u0, rng, -0.01, 0.01);
                simdtest::fillRandom(force, rng, -1e4, 1e4);
                Buf<float> qa(n);
                Buf<float> ua(n);
                Buf<float> qb(n);
                Buf<float> ub(n);
                Buf<float> qpa(n);
                Buf<float> upa(n);
                Buf<float> qpb(n);
                Buf<float> upb(n);
                qa.copyFrom(q0);
                qb.copyFrom(q0);
                ua.copyFrom(u0);
                ub.copyFrom(u0);
                const float* f = withForce ? force.data() : nullptr;
                // three consecutive steps, so that state feeds back
                for (int s = 0; s < 3; ++s) {
                    pme::simd::stepModes(qa.data(), ua.data(), c.beta.data(), c.kappa.data(),
                                         c.gamma.data(), f, n, withPrev ? qpa.data() : nullptr,
                                         withPrev ? upa.data() : nullptr);
                    pme::simd::reference::stepModes(qb.data(), ub.data(), c.beta.data(),
                                                    c.kappa.data(), c.gamma.data(), f, n,
                                                    withPrev ? qpb.data() : nullptr,
                                                    withPrev ? upb.data() : nullptr);
                }
                double err = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    // No fused arithmetic in stepModes: bit-identical on every target.
                    CHECK(qa[i] == qb[i]);
                    CHECK(ua[i] == ub[i]);
                    if (withPrev) {
                        CHECK(qpa[i] == qpb[i]);
                        CHECK(upa[i] == upb[i]);
                    }
                    err = std::max(err, std::abs(static_cast<double>(qa[i]) - qb[i]) /
                                            std::max(std::abs(static_cast<double>(qb[i])), 1e-30));
                }
                worst.note("stepModes f32", name, err);
                CHECK(err <= kTol);
            }
        }
    });
    worst.print("A8-T1");
}

TEST_CASE("A8-T1 stepModes in float64 equals the scalar reference on every target") {
    WorstErrors worst;
    simdtest::forEachTarget("A8-T1 stepModes f64", [&](std::size_t, const char* name, bool) {
        for (const std::size_t n : simdtest::sizes()) {
            simdtest::Coefficients c(n);
            Rng rng(200 + n);
            Buf<double> q0(n);
            Buf<double> u0(n);
            Buf<double> force(n);
            simdtest::fillRandom(q0, rng, -1.0, 1.0);
            simdtest::fillRandom(u0, rng, -0.01, 0.01);
            simdtest::fillRandom(force, rng, -1e4, 1e4);
            for (int coef32 = 0; coef32 < 2; ++coef32) {
                Buf<double> qa(n);
                Buf<double> ua(n);
                Buf<double> qb(n);
                Buf<double> ub(n);
                Buf<double> qpa(n);
                Buf<double> qpb(n);
                Buf<double> upa(n);
                Buf<double> upb(n);
                qa.copyFrom(q0);
                qb.copyFrom(q0);
                ua.copyFrom(u0);
                ub.copyFrom(u0);
                for (int s = 0; s < 3; ++s) {
                    if (coef32 != 0) {
                        pme::simd::stepModes(qa.data(), ua.data(), c.beta.data(), c.kappa.data(),
                                             c.gamma.data(), force.data(), n, qpa.data(),
                                             upa.data());
                        pme::simd::reference::stepModes(qb.data(), ub.data(), c.beta.data(),
                                                        c.kappa.data(), c.gamma.data(),
                                                        force.data(), n, qpb.data(), upb.data());
                    } else {
                        pme::simd::stepModes(qa.data(), ua.data(), c.beta64.data(),
                                             c.kappa64.data(), c.gamma64.data(), force.data(), n,
                                             qpa.data(), upa.data());
                        pme::simd::reference::stepModes(qb.data(), ub.data(), c.beta64.data(),
                                                        c.kappa64.data(), c.gamma64.data(),
                                                        force.data(), n, qpb.data(), upb.data());
                    }
                }
                double err = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    CHECK(qa[i] == qb[i]);
                    CHECK(ua[i] == ub[i]);
                    CHECK(qpa[i] == qpb[i]);
                    CHECK(upa[i] == upb[i]);
                    err = std::max(err, std::abs(qa[i] - qb[i]) / std::max(std::abs(qb[i]), 1e-30));
                }
                worst.note(coef32 != 0 ? "stepModes f64 coef f32" : "stepModes f64", name, err);
                CHECK(err <= kTol);
            }
        }
    });
    worst.print("A8-T1");
}

TEST_CASE("A8-R1 stepModes equals ModeBank step bit for bit") {
    // The A1 loop is the contract: pme::osc::ModeBank::step (scalar stepModes<float, float>).
    simdtest::forEachTarget("A8-R1 ModeBank", [&](std::size_t, const char*, bool) {
        for (const std::size_t n : {std::size_t{1}, std::size_t{17}, std::size_t{1024}}) {
            pme::osc::ModeBank bank;
            pme::osc::ModeBank twin;
            bank.resize(n);
            twin.resize(n);
            Rng rng(300 + n);
            for (std::size_t i = 0; i < n; ++i) {
                const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(n));
                pme::OscCoeffs c;
                REQUIRE(pme::compileOscillator(2.0 * std::numbers::pi * f, rng.uniform(0.5, 50.0),
                                               48000.0, c) == pme::Status::Ok);
                bank.setMode(i, c);
                twin.setMode(i, c);
                bank.q()[i] = twin.q()[i] = static_cast<float>(rng.uniform(-1.0, 1.0));
                bank.u()[i] = twin.u()[i] = static_cast<float>(rng.uniform(-0.01, 0.01));
            }
            Buf<float> force(n);
            Buf<float> qPrev(n);
            Buf<float> uPrev(n);
            std::vector<float> qPrevA1(n);
            std::vector<float> uPrevA1(n);
            for (int s = 0; s < 20; ++s) {
                // every third step has no force: A1 gets a zero vector, the kernel a null pointer
                const bool noForce = (s % 3) == 2;
                if (noForce) {
                    std::fill(force.data(), force.data() + n, 0.0F);
                } else {
                    simdtest::fillRandom(force, rng, -1e3, 1e3);
                }
                bank.step(std::span<const float>(force.data(), n), qPrevA1, uPrevA1);
                pme::simd::stepModes(twin.q(), twin.u(), twin.beta(), twin.kappa(), twin.gamma(),
                                     noForce ? nullptr : force.data(), n, qPrev.data(),
                                     uPrev.data());
                for (std::size_t i = 0; i < n; ++i) {
                    REQUIRE(twin.q()[i] == bank.q()[i]);
                    REQUIRE(twin.u()[i] == bank.u()[i]);
                    REQUIRE(qPrev[i] == qPrevA1[i]);
                    REQUIRE(uPrev[i] == uPrevA1[i]);
                }
            }
        }
    });
}

TEST_CASE("A8-T1 dot products equal the scalar reference on every target") {
    WorstErrors worst;
    simdtest::forEachTarget("A8-T1 dot", [&](std::size_t, const char* name, bool fused) {
        for (const std::size_t n : simdtest::sizes()) {
            Rng rng(400 + n);
            // mixed magnitudes; the bridge-force coefficients a_n span many decades
            for (int cancelling = 0; cancelling < 2; ++cancelling) {
                Buf<float> a(n);
                Buf<float> b(n);
                Buf<double> ad(n);
                Buf<double> bd(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const double lo = cancelling != 0 ? -1.0 : 0.1;
                    const double scale = std::pow(10.0, rng.uniform(-3.0, 3.0));
                    a[i] = static_cast<float>(rng.uniform(lo, 1.0) * scale);
                    b[i] = static_cast<float>(rng.uniform(lo, 1.0));
                    ad[i] = rng.uniform(lo, 1.0) * scale;
                    bd[i] = rng.uniform(lo, 1.0);
                }
                double sumAbs = 0.0;
                double sumAbsD = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    sumAbs += std::abs(static_cast<double>(a[i]) * b[i]);
                    sumAbsD += std::abs(ad[i] * bd[i]);
                }
                const float x = pme::simd::dot(a.data(), b.data(), n);
                const float xr = pme::simd::reference::dot(a.data(), b.data(), n);
                const double y = pme::simd::dot(ad.data(), bd.data(), n);
                const double yr = pme::simd::reference::dot(ad.data(), bd.data(), n);
                const double z = pme::simd::dotF64(a.data(), b.data(), n);
                const double zr = pme::simd::reference::dotF64(a.data(), b.data(), n);
                if (!fused) {
                    CHECK(x == xr);  // canonical order, unfused: bit-identical
                    CHECK(y == yr);
                }
                CHECK(z == zr);  // exact float products: bit-identical on every target
                const double ex = simdtest::reductionError(x, xr, sumAbs);
                const double ey = simdtest::reductionError(y, yr, sumAbsD);
                worst.note("dot f32", name, ex);
                worst.note("dot f64", name, ey);
                worst.note("dotF64", name, simdtest::reductionError(z, zr, sumAbs));
                CHECK(ex <= kTol);
                CHECK(ey <= kTol);
                // the float32 result is also close to the true sum (float64 reference)
                CHECK(simdtest::reductionError(x, pme::simd::reference::dotF64(a.data(), b.data(), n),
                                               sumAbs) <= kTol);
            }
        }
    });
    worst.print("A8-T1");
}

TEST_CASE("A8-R2 the canonical dot order is independent of the target") {
    // Collect the result of every target, then compare pairs: bit-identical unless one is fused.
    struct Entry {
        std::string name;
        bool fused;
        std::vector<float> f32;
        std::vector<double> f64;
        std::vector<double> mixed;
    };
    std::vector<Entry> entries;
    simdtest::forEachTarget("A8-R2 dot order", [&](std::size_t, const char* name, bool fused) {
        Entry e{name, fused, {}, {}, {}};
        for (const std::size_t n : simdtest::sizes()) {
            Rng rng(500 + n);
            Buf<float> a(n);
            Buf<float> b(n);
            Buf<double> ad(n);
            Buf<double> bd(n);
            simdtest::fillRandom(a, rng, -1.0, 1.0);
            simdtest::fillRandom(b, rng, -1.0, 1.0);
            simdtest::fillRandom(ad, rng, -1.0, 1.0);
            simdtest::fillRandom(bd, rng, -1.0, 1.0);
            e.f32.push_back(pme::simd::dot(a.data(), b.data(), n));
            e.f64.push_back(pme::simd::dot(ad.data(), bd.data(), n));
            e.mixed.push_back(pme::simd::dotF64(a.data(), b.data(), n));
        }
        entries.push_back(std::move(e));
    });
    REQUIRE(!entries.empty());
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const Entry& x = entries[0];
        const Entry& y = entries[i];
        CAPTURE(x.name);
        CAPTURE(y.name);
        for (std::size_t k = 0; k < x.f32.size(); ++k) {
            CHECK(x.mixed[k] == y.mixed[k]);
            if (!x.fused && !y.fused) {
                CHECK(x.f32[k] == y.f32[k]);
                CHECK(x.f64[k] == y.f64[k]);
            } else {
                // targets differ only through fused multiply-add contraction
                CHECK(std::abs(static_cast<double>(x.f32[k]) - y.f32[k]) <=
                      kTol * std::max(1.0, std::abs(static_cast<double>(x.f32[k]))) * 16.0);
                CHECK(std::abs(x.f64[k] - y.f64[k]) <= kTol * 16.0);
            }
        }
    }
}

TEST_CASE("A8-T1 rank-one operations equal the scalar reference on every target") {
    WorstErrors worst;
    simdtest::forEachTarget("A8-T1 rank one", [&](std::size_t, const char* name, bool fused) {
        for (const std::size_t n : simdtest::sizes()) {
            Rng rng(600 + n);
            Buf<float> w(n);
            Buf<float> a(n);
            Buf<float> h(n);
            Buf<double> wd(n);
            Buf<double> ad(n);
            Buf<double> hd(n);
            simdtest::fillRandom(w, rng, 0.0, 1.0);
            simdtest::fillRandom(h, rng, 0.0, 1.0);
            simdtest::fillRandom(a, rng, -1.0, 1.0);
            simdtest::fillRandom(wd, rng, 0.0, 1.0);
            simdtest::fillRandom(hd, rng, 0.0, 1.0);
            simdtest::fillRandom(ad, rng, -1.0, 1.0);
            float wa = 0.0F;
            float wh = 0.0F;
            float war = 0.0F;
            float whr = 0.0F;
            pme::simd::dot2(w.data(), a.data(), h.data(), n, wa, wh);
            pme::simd::reference::dot2(w.data(), a.data(), h.data(), n, war, whr);
            double sa = 0.0;
            double sh = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                sa += std::abs(static_cast<double>(w[i]) * a[i]);
                sh += std::abs(static_cast<double>(w[i]) * h[i]);
            }
            CHECK(simdtest::reductionError(wa, war, sa) <= kTol);
            CHECK(simdtest::reductionError(wh, whr, sh) <= kTol);
            double wad = 0.0;
            double whd = 0.0;
            double wadr = 0.0;
            double whdr = 0.0;
            pme::simd::dot2(wd.data(), ad.data(), hd.data(), n, wad, whd);
            pme::simd::reference::dot2(wd.data(), ad.data(), hd.data(), n, wadr, whdr);
            if (!fused) {
                CHECK(wa == war);
                CHECK(wh == whr);
                CHECK(wad == wadr);
                CHECK(whd == whdr);
            }
            // dot2 equals dot, element for element of the same order (fused or not: same kernel family)
            CHECK(simdtest::reductionError(wa, pme::simd::dot(w.data(), a.data(), n), sa) <= kTol);

            // axpyNeg, also in place
            const float c = 0.37F;
            Buf<float> z(n);
            Buf<float> zr(n);
            pme::simd::axpyNeg(z.data(), a.data(), h.data(), c, n);
            pme::simd::reference::axpyNeg(zr.data(), a.data(), h.data(), c, n);
            Buf<float> inPlace(n);
            inPlace.copyFrom(a);
            pme::simd::axpyNeg(inPlace.data(), inPlace.data(), h.data(), c, n);
            Buf<double> zd(n);
            Buf<double> zdr(n);
            pme::simd::axpyNeg(zd.data(), ad.data(), hd.data(), 0.37, n);
            pme::simd::reference::axpyNeg(zdr.data(), ad.data(), hd.data(), 0.37, n);
            double ez = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double scale = std::max(std::abs(static_cast<double>(zr[i])),
                                              (std::abs(static_cast<double>(a[i])) + c * h[i]) / 16.0);
                ez = std::max(ez, std::abs(static_cast<double>(z[i]) - zr[i]) / scale);
                CHECK(inPlace[i] == z[i]);
                if (!fused) {
                    CHECK(z[i] == zr[i]);
                    CHECK(zd[i] == zdr[i]);
                }
                CHECK(std::abs(zd[i] - zdr[i]) <= 1e-12);
            }
            worst.note("axpyNeg f32", name, ez);
            CHECK(ez <= kTol);

            // fused rank-one update, float32 and float64
            Buf<float> r(n);
            Buf<float> rr(n);
            r.copyFrom(a);
            rr.copyFrom(a);
            const float cc = pme::simd::rankOneUpdate(r.data(), r.data(), h.data(), w.data(), n);
            const float ccr = pme::simd::reference::rankOneUpdate(rr.data(), rr.data(), h.data(), w.data(), n);
            CHECK(std::abs(cc - ccr) <= kTol * std::max(std::abs(ccr), 1e-3F));
            Buf<double> rd(n);
            Buf<double> rdr(n);
            rd.copyFrom(ad);
            rdr.copyFrom(ad);
            const double cd = pme::simd::rankOneUpdate(rd.data(), rd.data(), hd.data(), wd.data(), n);
            const double cdr =
                pme::simd::reference::rankOneUpdate(rdr.data(), rdr.data(), hd.data(), wd.data(), n);
            CHECK(std::abs(cd - cdr) <= 1e-12 * std::max(std::abs(cdr), 1.0));
        }
    });
    worst.print("A8-T1");
}

TEST_CASE("A8-R3 rankOneUpdate equals the A3 lowRankSolve with r = 1 in float64") {
    simdtest::forEachTarget("A8-R3 lowRankSolve", [&](std::size_t, const char* name, bool) {
        double worst = 0.0;
        for (const std::size_t n : simdtest::sizes()) {
            for (int trial = 0; trial < 5; ++trial) {
                Rng rng(700 + n * 7 + static_cast<std::uint64_t>(trial));
                Buf<double> w(n);
                Buf<double> h(n);
                Buf<double> a(n);
                simdtest::fillRandom(w, rng, 0.0, 2.0);
                simdtest::fillRandom(h, rng, 0.0, 2.0);
                simdtest::fillRandom(a, rng, -1.0, 1.0);
                Buf<double> zSimd(n);
                Buf<double> zA3(n);
                zSimd.copyFrom(a);
                const double c = pme::simd::rankOneUpdate(zSimd.data(), zSimd.data(), h.data(), w.data(), n);
                const double* hp = h.data();
                const double* wp = w.data();
                pme::nl::lowRankSolve(zA3.data(), a.data(), n, 1, &hp, &wp);
                CHECK(std::isfinite(c));
                for (std::size_t i = 0; i < n; ++i) {
                    const double scale = std::abs(a[i]) + std::abs(h[i] * c) + 1e-300;
                    const double err = std::abs(zSimd[i] - zA3[i]) / std::max(std::abs(zA3[i]), scale / 16.0);
                    worst = std::max(worst, err);
                    CHECK(err <= 1e-12);
                }
            }
        }
        std::printf("[simd] A8-R3 worst relative error vs lowRankSolve on %s: %.3g\n", name, worst);
    });
}
