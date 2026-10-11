// SPDX-License-Identifier: MIT
// Covers: A2-R6
// Point shapes at registered points, the window factors and windowedShapes.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "fixtures.hpp"
#include "pme/string/string_model.hpp"
#include "pme/test/tolerance.hpp"

namespace {

constexpr double kPi = std::numbers::pi;
using pme::string::WindowKind;

// 60-digit mpmath 1.3.0 values of sin(u)/u and sin(u)/u * pi^2/(pi^2 - u^2) at the double u.
struct WRef {
    double u;
    double rect;
    double rc;
};
// NOLINTBEGIN(modernize-use-std-numbers): reference abscissae near pi, deliberately literal
constexpr std::array<WRef, 26> kWRef{{
    {1e-12, 1.0, 1.0},
    {1e-08, 0.99999999999999998333, 0.99999999999999999347},
    {1e-05, 0.99999999998333333333, 0.9999999999934654517},
    {9.99e-05, 0.99999999833666500083, 0.99999999934785142613},
    {0.00010001, 0.99999999833299998417, 0.99999999934641447243},
    {0.001, 0.99999983333334166667, 0.99999993465451868812},
    {0.1, 0.99833416646828152288, 0.99934671639001872629},
    {0.5, 0.95885107720840600055, 0.98377026923619412492},
    {1.0, 0.84147098480789650665, 0.93634229436760444897},
    {2.0, 0.4546487134128408477, 0.76448132382076622104},
    {3.0, 0.047040002686622407367, 0.53388209277868730429},
    {3.14, 0.00050721430461363953269, 0.50038023149985288149},
    {3.1415, 0.000029493423415721867524, 0.50002211946090229974},
    {3.141592643589793, 3.1830988916065525786e-9, 0.50000000238732416164},
    {3.141582653589793, 3.183108993995305999e-6, 0.50000238732467868399},
    {3.141592653589793, 3.8981718325193755985e-17, 0.50000000000000002924},
    {3.141592663589793, -3.1830987933788794461e-9, 0.4999999976126758979},
    {3.141602653589793, -3.1830887296806139617e-6, 0.49999761267638591495},
    {3.15, -0.0026689674181424183751, 0.49799327579148474807},
    {3.5, -0.10022377933989138518, 0.41554817784047396894},
    {4.0, -0.18920062382698206284, 0.3046027421041891585},
    {5.0, -0.19178485493262769378, 0.1251018610803294043},
    {7.0, 0.093855228388398441485, -0.023672491958991899437},
    {10.0, -0.05440211108893698134, 0.0059572279857874515821},
    {15.0, 0.043352522677141124389, -0.0019888972333336093547},
    {19.9, 0.043600206062395347351, -0.0011144049950391517772},
}};
// NOLINTEND(modernize-use-std-numbers)

double relErr(double a, double e) { return std::fabs(a - e) / std::fabs(e); }

}  // namespace

TEST_CASE("A2-R6 phiBar is exactly zero at the ends for every mode") {
    auto p = pme::string_test::mid();
    const std::array<double, 4> pts{0.0, p.L, 0.2137 * p.L, 0.5 * p.L};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    REQUIRE(m.phiBar.size() == 4);
    for (const auto& row : m.phiBar) {
        CHECK(row.size() == m.k.size());
    }
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        CHECK(m.phiBar[0][i] == 0.0F);
        CHECK(m.phiBar[1][i] == 0.0F);
    }
}

