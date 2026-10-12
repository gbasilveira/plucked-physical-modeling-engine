// SPDX-License-Identifier: MIT
// Covers: A7-R2, A7-R3, A7-R4
// Longitudinal modes (SPEC-GEO "Longitudinal modes"), the pair-sum forcing and F_L (SPEC-GEO
// "Longitudinal force at the bridge").
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include "pme/osc/oscillator.hpp"
#include "pme/string/longitudinal.hpp"
#include "pme/string/string_model.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::BridgeEnd;
using pme::Status;
using pme::string::LongitudinalModes;

constexpr double kInf = std::numeric_limits<double>::infinity();

/// Compile longitudinal modes for a plain string; returns the status.
Status compileLong(const pme::StringParams& p, int ml, double b1, double b3, double fs, double fMax,
                   LongitudinalModes& lm, pme::StringModes* modesOut = nullptr) {
    const pme::StringModes m = compile(p, fMax < 1.0e5 ? fMax : 20000.0, fs);
    if (m.k.empty()) {
        return Status::Rejected;
    }
    if (modesOut != nullptr) {
        *modesOut = m;
    }
    return pme::string::compileLongitudinal(m, p, ml, b1, b3, fs, fMax, lm);
}

/// The SPEC-GEO pair sum by the direct double loop over all (i, j), both polarisations.
double directForce(const LongitudinalModes& lm, const std::vector<double>& qV,
                   const std::vector<double>& qH, std::size_t m) {
    double sum = 0.0;
    for (const auto* q : {&qV, &qH}) {
        for (std::size_t i = 1; i <= q->size(); ++i) {
            for (std::size_t j = 1; j <= q->size(); ++j) {
                const std::size_t diff = i > j ? i - j : j - i;
                if (i + j == m || diff == m) {
                    sum += lm.k[i - 1] * lm.k[j - 1] * (*q)[i - 1] * (*q)[j - 1];
                }
            }
        }
    }
    return lm.pairCoeff[m - 1] * sum;
}

/// Counts every read of a state value (the pair sum reads each q through a conversion to
/// double), so the O(N) claim can be tested without timing.
struct Counted {
    double v{0.0};
    static inline std::size_t reads = 0;
    explicit operator double() const noexcept {
        ++reads;
        return v;
    }
};

}  // namespace

TEST_CASE("A7-R2 omega_m^L equals K_m sqrt(EA/rhoA) and sigma_m uses its own b1 and b3") {
    const pme::StringParams p = plainSteel(0.65, 262.0);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 8, 0.5, 1.0e-9, kFs, 20000.0, lm, &m) == Status::Ok);
    REQUIRE(lm.kept() >= 1);
    // Independent evaluation: EA = E pi d^2/4, rhoA = rho pi d^2/4 for a plain string.
    const double ea = p.ECore * kPi * p.dCore * p.dCore / 4.0;
    const double rhoA = p.rhoCore * kPi * p.dCore * p.dCore / 4.0;
    CHECK(lm.EA == doctest::Approx(ea).epsilon(1e-14));
    CHECK(lm.rhoA == doctest::Approx(rhoA).epsilon(1e-14));
    double worst = 0.0;
    for (int i = 0; i < lm.kept(); ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const double K = static_cast<double>(i + 1) * kPi / p.L;
        const double w = K * std::sqrt(ea / rhoA);
        worst = std::max(worst, std::abs(lm.omega[idx] / w - 1.0));
        CHECK(lm.K[idx] == doctest::Approx(K).epsilon(1e-14));
        CHECK(lm.sigma[idx] == doctest::Approx(0.5 + 1.0e-9 * w * w).epsilon(1e-12));
        pme::osc::OscCoeffsF64 c;
        REQUIRE(pme::osc::compileOscillatorF64(lm.omega[idx], lm.sigma[idx], kFs, c) == Status::Ok);
        CHECK(lm.osc[idx].beta == c.beta);
        CHECK(lm.osc[idx].kappa == c.kappa);
        CHECK(lm.osc[idx].gamma == c.gamma);
        CHECK(lm.osc[idx].omegaHat2 == c.omegaHat2);
    }
    std::cout << "A7-R2 omega^L: worst relative deviation " << sci(worst) << ", kept " << lm.kept()
              << " of 8 (f_L1 = " << fix(lm.omega[0] / (2.0 * kPi), 1) << " Hz)\n";
    CHECK(worst <= 1e-12);
    CHECK(lm.eaMinusT == doctest::Approx(ea - m.tension).epsilon(1e-14));
    CHECK(lm.requested == 8);
}

