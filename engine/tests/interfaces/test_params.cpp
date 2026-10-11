// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <string_view>

#include "pme/params.hpp"

TEST_CASE("P0.4-T3 parameter IDs are unique, units non-empty, ranges valid") {
    const auto registry = pme::paramRegistry();
    REQUIRE(!registry.empty());

    std::set<pme::ParamId> ids;
    std::set<std::string_view> names;
    pme::ParamId previous = 0;
    for (const pme::ParamInfo& p : registry) {
        CAPTURE(p.name);
        CHECK(ids.insert(p.id).second);
        CHECK(names.insert(p.name).second);
        CHECK(p.id > previous);  // sorted ascending, as findParam requires
        previous = p.id;
        CHECK(!p.name.empty());
        CHECK(!p.unit.empty());
        CHECK(!p.specKey.empty());
        CHECK(!p.description.empty());
        CHECK(std::isfinite(p.min));
        CHECK(std::isfinite(p.max));
        CHECK(std::isfinite(p.defaultValue));
        CHECK(p.min < p.max);
        CHECK(p.min <= p.defaultValue);
        CHECK(p.defaultValue <= p.max);
        CHECK((p.cls == pme::ParamClass::Continuous || p.cls == pme::ParamClass::Structural));
    }
}

TEST_CASE("P0.4-T3 findParam finds every entry and returns null for unused ids") {
    for (const pme::ParamInfo& p : pme::paramRegistry()) {
        const pme::ParamInfo* found = pme::findParam(p.id);
        REQUIRE(found != nullptr);
        CHECK(found == &p);
    }
    CHECK(pme::findParam(0) == nullptr);
    CHECK(pme::findParam(999) == nullptr);
    CHECK(pme::findParam(1004) == nullptr);
    CHECK(pme::findParam(0xFFFFFFFFu) == nullptr);
}

TEST_CASE("P0.4-T3 the registry holds the SPEC-API continuous parameters") {
    // SPEC-API "Parameter classes": pedal values, una corda shift, kappa_s, output gains, damper
    // timing.
    for (const pme::ParamId id : {1001u, 1002u, 1003u, 1010u, 2001u, 3001u, 4001u, 4002u, 4003u}) {
        const pme::ParamInfo* p = pme::findParam(id);
        REQUIRE(p != nullptr);
        CHECK(p->cls == pme::ParamClass::Continuous);
    }
    // Instrument-wide structural defaults of SPEC-INST: kappa_L, M_L, M_b, f_symp, f_max.
    for (const pme::ParamId id : {6001u, 6002u, 7001u, 3101u, 6003u}) {
        const pme::ParamInfo* p = pme::findParam(id);
        REQUIRE(p != nullptr);
        CHECK(p->cls == pme::ParamClass::Structural);
    }
}
