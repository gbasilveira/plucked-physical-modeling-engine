// SPDX-License-Identifier: MIT
// Covers: A5-R2
// HammerPotential: value and gradient against finite differences (one and three strings, both
// approach signs), exact zero out of contact, window and strike position, and the shank's
// two-entry hammer direction.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "pme/exciters/hammer.hpp"
#include "pme/string/string_model.hpp"
#include "test_support.hpp"

using namespace pme;
using namespace pme::exciters;
using namespace pme::exciters_test;

namespace {

struct Rig {
    HammerPotential<double> pot;
    std::vector<double> z;
    std::size_t nz{0};
};

// Random geometry: `ns` strings of 8 modes, hammer coordinate(s) after them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): ns, sE
Rig makeRig(std::size_t ns, double sE, bool shank, std::uint64_t seed) {
    std::mt19937_64 eng(seed);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    constexpr std::size_t kModes = 8;
    Rig r;
    std::vector<StringSlice> slices(ns);
    for (std::size_t s = 0; s < ns; ++s) {
        slices[s].offset = s * kModes;
        for (std::size_t n = 0; n < kModes; ++n) {
            slices[s].phiBar.push_back(static_cast<float>(4.0 * uni(eng)));
        }
    }
    const std::size_t hi = ns * kModes;
    r.nz = hi + (shank ? 2 : 1);
    REQUIRE(r.pot.configure(std::move(slices), hi, shank ? 0.2 : 0.0, sE) == Status::Ok);
    StrikeMap map;
    map.strings = ns;
    for (std::size_t s = 0; s < ns; ++s) {
        map.weight[s] = 0.5 + 0.5 * (uni(eng) * 0.5 + 0.5);
        map.stiffness[s] = 2.0e9 * (1.0 + 0.3 * uni(eng));
        map.exponent[s] = 2.5 + 0.4 * uni(eng);
    }
    r.pot.setStrikeMap(map);
    r.pot.setActive(true);
    r.z.assign(r.nz, 0.0);
    for (std::size_t i = 0; i < hi; ++i) {
        r.z[i] = 1.0e-4 * uni(eng);
    }
    // Put the hammer well inside every string's contact.
    double top = -1.0;
    for (std::size_t s = 0; s < ns; ++s) {
        top = std::max(top, r.pot.stringPosition(r.z, s));
    }
    r.z[hi] = top + 1.0e-3;
    if (shank) {
        r.z[hi + 1] = 5.0e-4;
    }
    return r;
}

void checkGradient(Rig& r) {
    std::vector<double> grad(r.nz, 0.0);
    r.pot.gradient(r.z, grad);
    const double h = 1.0e-8;
    for (std::size_t i = 0; i < r.nz; ++i) {
        std::vector<double> zp = r.z;
        std::vector<double> zm = r.z;
        zp[i] += h;
        zm[i] -= h;
        const double fd = (r.pot.value(zp) - r.pot.value(zm)) / (2.0 * h);
        const double scale = std::max(std::abs(fd), std::abs(grad[i]));
        INFO("coordinate " << i << " analytic " << sci(grad[i]) << " finite difference "
                           << sci(fd));
        CHECK(std::abs(grad[i] - fd) <= 1e-6 * std::max(scale, 1e-3));
    }
}

}  // namespace

TEST_CASE("A5-R2 V_H and its gradient match finite differences for one and three strings") {
    std::uint64_t seed = 1;
    for (const std::size_t ns : {std::size_t{1}, std::size_t{3}}) {
        for (const double sE : {+1.0, -1.0}) {
            Rig r = makeRig(ns, sE, false, seed++);
            REQUIRE(r.pot.value(r.z) > 0.0);
            checkGradient(r);
        }
    }
}

TEST_CASE("A5-R2 the shank's two hammer entries follow x_H = X + lever Y") {
    for (const std::size_t ns : {std::size_t{1}, std::size_t{3}}) {
        Rig r = makeRig(ns, -1.0, true, 99 + ns);
        const std::size_t hi = ns * 8;
        CHECK(r.pot.headPosition(r.z) == doctest::Approx(r.z[hi] + 0.2 * r.z[hi + 1]));
        std::vector<double> grad(r.nz, 0.0);
        r.pot.gradient(r.z, grad);
        CHECK(grad[hi + 1] == doctest::Approx(0.2 * grad[hi]).epsilon(1e-14));
        checkGradient(r);
    }
}

