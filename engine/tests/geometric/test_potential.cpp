// SPDX-License-Identifier: MIT
// Covers: A7-R1
// TensionModulationPotential (SPEC-GEO "Tension modulation"): value and gradient against finite
// differences over both polarisations, lossless energy, the drift rule and the shared psi of one
// segment (SPEC-NL "SAV formulation", "Drift control").
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

#include "pme/nonlinear/sav.hpp"
#include "pme/nonlinear/tension.hpp"
#include "test_support.hpp"

namespace {

using namespace pme::geometric_test;
using pme::Status;
using pme::nl::Potential;
using pme::nl::SavSegment;
using pme::nl::TensionModulationPotential;
using pme::string::GeometricConfig;

/// A second potential on the same segment: V = K/4 z_0^4 (stands for a hammer or a barrier).
class QuarticOnFirstMode final : public Potential<double> {
public:
    explicit QuarticOnFirstMode(double k) : k_(k) {}
    [[nodiscard]] double value(std::span<const double> z) const noexcept override {
        return 0.25 * k_ * z[0] * z[0] * z[0] * z[0];
    }
    void gradient(std::span<const double> z, std::span<double> grad) const noexcept override {
        for (double& g : grad) {
            g = 0.0;
        }
        grad[0] = k_ * z[0] * z[0] * z[0];
    }

private:
    double k_;
};

GeometricConfig tensionOnly(bool relaxation) {
    GeometricConfig c;
    c.tensionModulation = true;
    c.sav.driftRelaxation = relaxation;
    return c;
}

}  // namespace

TEST_CASE("A7-R1 value and gradient match finite differences over both polarisations") {
    const pme::StringModes m = compile(plainSteel(0.65, 262.0));
    REQUIRE(m.k.size() > 10);
    const std::size_t n = m.k.size();
    const double eaMinusT = 157.0e3 - m.tension;
    const double c = eaMinusT / (2.0 * m.L);
    std::vector<double> k2(n);
    for (std::size_t i = 0; i < n; ++i) {
        k2[i] = m.k[i] * m.k[i];
    }
    TensionModulationPotential<double> pot;
    REQUIRE(pot.configure(k2, c) == Status::Ok);

    Rng rng(11U);
    const std::size_t nz = 2 * n + 3;  // three lumped coordinates the potential must ignore
    std::vector<double> z(nz);
    for (double& v : z) {
        v = rng.uniform(-1.0, 1.0) * 1.0e-4;
    }
    std::vector<double> grad(nz, 123.0);  // must be overwritten, lumped entries set to 0
    pot.gradient(z, grad);
    double worst = 0.0;
    double gmax = 0.0;
    for (const double g : grad) {
        gmax = std::max(gmax, std::abs(g));
    }
    for (std::size_t i = 0; i < nz; ++i) {
        const double h = 1.0e-3 * 1.0e-4;
        std::vector<double> zp = z;
        std::vector<double> zm = z;
        zp[i] += h;
        zm[i] -= h;
        const double fd = (pot.value(zp) - pot.value(zm)) / (2.0 * h);
        worst = std::max(worst, std::abs(fd - grad[i]) / gmax);
        if (i >= 2 * n) {
            CHECK(grad[i] == 0.0);
        }
    }
    std::cout << "A7-R1 FD gradient: worst error relative to max |grad| = " << sci(worst) << "\n";
    CHECK(worst <= 1e-6);

    // V = (EA - T) S^2 / (8 L), Delta T = (EA - T) S / (2 L), dV/dq = Delta T k^2 q (SPEC-GEO).
    const std::span<const double> zs(z);
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s += k2[i] * (z[i] * z[i] + z[n + i] * z[n + i]);
    }
    CHECK(pot.sum(zs) == doctest::Approx(s).epsilon(1e-14));
    CHECK(pot.deltaT(zs) == doctest::Approx(eaMinusT * s / (2.0 * m.L)).epsilon(1e-13));
    CHECK(pot.value(zs) == doctest::Approx(eaMinusT * s * s / (8.0 * m.L)).epsilon(1e-13));
    CHECK(grad[3] == doctest::Approx(pot.deltaT(zs) * k2[3] * z[3]).epsilon(1e-13));
    CHECK(grad[n + 5] == doctest::Approx(pot.deltaT(zs) * k2[5] * z[n + 5]).epsilon(1e-13));
}

TEST_CASE("A7-R1 configure rejects bad input and the float instantiation agrees") {
    TensionModulationPotential<double> bad;
    const std::vector<double> ok{1.0, 4.0, 9.0};
    CHECK(bad.configure({}, 1.0) == Status::InvalidArgument);
    CHECK(bad.configure(ok, 0.0) == Status::InvalidArgument);
    CHECK(bad.configure(ok, -1.0) == Status::InvalidArgument);
    CHECK(bad.configure(ok, std::numeric_limits<double>::quiet_NaN()) == Status::InvalidArgument);
    const std::vector<double> negative{1.0, -4.0};
    CHECK(bad.configure(negative, 1.0) == Status::InvalidArgument);
    CHECK(bad.modes() == 0);  // unchanged

    TensionModulationPotential<float> pf;
    TensionModulationPotential<double> pd;
    REQUIRE(pf.configure(ok, 2.0) == Status::Ok);
    REQUIRE(pd.configure(ok, 2.0) == Status::Ok);
    CHECK(pf.modes() == 3);
    CHECK(pf.stiffness() == 2.0);
    const std::array<float, 6> zf{0.5F, -0.25F, 0.125F, 0.75F, 0.5F, -1.0F};
    std::array<double, 6> zd{};
    for (std::size_t i = 0; i < zf.size(); ++i) {
        zd[i] = static_cast<double>(zf[i]);
    }
    CHECK(pf.value(zf) == pd.value(zd));  // exactly representable inputs: same float64 sums
    std::array<double, 6> gf{};
    std::array<double, 6> gd{};
    pf.gradient(zf, gf);
    pd.gradient(zd, gd);
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(gf[i] == gd[i]);
    }
}

