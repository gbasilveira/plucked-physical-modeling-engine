// SPDX-License-Identifier: MIT
// D1-R4: state serialiser (the CLAP-level round trip is D1-T2 in plugin/clap/tests).
#include <doctest/doctest.h>

#include <limits>
#include <string>

#include "pme/plugin/state.hpp"

using pme::plugin::StateData;
using pme::plugin::StateParse;

TEST_CASE("D1-R4 state serialises doubles exactly and in a fixed order") {
    StateData s;
    s.pluginVersion = "1.2.3";
    s.instrument = "pianos/steinway.json";
    s.params = {{1001, 0.1},
                {2001, -96.0},
                {3101, 0.30000000000000004},
                {6001, std::numeric_limits<double>::denorm_min()}};
    const std::string text = pme::plugin::serializeState(s);
    StateData back;
    REQUIRE(pme::plugin::parseState(text, back) == StateParse::Ok);
    CHECK(back.schemaVersion == 1);
    CHECK(back.pluginVersion == "1.2.3");
    CHECK(back.interfacesVersion == pme::interfacesVersionMajor);
    CHECK(back.instrument == "pianos/steinway.json");
    REQUIRE(back.params.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(back.params[i].first == s.params[i].first);
        CHECK(back.params[i].second == s.params[i].second);
    }
    CHECK(pme::plugin::serializeState(back) == text);
}

TEST_CASE("D1-R4 state rejects malformed input and newer schema versions") {
    StateData out;
    out.instrument = "unchanged";
    CHECK(pme::plugin::parseState("", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState("not json", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState("[]", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState("{}", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":0})", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"x":1}})", out) ==
          StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":2,"params":{}})", out) ==
          StateParse::NewerSchema);
    CHECK(out.instrument == "unchanged");
}

TEST_CASE("D1-R4 state keeps unknown ids for the controller to ignore") {
    StateData out;
    REQUIRE(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"999999":1.5}})", out) ==
            StateParse::Ok);
    REQUIRE(out.params.size() == 1);
    CHECK(out.params[0].first == 999999);
}
