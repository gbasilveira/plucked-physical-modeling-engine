// SPDX-License-Identifier: MIT
// D1-R4: state serialiser (the CLAP-level round trip is D1-T2 in plugin/clap/tests).
#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <string_view>

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

TEST_CASE("D1-R4 state loader survives truncated mutated and hostile streams") {
    StateData good;
    good.pluginVersion = "0.0.0";
    good.instrument = "a";
    good.params = {{1001, 0.5}, {2001, -6.5}, {6003, 12345.678901234567}};
    const std::string text = pme::plugin::serializeState(good);

    // Every proper prefix of a valid stream is refused and leaves the output unchanged.
    for (std::size_t n = 0; n < text.size(); ++n) {
        StateData out;
        out.instrument = "unchanged";
        CHECK(pme::plugin::parseState(std::string_view(text).substr(0, n), out) ==
              StateParse::Malformed);
        CHECK(out.instrument == "unchanged");
    }
    // Deterministic byte mutations: never a crash; a refusal leaves the output unchanged.
    unsigned seed = 12345U;
    for (int i = 0; i < 400; ++i) {
        std::string t = text;
        for (int k = 0; k < 1 + (i % 4); ++k) {
            seed = seed * 1664525U + 1013904223U;
            t[(seed >> 8U) % t.size()] = static_cast<char>(seed >> 24U);
        }
        StateData out;
        out.instrument = "unchanged";
        const StateParse r = pme::plugin::parseState(t, out);
        if (r != StateParse::Ok) {
            CHECK(out.instrument == "unchanged");
        }
    }
    StateData out;
    // Numbers outside double or int range, ids outside the id range, negative and non-integer ids.
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"1001":1e999}})", out) ==
          StateParse::Malformed);
    // An out-of-range interfaces_version is currently truncated, not refused (review item 8).
    (void)pme::plugin::parseState(R"({"state_schema_version":1,"interfaces_version":99999999999})",
                                  out);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"-1":1}})", out) ==
          StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"99999999999":1}})",
                                  out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1,"params":{"1001":"x"}})", out) ==
          StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":1.5})", out) == StateParse::Malformed);
    CHECK(pme::plugin::parseState(R"({"state_schema_version":true})", out) ==
          StateParse::Malformed);
    // Very deep nesting is parsed and discarded without exhausting the stack.
    const std::size_t depth = 1000000;
    const std::string deep = R"({"state_schema_version":1,"x":)" + std::string(depth, '[') +
                             std::string(depth, ']') + "}";
    CHECK(pme::plugin::parseState(deep, out) == StateParse::Ok);
    CHECK(pme::plugin::parseState(std::string(depth, '['), out) == StateParse::Malformed);
}