TEST_CASE("A2-R6 phiBar at the midpoint has zero even modes and unit-amplitude odd modes") {
    auto p = pme::string_test::mid();
    const std::array<double, 1> pts{0.5 * p.L};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    const double amp = std::sqrt(2.0 / p.L);
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const int n = static_cast<int>(i) + 1;
        if (n % 2 == 0) {
            CHECK(std::fabs(m.phiBar[0][i]) <= 1e-7 * amp);
        } else {
            const double sign = ((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
            CHECK(m.phiBar[0][i] == static_cast<float>(sign * amp));
        }
    }
}

TEST_CASE("A2-R6 phiBar float32 values are the rounded float64 values") {
    auto p = pme::string_test::bass();
    const double x0 = 0.2137 * p.L;
    const std::array<double, 1> pts{x0};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    const double amp = std::sqrt(2.0 / p.L);
    for (std::size_t i = 0; i < m.k.size(); ++i) {
        const auto n = static_cast<double>(i + 1);
        const double phi = amp * std::sin(n * kPi * (x0 / p.L));  // float64 reference
        const auto expected = static_cast<float>(phi);
        // Away from a node the float32 value is the correctly rounded float64 value.
        CHECK(std::fabs(static_cast<double>(m.phiBar[0][i]) - static_cast<double>(expected)) <=
              1.2e-7 * std::fabs(phi) + 1e-6 * amp * 1e-3);
    }
}

TEST_CASE("A2-R6 windowFactor point is 1 and limits at u = 0 and u = pi") {
    CHECK(pme::string::windowFactor(WindowKind::Point, 1234.0, 0.05) == 1.0);
    CHECK(pme::string::windowFactor(WindowKind::Rectangular, 0.0, 0.05) == 1.0);
    CHECK(pme::string::windowFactor(WindowKind::RaisedCosine, 0.0, 0.05) == 1.0);
    CHECK(pme::string::windowFactor(WindowKind::RaisedCosine, 100.0, 0.0) == 1.0);
    // u = k b / 2 = pi: rectangular -> 0, raised cosine -> 1/2.
    const double b = 0.02;
    const double k = 2.0 * kPi / b;
    CHECK(std::fabs(pme::string::windowFactor(WindowKind::Rectangular, k, b)) < 1e-15);
    PME_CHECK_REL(pme::string::windowFactor(WindowKind::RaisedCosine, k, b), 0.5, 1e-15);
    // Even in u.
    CHECK(pme::string::windowFactor(WindowKind::RaisedCosine, -3.0, b) ==
          pme::string::windowFactor(WindowKind::RaisedCosine, 3.0, b));
}

TEST_CASE("A2-R6 windowFactor matches 60-digit references within 1e-12") {
    double worstRect = 0.0;
    double worstRc = 0.0;
    double worstRcNearZero = 0.0;
    double worstRcNearPi = 0.0;
    for (const auto& r : kWRef) {
        // k = 2u, b = 1 gives u = k b / 2 exactly.
        const double rect = pme::string::windowFactor(WindowKind::Rectangular, 2.0 * r.u, 1.0);
        const double rc = pme::string::windowFactor(WindowKind::RaisedCosine, 2.0 * r.u, 1.0);
        INFO("u = " << r.u);
        if (std::fabs(r.u - kPi) > 1e-4) {  // rect has a zero at pi: relative error is undefined
            const double e = relErr(rect, r.rect);
            worstRect = std::max(worstRect, e);
            CHECK(e <= 1e-12);
        } else {
            CHECK(std::fabs(rect - r.rect) <= 1e-15);
        }
        const double e = relErr(rc, r.rc);
        worstRc = std::max(worstRc, e);
        if (r.u < 1e-3) {
            worstRcNearZero = std::max(worstRcNearZero, e);
        }
        if (std::fabs(r.u - kPi) < 1e-3) {
            worstRcNearPi = std::max(worstRcNearPi, e);
        }
        CHECK(e <= 1e-12);
    }
    MESSAGE("worst relative error: rect " << worstRect << ", raised cosine " << worstRc
                                          << " (near 0 " << worstRcNearZero << ", near pi "
                                          << worstRcNearPi << ")");
}

TEST_CASE("A2-R6 windowedShapes forms phi times W in float64") {
    auto p = pme::string_test::mid();
    const double x0 = 0.31 * p.L;
    const std::array<double, 1> pts{x0};
    pme::StringModes m;
    REQUIRE(pme::compileString(p, 48000.0, 16000.0, pts, m) == pme::Status::Ok);
    const std::size_t n = m.k.size();

    std::vector<float> point(n);
    pme::string::windowedShapes(m, x0, WindowKind::Point, 0.04, point);
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(point[i] == m.phiBar[0][i]);
    }
    for (const auto kind : {WindowKind::Rectangular, WindowKind::RaisedCosine}) {
        std::vector<float> w(n);
        pme::string::windowedShapes(m, x0, kind, 0.04, w);
        for (std::size_t i = 0; i < n; ++i) {
            const double W = pme::string::windowFactor(kind, m.k[i], 0.04);
            const double ref = static_cast<double>(m.phiBar[0][i]) * W;
            CHECK(std::fabs(static_cast<double>(w[i]) - ref) <= 2.5e-7 * std::fabs(ref) + 1e-9);
        }
    }
    // A short span is filled with the first modes; a long span is zero-padded.
    std::vector<float> shortSpan(5);
    pme::string::windowedShapes(m, x0, WindowKind::Point, 0.04, shortSpan);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(shortSpan[i] == m.phiBar[0][i]);
    }
    std::vector<float> longSpan(n + 3, 9.0F);
    pme::string::windowedShapes(m, x0, WindowKind::Point, 0.04, longSpan);
    CHECK(longSpan[n] == 0.0F);
    CHECK(longSpan[n + 2] == 0.0F);
    CHECK(longSpan[0] == m.phiBar[0][0]);
}