TEST_CASE("A7-R2 the bandwidth rule truncates the longitudinal modes") {
    const pme::StringParams p = plainSteel(0.65, 262.0);
    const double fL1 = std::sqrt(p.ECore / p.rhoCore) / (2.0 * p.L);  // about 3.88 kHz
    struct Case {
        int ml;
        double fs;
        double fMax;
        double b1;
    };
    for (const Case& c :
         {Case{8, 48000.0, 20000.0, 0.0}, Case{8, 48000.0, kInf, 0.0}, Case{16, 96000.0, kInf, 0.0},
          Case{16, 96000.0, 10000.0, 0.0}, Case{3, 48000.0, 20000.0, 0.0}}) {
        CAPTURE(c.ml);
        CAPTURE(c.fs);
        CAPTURE(c.fMax);
        LongitudinalModes lm;
        REQUIRE(compileLong(p, c.ml, c.b1, 0.0, c.fs, c.fMax, lm) == Status::Ok);
        // SPEC-OSC Bandwidth with sigma = 0: omega_d dt <= 0.95 pi, i.e. f <= 0.475 fs, and f <=
        // fMax.
        const double limit = std::min(0.475 * c.fs, c.fMax);
        const int expected = std::min(c.ml, static_cast<int>(std::floor(limit / fL1 + 1e-9)));
        CHECK(lm.kept() == expected);
        CHECK(lm.requested == c.ml);
        CHECK(lm.osc.size() == lm.omega.size());
        CHECK(lm.pairCoeff.size() == lm.omega.size());
        CHECK(lm.bridgeCoeff.size() == lm.omega.size());
        std::cout << "A7-R2 truncation: M_L " << c.ml << ", fs " << c.fs << ", fMax " << c.fMax
                  << ": kept " << lm.kept() << "\n";
    }
    // A short treble string: the fundamental (25 kHz) is above the Nyquist margin: none kept.
    LongitudinalModes treble;
    REQUIRE(compileLong(plainSteel(0.1, 2000.0), 8, 0.0, 0.0, kFs, 20000.0, treble) == Status::Ok);
    CHECK(treble.kept() == 0);
    CHECK(treble.requested == 8);
    // An overdamped fundamental (sigma >= omega) stops the prefix at once.
    LongitudinalModes damped;
    REQUIRE(compileLong(p, 8, 5.0e4, 0.0, kFs, 20000.0, damped) == Status::Ok);
    CHECK(damped.kept() == 0);
    // M_L = 0 is an empty set that still carries the geometry constants.
    LongitudinalModes none;
    REQUIRE(compileLong(p, 0, 0.0, 0.0, kFs, 20000.0, none) == Status::Ok);
    CHECK(none.kept() == 0);
    CHECK(none.staticCoeff == doctest::Approx(0.5 * none.eaMinusT));
}