TEST_CASE("A5-R2 value is exactly zero out of contact and when inactive") {
    Rig r = makeRig(3, 1.0, false, 5);
    const std::size_t hi = 24;
    REQUIRE(r.pot.value(r.z) > 0.0);
    // Move the hammer to the lowest string position: every delta <= 0.
    double bottom = 1.0;
    double top = -1.0;
    for (std::size_t s = 0; s < 3; ++s) {
        bottom = std::min(bottom, r.pot.stringPosition(r.z, s));
        top = std::max(top, r.pot.stringPosition(r.z, s));
    }
    r.z[hi] = bottom;
    CHECK(r.pot.value(r.z) == 0.0);
    r.z[hi] = bottom - 1e-3;
    CHECK(r.pot.value(r.z) == 0.0);
    std::vector<double> grad(r.nz, 1.0);
    r.pot.gradient(r.z, grad);
    for (const double g : grad) {
        CHECK(g == 0.0);
    }
    r.z[hi] = top + 1e-3;
    REQUIRE(r.pot.value(r.z) > 0.0);
    r.pot.setActive(false);
    CHECK(r.pot.value(r.z) == 0.0);
    CHECK(r.pot.hammerGradient(r.z) == 0.0);
}

TEST_CASE("A5-R2 a string with weight zero adds no potential") {
    Rig r = makeRig(3, 1.0, false, 8);
    const std::size_t hi = 24;
    StrikeMap map = r.pot.map();
    // Only string 1 is below the hammer; strings 0 and 2 are far above it but have weight 0 anyway.
    map.weight[0] = 0.0;
    map.weight[2] = 0.0;
    r.pot.setStrikeMap(map);
    const double d1 = r.pot.penetration(r.z, 1);
    REQUIRE(d1 > 0.0);
    const double expected = map.weight[1] * map.stiffness[1] / (map.exponent[1] + 1.0) *
                            std::pow(d1, map.exponent[1] + 1.0);
    CHECK(r.pot.value(r.z) == doctest::Approx(expected).epsilon(1e-13));
    std::vector<double> grad(r.nz, 0.0);
    r.pot.gradient(r.z, grad);
    for (std::size_t n = 0; n < 8; ++n) {
        CHECK(grad[n] == 0.0);
        CHECK(grad[16 + n] == 0.0);
    }
    CHECK(grad[hi] > 0.0);
}

TEST_CASE("A5-R2 the window and the strike position are those of windowedShapes") {
    const std::vector<StringModes> modes = unison(3, true);
    HammerParams h = pianoHammer();
    h.strikeRatio = 0.11;
    h.width = 0.012;
    StruckNote<double> note;
    REQUIRE(note.configure(describe(modes, h), kFs, nullptr) == Status::Ok);
    for (std::size_t s = 0; s < 3; ++s) {
        std::vector<float> expected(modes[s].k.size(), 0.0F);
        string::windowedShapes(modes[s], 0.11 * modes[s].L, string::WindowKind::RaisedCosine, 0.012,
                               expected);
        const auto& got = note.potential().slice(s).phiBar;
        REQUIRE(got.size() == expected.size());
        for (std::size_t n = 0; n < got.size(); ++n) {
            CHECK(got[n] == expected[n]);  // bit-identical
        }
        CHECK(note.modeOffset(s) == note.potential().slice(s).offset);
    }
    // delta_s = x_H - s_E sum phi-bar q: set one mode of string 1 and read it back.
    note.segment().q()[note.modeOffset(1) + 2] = 1.0e-3F;
    const std::span<const double> z(note.segment().q(), note.segment().size());
    const double expected = 1.0 * static_cast<double>(note.potential().slice(1).phiBar[2]) *
                            static_cast<double>(note.segment().q()[note.modeOffset(1) + 2]);
    CHECK(note.potential().stringPosition(z, 1) == doctest::Approx(expected).epsilon(1e-14));
    CHECK(note.potential().stringPosition(z, 0) == 0.0);
}

TEST_CASE("A5-R2 an approach sign of minus one mirrors the string position") {
    const std::vector<StringModes> modes = unison(1, true);
    StruckNoteDesc d = describe(modes, pianoHammer());
    StruckNote<double> plus;
    REQUIRE(plus.configure(d, kFs, nullptr) == Status::Ok);
    d.approachSign = -1.0;
    StruckNote<double> minus;
    REQUIRE(minus.configure(d, kFs, nullptr) == Status::Ok);
    plus.segment().q()[3] = 2.0e-4;
    minus.segment().q()[3] = 2.0e-4;
    const std::span<const double> zp(plus.segment().q(), plus.segment().size());
    const std::span<const double> zm(minus.segment().q(), minus.segment().size());
    CHECK(plus.potential().stringPosition(zp, 0) == -minus.potential().stringPosition(zm, 0));
    CHECK(plus.potential().stringPosition(zp, 0) != 0.0);
}
