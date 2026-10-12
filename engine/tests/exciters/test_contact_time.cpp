// SPDX-License-Identifier: MIT
// Covers: A5-T1
// A5-T1: contact time of an elastic felt against a rigid wall scales as v^((1-p)/(1+p)), matches the
// SPEC-HAM closed form, and the hammer rebounds at -v.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "pme/nonlinear/sav.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

namespace {

struct Impact {
    double contactTime{0.0};  // s, zero crossings of delta interpolated
    double reboundSpeed{0.0};
};

// A free mass launched at v against RigidFeltPotential: m = 8 g, K = 4.5e9 N/m^p, mu_HC = 0.
Impact runRigidImpact(double fs, double p, double v) {
    const double dt = 1.0 / fs;
    nl::SavSegment<double> seg;
    REQUIRE(seg.configure(0, 1, 0, fs) == Status::Ok);
    REQUIRE(seg.setLumped(0, 8.0e-3) == Status::Ok);
    const RigidFeltPotential<double> pot(4.5e9, p, 0);
    const std::array<const nl::Potential<double>*, 1> pots{&pot};
    seg.q()[0] = -v * dt;  // SPEC-HAM Velocity map: launched at the gap v dt
    seg.u()[0] = v * dt;
    double prev = seg.q()[0];
    double start = -1.0;
    double end = -1.0;
    for (int n = 1; n < 200000 && end < 0.0; ++n) {
        nl::savStep<double>(seg, pots, {}, {});
        const double d = seg.q()[0];  // delta at sample n
        if (start < 0.0 && d > 0.0) {
            start = (n - 1) + (-prev) / (d - prev);
        } else if (start >= 0.0 && d <= 0.0) {
            end = (n - 1) + prev / (prev - d);
        }
        prev = d;
    }
    REQUIRE(end > 0.0);
    nl::savStep<double>(seg, pots, {}, {});
    Impact out;
    out.contactTime = (end - start) * dt;
    out.reboundSpeed = seg.u()[0] / dt;
    return out;
}

// SPEC-HAM Oracle: t_c = 2 delta_m / v sqrt(pi) Gamma(1 + 1/(p+1)) / Gamma(1/2 + 1/(p+1)).
double oracleContactTime(double p, double v) {
    const double m = 8.0e-3;
    const double K = 4.5e9;
    const double deltaM = std::pow((p + 1.0) * m * v * v / (2.0 * K), 1.0 / (p + 1.0));
    return 2.0 * deltaM / v * std::sqrt(std::numbers::pi) * std::tgamma(1.0 + 1.0 / (p + 1.0)) /
           std::tgamma(0.5 + 1.0 / (p + 1.0));
}

}  // namespace

TEST_CASE("A5-T1 contact time scales as v^((1-p)/(1+p)) within 2 percent at 48 and 96 kHz") {
    struct Case {
        double fs;
        double p;
    };
    const std::array<Case, 3> cases{{{48000.0, 2.5}, {48000.0, 3.5}, {96000.0, 2.5}}};
    const std::array<double, 5> speeds{0.5, 1.0, 2.0, 4.0, 6.0};
    for (const Case& c : cases) {
        const double exponent = (1.0 - c.p) / (1.0 + c.p);
        const Impact ref = runRigidImpact(c.fs, c.p, 1.0);
        for (const double v : speeds) {
            const Impact r = runRigidImpact(c.fs, c.p, v);
            const double ratio = r.contactTime / ref.contactTime / std::pow(v, exponent);
            MESSAGE("fs " << c.fs << " p " << c.p << " v " << v << " t_c " << sci(r.contactTime, 6)
                          << " s ratio-1 " << sci(ratio - 1.0)
                          << " rebound/v+1 " << sci(r.reboundSpeed / v + 1.0));
            CHECK(std::abs(ratio - 1.0) <= 0.02);
            CHECK(r.reboundSpeed / (-v) == doctest::Approx(1.0).epsilon(1e-3));  // rebounds at -v
        }
    }
}

TEST_CASE("A5-T1 absolute contact time matches the SPEC-HAM closed form at 3 m/s p = 2.5") {
    for (const double fs : {48000.0, 96000.0}) {
        const Impact r = runRigidImpact(fs, 2.5, 3.0);
        const double oracle = oracleContactTime(2.5, 3.0);
        const double tolerance = std::max(0.01 * oracle, 1.0 / fs);
        MESSAGE("fs " << fs << " t_c " << sci(r.contactTime, 6) << " oracle " << sci(oracle, 6)
                      << " relative error " << sci(r.contactTime / oracle - 1.0));
        CHECK(std::abs(r.contactTime - oracle) <= tolerance);
    }
}
