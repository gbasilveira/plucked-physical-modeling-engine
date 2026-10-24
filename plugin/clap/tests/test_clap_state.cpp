// SPDX-License-Identifier: MIT
// D1-T2: state save and load round-trips exactly, through the CLAP state extension with in-memory
// streams (D1-R4).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "clap_test_support.hpp"
#include "pme/plugin/ids.hpp"

using namespace pme::clap;
using namespace pme::clap::test;

namespace {

std::string saveState(const clap_plugin_t* p, std::size_t chunk = 0) {
    const auto* state =
        static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    MemoryOut out(chunk);
    REQUIRE(state->save(p, &out.stream));
    return out.data;
}

bool loadState(const clap_plugin_t* p, const std::string& bytes) {
    const auto* state =
        static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    MemoryIn in(bytes);
    return state->load(p, &in.stream);
}

double paramValue(const clap_plugin_t* p, clap_id id) {
    const auto* params =
        static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    double v = 0.0;
    REQUIRE(params->get_value(p, id, &v));
    return v;
}

}  // namespace

TEST_CASE("D1-T2 state save and load round-trips exactly") {
    Instance a;
    REQUIRE(a.initialised);
    const auto* params = a.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);

    // A non-default value on every parameter: the extremes and a value with full 17-digit
    // precision, delivered the way a host would (param-value events through flush).
    const auto registry = pme::paramRegistry();
    std::vector<double> chosen;
    InEvents in;
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const pme::ParamInfo& r = registry[i];
        double v = 0.0;
        switch (i % 3) {
            case 0:
                v = r.min != r.defaultValue ? r.min : r.max;
                break;
            case 1:
                v = r.max != r.defaultValue ? r.max : r.min;
                break;
            default:
                v = r.min + (r.max - r.min) * 0.12345678901234567;
                break;
        }
        REQUIRE(v != r.defaultValue);
        chosen.push_back(v);
        in.param(0, r.id, v);
    }
    params->flush(a.plugin, in.list(), nullptr);
    pme::clap::controllerOf(a.plugin).setInstrumentReference("instruments/steinway-d.json");
    for (std::size_t i = 0; i < registry.size(); ++i) {
        CHECK(paramValue(a.plugin, registry[i].id) == chosen[i]);  // inside the range: exact
    }

    const std::string first = saveState(a.plugin);
    REQUIRE_FALSE(first.empty());

    Instance b;
    REQUIRE(b.initialised);
    REQUIRE(loadState(b.plugin, first));
    for (std::size_t i = 0; i < registry.size(); ++i) {
        CHECK(paramValue(b.plugin, registry[i].id) == paramValue(a.plugin, registry[i].id));
    }
    CHECK(pme::clap::controllerOf(b.plugin).instrumentReference() == "instruments/steinway-d.json");
    const std::string second = saveState(b.plugin, /*chunk=*/5);  // partial writes
    CHECK(first == second);                                       // identical byte streams
}

TEST_CASE("D1-T2 state carries the schema, plug-in and interface versions") {
    Instance a;
    const std::string text = saveState(a.plugin);
    pme::plugin::StateData data;
    REQUIRE(pme::plugin::parseState(text, data) == pme::plugin::StateParse::Ok);
    CHECK(data.schemaVersion == 1);
    CHECK(data.pluginVersion == pme::plugin::kPluginVersion);
    CHECK(data.interfacesVersion == pme::interfacesVersionMajor);
    CHECK(data.params.empty());  // only values that differ from the default are stored
}

TEST_CASE("D1-T2 a newer schema version is rejected and nothing changes") {
    Instance a;
    const std::string newer =
        R"({"state_schema_version":2,"plugin_version":"9.9.9","interfaces_version":2,"params":{"2001":-3.0}})";
    CHECK_FALSE(loadState(a.plugin, newer));
    CHECK(paramValue(a.plugin, 2001) == 0.0);
}

TEST_CASE("D1-T2 unknown parameter ids are ignored and values are clamped") {
    Instance a;
    const std::string text =
        R"({"state_schema_version":1,"plugin_version":"0","interfaces_version":2,"instrument":"",)"
        R"("params":{"2001":-3.0,"999999":1.0,"4001":123.0}})";
    REQUIRE(loadState(a.plugin, text));
    CHECK(paramValue(a.plugin, 2001) == -3.0);
    CHECK(paramValue(a.plugin, 4001) == 0.05);  // clamped to the registry maximum
}

TEST_CASE("D1-T2 an empty or malformed state is refused") {
    Instance a;
    CHECK_FALSE(loadState(a.plugin, ""));
    CHECK_FALSE(loadState(a.plugin, "{"));
    CHECK_FALSE(loadState(a.plugin, "[1,2,3]"));
    const auto* state = a.ext<clap_plugin_state_t>(CLAP_EXT_STATE);
    CHECK_FALSE(state->load(a.plugin, nullptr));
    CHECK_FALSE(state->save(a.plugin, nullptr));
}

TEST_CASE("D1-R4 loaded state reaches the engine while the plug-in is active") {
    Instance a;
    REQUIRE(a.plugin->activate(a.plugin, 48000.0, 1, 64));
    REQUIRE(loadState(a.plugin, R"({"state_schema_version":1,"params":{"2001":-3.0}})"));
    OutBuffers out(64);
    process(a.plugin, 64, out, InEvents());
    REQUIRE(a.engine->events.size() == 1);
    const auto& param = std::get<pme::Param>(a.engine->events[0].payload);
    CHECK(param.id == 2001);
    CHECK(param.value == -3.0);
    a.plugin->deactivate(a.plugin);
}