TEST_CASE("A7-R2 EA minus T not positive and bad arguments are rejected") {
    // f1 = 4.5 kHz on 0.65 m makes the transverse wave faster than the longitudinal one: T > EA.
    const pme::StringParams taut = plainSteel(0.65, 4500.0);
    const pme::StringModes mt = compile(taut, 20000.0);
    REQUIRE_FALSE(mt.k.empty());
    LongitudinalModes lm;
    lm.requested = 77;
    CHECK(pme::string::compileLongitudinal(mt, taut, 8, 0.0, 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(lm.requested == 77);  // unchanged

    const pme::StringParams p = plainSteel(0.65, 262.0);
    const pme::StringModes m = compile(p);
    CHECK(pme::string::compileLongitudinal(m, p, -1, 0.0, 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(m, p, 8, -1.0, 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(m, p, 8, 0.0, -1.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(m, p, 8, std::nan(""), 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(m, p, 8, 0.0, 0.0, 0.0, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(m, p, 8, 0.0, 0.0, kFs, std::nan(""), lm) ==
          Status::InvalidArgument);
    CHECK(pme::string::compileLongitudinal(pme::StringModes{}, p, 8, 0.0, 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    pme::StringParams badParams = p;
    badParams.L = -1.0;
    CHECK(pme::string::compileLongitudinal(m, badParams, 8, 0.0, 0.0, kFs, 20000.0, lm) ==
          Status::InvalidArgument);
    CHECK(lm.requested == 77);
}

TEST_CASE("A7-R2 a wound string uses the core's EA and the full linear mass") {
    const pme::StringParams p = woundBass(1.9, 27.5);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 4, 0.0, 0.0, kFs, 20000.0, lm, &m) == Status::Ok);
    const double eaCore = p.ECore * kPi * p.dCore * p.dCore / 4.0;
    const double rhoACore = p.rhoCore * kPi * p.dCore * p.dCore / 4.0;
    CHECK(lm.EA == doctest::Approx(eaCore).epsilon(1e-14));  // the winding carries no axial load
    CHECK(lm.rhoA == doctest::Approx(pme::string::linearDensity(p)).epsilon(1e-14));
    CHECK(lm.rhoA > 2.0 * rhoACore);  // the winding mass is in rhoA
    REQUIRE(lm.kept() >= 1);
    const double w1 = (kPi / p.L) * std::sqrt(eaCore / lm.rhoA);
    CHECK(lm.omega[0] == doctest::Approx(w1).epsilon(1e-13));
    // Slower than the core alone would be.
    CHECK(lm.omega[0] < (kPi / p.L) * std::sqrt(eaCore / rhoACore));
}

TEST_CASE("A7-R3 the pair sum equals the direct double sum over all pairs") {
    const pme::StringParams p = plainSteel(0.65, 262.0);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 16, 0.0, 0.0, 96000.0, kInf, lm, &m) == Status::Ok);
    REQUIRE(lm.kept() >= 8);
    const std::size_t n = m.k.size();
    Rng rng(3U);
    std::vector<double> qV(n);
    std::vector<double> qH(n);
    for (std::size_t i = 0; i < n; ++i) {
        qV[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
        qH[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
    }
    std::vector<double> f(static_cast<std::size_t>(lm.kept()));
    pme::string::longitudinalForce<double>(lm, qV, qH, f);
    double worst = 0.0;
    double fmax = 0.0;
    for (const double v : f) {
        fmax = std::max(fmax, std::abs(v));
    }
    for (std::size_t mi = 1; mi <= f.size(); ++mi) {
        worst = std::max(worst, std::abs(f[mi - 1] - directForce(lm, qV, qH, mi)) / fmax);
    }
    std::cout << "A7-R3 pair sum vs direct sum (" << f.size() << " modes, N = " << n
              << "): worst relative error " << sci(worst) << "\n";
    CHECK(worst <= 1e-12);

    // Single polarisation: an empty qH contributes nothing.
    std::vector<double> fv(f.size());
    pme::string::longitudinalForce<double>(lm, qV, std::span<const double>{}, fv);
    const std::vector<double> zeros(n, 0.0);
    for (std::size_t mi = 1; mi <= fv.size(); ++mi) {
        CHECK(fv[mi - 1] == doctest::Approx(directForce(lm, qV, zeros, mi)).epsilon(1e-12));
    }
    // A shorter output span fills only the first modes.
    std::vector<double> fs3(3, -1.0);
    pme::string::longitudinalForce<double>(lm, qV, qH, fs3);
    CHECK(fs3[2] == f[2]);
    // The float instantiation rounds the float64 sum once.
    std::vector<float> qVf(qV.begin(), qV.end());
    std::vector<float> qHf(qH.begin(), qH.end());
    std::vector<float> ff(f.size());
    pme::string::longitudinalForce<float>(lm, qVf, qHf, ff);
    for (std::size_t mi = 0; mi < f.size(); ++mi) {
        CHECK(static_cast<double>(ff[mi]) == doctest::Approx(f[mi]).epsilon(1e-5));
    }
}

TEST_CASE("A7-R3 the pair sum matches the quadrature of the modal projection of the PDE forcing") {
    // f_m = -(EA - T) / (2 rhoA) integral (y_x^2 + z_x^2) chi_m' dx, chi_m' = sqrt(2/L) K_m cos(K_m
    // x).
    const pme::StringParams p = plainSteel(0.65, 262.0);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 8, 0.0, 0.0, 96000.0, kInf, lm, &m) == Status::Ok);
    const std::size_t n = 24;
    Rng rng(5U);
    std::vector<double> qV(n);
    std::vector<double> qH(n);
    for (std::size_t i = 0; i < n; ++i) {
        qV[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
        qH[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
    }
    std::vector<double> f(static_cast<std::size_t>(lm.kept()));
    pme::string::longitudinalForce<double>(lm, std::span<const double>(qV),
                                           std::span<const double>(qH), f);
    // The pair sum above runs over all N modes; zero the rest by passing only the first n.
    const std::size_t quadPoints =
        4096;  // trapezoid rule: exact for these trigonometric integrands
    const double dx = m.L / static_cast<double>(quadPoints);
    const double shape = std::sqrt(2.0 / m.L);
    double worst = 0.0;
    double fmax = 0.0;
    for (const double v : f) {
        fmax = std::max(fmax, std::abs(v));
    }
    for (std::size_t mi = 1; mi <= f.size(); ++mi) {
        const double K = static_cast<double>(mi) * kPi / m.L;
        double integral = 0.0;
        for (std::size_t s = 0; s <= quadPoints; ++s) {
            const double x = static_cast<double>(s) * dx;
            double yx = 0.0;
            double zx = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double d = shape * m.k[i] * std::cos(m.k[i] * x);
                yx += qV[i] * d;
                zx += qH[i] * d;
            }
            const double weight = (s == 0 || s == quadPoints) ? 0.5 : 1.0;
            integral += weight * (yx * yx + zx * zx) * shape * K * std::cos(K * x);
        }
        integral *= dx;
        const double quad = -(lm.eaMinusT) / (2.0 * lm.rhoA) * integral;
        // The pair sum with only n modes populated: pad the unused modes with zeros.
        std::vector<double> qVp(m.k.size(), 0.0);
        std::vector<double> qHp(m.k.size(), 0.0);
        std::copy(qV.begin(), qV.end(), qVp.begin());
        std::copy(qH.begin(), qH.end(), qHp.begin());
        std::vector<double> fm(f.size());
        pme::string::longitudinalForce<double>(lm, std::span<const double>(qVp),
                                               std::span<const double>(qHp), fm);
        worst = std::max(worst, std::abs(fm[mi - 1] - quad) / fmax);
    }
    std::cout << "A7-R3 pair sum vs modal-projection quadrature: worst relative error "
              << sci(worst) << "\n";
    CHECK(worst <= 1e-9);
}

TEST_CASE("A7-R3 the pair sum costs O(N) reads per longitudinal mode") {
    const pme::StringParams p = plainSteel(0.65, 262.0);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 16, 0.0, 0.0, 96000.0, kInf, lm, &m) == Status::Ok);
    const std::size_t nFull = m.k.size();
    REQUIRE(nFull >= 50);
    std::vector<std::size_t> reads;
    for (const std::size_t n : {nFull / 2, nFull}) {
        std::vector<Counted> q(n, Counted{1.0e-6});
        std::size_t worstPerMode = 0;
        std::size_t total = 0;
        for (std::size_t mi = 1; mi <= static_cast<std::size_t>(lm.kept()); ++mi) {
            Counted::reads = 0;
            const double s = pme::string::detail::pairSum<Counted>(
                std::span<const double>(lm.k.data(), n), std::span<const Counted>(q), mi);
            CHECK(std::isfinite(s));
            worstPerMode = std::max(worstPerMode, Counted::reads);
            total += Counted::reads;
            CHECK(Counted::reads <= 2 * n + 2);  // at most about two reads per transverse mode
        }
        reads.push_back(total);
        std::cout << "A7-R3 operation count: N = " << n << ", worst reads per mode " << worstPerMode
                  << " (2N = " << 2 * n << ")\n";
    }
    // Doubling N doubles the total (linear), it does not quadruple it.
    const double ratio = static_cast<double>(reads[1]) / static_cast<double>(reads[0]);
    CHECK(
        ratio ==
        doctest::Approx(static_cast<double>(nFull) / static_cast<double>(nFull / 2)).epsilon(0.1));
}

TEST_CASE("A7-R3 the longitudinal modes are driven one way and leave the transverse modes alone") {
    using pme::string::GeometricConfig;
    Built<float> off;
    Built<float> on;
    GeometricConfig cOff;  // all features off
    GeometricConfig cOn;
    cOn.longitudinalModes = 8;  // kappa_L = kappa_T = 0, tension modulation off
    cOn.fMax = 20000.0;
    build(off, plainSteel(0.65, 262.0, 1.0e-3, 0.4), cOff);
    build(on, plainSteel(0.65, 262.0, 1.0e-3, 0.4), cOn);
    REQUIRE(off.status == Status::Ok);
    REQUIRE(on.status == Status::Ok);
    trianglePluck<float>(off.modes, 4.0e-3, 0.12 * off.modes.L, 0.3, off.seg.qV(), off.seg.qH());
    trianglePluck<float>(on.modes, 4.0e-3, 0.12 * on.modes.L, 0.3, on.seg.qV(), on.seg.qH());
    Rng rng(9U);
    bool identical = true;
    double xiMax = 0.0;
    for (int s = 0; s < 4000; ++s) {
        const auto acc = static_cast<float>(rng.uniform(-1.0, 1.0) * 1.0e3);  // moving support
        off.seg.advance(acc, 0.5F * acc);
        on.seg.advance(acc, 0.5F * acc);
        for (std::size_t i = 0; i < off.seg.modeCount(); ++i) {
            identical = identical && off.seg.qV()[i] == on.seg.qV()[i] &&
                        off.seg.uV()[i] == on.seg.uV()[i] && off.seg.qH()[i] == on.seg.qH()[i] &&
                        off.seg.uH()[i] == on.seg.uH()[i];
        }
        for (std::size_t m = 0; m < on.seg.longitudinalCount(); ++m) {
            xiMax = std::max(xiMax, std::abs(static_cast<double>(on.seg.xi()[m])));
        }
    }
    std::cout << "A7-R3 one-way: peak |xi| = " << sci(xiMax) << " with kept "
              << on.seg.longitudinalCount() << " modes\n";
    CHECK(identical);    // bit-identical transverse modes
    CHECK(xiMax > 0.0);  // the longitudinal modes did move
}

TEST_CASE("A7-R4 F_L for a static longitudinal shape and a slope matches the formula") {
    for (const BridgeEnd end : {BridgeEnd::AtL, BridgeEnd::At0}) {
        pme::StringParams p = plainSteel(0.65, 262.0);
        p.bridgeEnd = end;
        LongitudinalModes lm;
        pme::StringModes m;
        REQUIRE(compileLong(p, 5, 0.0, 0.0, kFs, 20000.0, lm, &m) == Status::Ok);
        REQUIRE(lm.kept() == 5);
        const std::size_t n = m.k.size();
        Rng rng(21U);
        std::vector<double> xi(5);
        for (double& v : xi) {
            v = rng.uniform(-1.0, 1.0) * 1.0e-6;
        }
        std::vector<double> qV(n);
        std::vector<double> qH(n);
        for (std::size_t i = 0; i < n; ++i) {
            qV[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
            qH[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
        }
        // Reference from the SPEC text: EA xi_x(xb) + (EA - T)/2 (y_x(xb)^2 + z_x(xb)^2),
        // xi_x(xb) = sum xi_m sqrt(2/L) K_m cos(K_m xb), the slopes likewise with cos(k_n xb).
        const double xb = end == BridgeEnd::AtL ? m.L : 0.0;
        const double ea = p.ECore * kPi * p.dCore * p.dCore / 4.0;
        const double shape = std::sqrt(2.0 / m.L);
        double xix = 0.0;
        for (std::size_t mi = 0; mi < xi.size(); ++mi) {
            const double K = static_cast<double>(mi + 1) * kPi / m.L;
            xix += xi[mi] * shape * K * std::cos(K * xb);
        }
        double yx = 0.0;
        double zx = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            yx += qV[i] * shape * m.k[i] * std::cos(m.k[i] * xb);
            zx += qH[i] * shape * m.k[i] * std::cos(m.k[i] * xb);
        }
        const double reference = ea * xix + 0.5 * (ea - m.tension) * (yx * yx + zx * zx);
        const double fl = pme::string::longitudinalBridgeForce<double>(lm, xi, qV, qH);
        CHECK(fl == doctest::Approx(reference).epsilon(1e-12));
        // The linear part alone (q = 0) and the quadratic part alone (xi = 0).
        const std::vector<double> zeros(n, 0.0);
        CHECK(pme::string::longitudinalBridgeForce<double>(lm, xi, zeros, zeros) ==
              doctest::Approx(ea * xix).epsilon(1e-12));
        const std::vector<double> noXi(5, 0.0);
        CHECK(pme::string::longitudinalBridgeForce<double>(lm, noXi, qV, qH) ==
              doctest::Approx(0.5 * (ea - m.tension) * (yx * yx + zx * zx)).epsilon(1e-12));
        // Without the horizontal polarisation the z_x term is absent.
        CHECK(pme::string::longitudinalBridgeForce<double>(lm, xi, qV, std::span<const double>{}) ==
              doctest::Approx(ea * xix + 0.5 * (ea - m.tension) * yx * yx).epsilon(1e-12));
    }
}

TEST_CASE("A7-R4 y_x(L) matches finite differences of y(x)") {
    const pme::StringParams p = plainSteel(0.65, 262.0);
    LongitudinalModes lm;
    pme::StringModes m;
    REQUIRE(compileLong(p, 4, 0.0, 0.0, kFs, 20000.0, lm, &m) == Status::Ok);
    const std::size_t n = 20;  // low modes: the central difference stays accurate
    Rng rng(33U);
    std::vector<double> q(m.k.size(), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        q[i] = rng.uniform(-1.0, 1.0) * 1.0e-4 / (1.0 + static_cast<double>(i));
    }
    const double shape = std::sqrt(2.0 / m.L);
    auto y = [&](double x) {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            s += q[i] * shape * std::sin(m.k[i] * x);
        }
        return s;
    };
    const double h = 1.0e-5 * m.L;
    const double fd = (y(m.L + h) - y(m.L - h)) / (2.0 * h);  // the series continues analytically
    const double yx = pme::string::slopeAtBridge<double>(lm, q);
    std::cout << "A7-R4 y_x(L): formula " << sci(yx) << ", finite difference " << sci(fd) << "\n";
    CHECK(yx == doctest::Approx(fd).epsilon(1e-7));
}