TEST_CASE("A7-R1 lossless transverse energy including psi is conserved to 1e-10 over 1e5 samples") {
    // Rigid bridge (w = 0), drift relaxation off (SPEC-NL "Drift control" 2 only removes energy).
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), tensionOnly(false));
    REQUIRE(b.status == Status::Ok);
    auto& seg = b.seg;
    REQUIRE(seg.usesSav());
    // A large pluck at 0.12 L, 30 degrees off vertical so both polarisations are loaded.
    trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.5, seg.qV(), seg.qH());
    seg.initialisePsi();
    seg.advance(0.0, 0.0);
    const double e0 = seg.transverseEnergy(0.0, 0.0, 0.0, 0.0);
    REQUIRE(e0 > 0.0);
    double drift = 0.0;
    for (int n = 1; n < 100000; ++n) {
        seg.advance(0.0, 0.0);
        drift = std::max(drift, std::abs(seg.transverseEnergy(0.0, 0.0, 0.0, 0.0) - e0) / e0);
    }
    const double vkc =
        seg.potential().value(std::span<const double>(seg.qV(), 2 * seg.modeCount()));
    std::cout << "A7-R1 energy: E0 = " << sci(e0) << " J, V_KC/E0 = " << sci(vkc / e0)
              << ", max relative drift over 1e5 samples = " << sci(drift) << "\n";
    CHECK(drift <= 1e-10);
    CHECK(seg.sav().counters().solvesSkipped == 0);  // V_KC > 0: the skip rule never applies
}

TEST_CASE("A7-R1 with the drift relaxation on the transverse energy never rises") {
    Built<double> b;
    build(b, plainSteel(0.65, 262.0), tensionOnly(true));
    REQUIRE(b.status == Status::Ok);
    auto& seg = b.seg;
    trianglePluck<double>(b.modes, 6.0e-3, 0.12 * b.modes.L, 0.5, seg.qV(), seg.qH());
    seg.initialisePsi();
    seg.advance(0.0, 0.0);
    double prev = seg.transverseEnergy(0.0, 0.0, 0.0, 0.0);
    const double e0 = prev;
    double worstRise = 0.0;
    for (int n = 1; n < 100000; ++n) {
        seg.advance(0.0, 0.0);
        const double e = seg.transverseEnergy(0.0, 0.0, 0.0, 0.0);
        worstRise = std::max(worstRise, (e - prev) / e0);
        prev = e;
    }
    std::cout << "A7-R1 relaxation: drift corrections = " << seg.sav().counters().driftCorrections
              << ", worst single-step relative rise = " << sci(worstRise) << "\n";
    CHECK(worstRise <= 1e-12);  // rounding only
    CHECK(prev <= e0 * (1.0 + 1e-10));
}

TEST_CASE("A7-R1 the tension potential shares psi with a second potential and stays rank 1") {
    const pme::StringModes m = compile(plainSteel(0.65, 262.0));
    const std::size_t n = m.k.size();
    std::vector<double> k2(n);
    for (std::size_t i = 0; i < n; ++i) {
        k2[i] = m.k[i] * m.k[i];
    }
    TensionModulationPotential<double> tension;
    REQUIRE(tension.configure(k2, (157.0e3 - m.tension) / (2.0 * m.L)) == Status::Ok);
    const QuarticOnFirstMode second(5.0e13);

    // maxDashpots = 0: the only rank-one term is the SAV pair, so a second psi would be
    // impossible; both potentials add into the single gradient and the one psi.
    pme::nl::SavParams params;
    params.driftRelaxation = false;
    SavSegment<double> seg;
    REQUIRE(seg.configure(2 * n, 0, 0, kFs, params) == Status::Ok);
    for (std::size_t i = 0; i < 2 * n; ++i) {
        pme::osc::OscCoeffsF64 c;
        const pme::OscCoeffs& o = (i < n) ? m.oscV[i] : m.oscH[i - n];
        const double dt = 1.0 / kFs;
        const double sd = o.sigmaHat * dt;
        c.beta = (1.0 - sd) / (1.0 + sd);
        c.kappa = o.omegaHat2 * dt * dt / (1.0 + sd);
        c.gamma = dt * dt / (1.0 + sd);
        c.omegaHat2 = o.omegaHat2;
        REQUIRE(seg.setCoordinate(i, c, m.rhoA) == Status::Ok);
    }
    trianglePluck<double>(m, 5.0e-3, 0.2 * m.L, 0.0, seg.q(), seg.q() + n);
    const std::array<const Potential<double>*, 2> pots{&tension, &second};
    seg.initialisePsi(pots);
    pme::nl::savStep<double>(seg, pots, {}, {});
    const double e0 = pme::nl::savEnergy(seg);
    double drift = 0.0;
    for (int s = 1; s < 50000; ++s) {
        pme::nl::savStep<double>(seg, pots, {}, {});
        drift = std::max(drift, std::abs(pme::nl::savEnergy(seg) - e0) / e0);
    }
    std::cout << "A7-R1 shared psi: max relative energy drift over 5e4 samples = " << sci(drift)
              << "\n";
    CHECK(drift <= 1e-10);
    CHECK(seg.counters().solvesSkipped == 0);
}
