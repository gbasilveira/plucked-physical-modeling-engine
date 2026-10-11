// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-API "Parameter classes" (state stores registry parameters).
#include "pme/plugin/state.hpp"

#include <charconv>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>

namespace pme::plugin {

std::string serializeState(const StateData& state) noexcept {
    try {
        nlohmann::json j = nlohmann::json::object();
        j["state_schema_version"] = state.schemaVersion;
        j["plugin_version"] = state.pluginVersion;
        j["interfaces_version"] = state.interfacesVersion;
        j["instrument"] = state.instrument;
        nlohmann::json params = nlohmann::json::object();
        for (const auto& [id, value] : state.params) {
            params[std::to_string(id)] = value;
        }
        j["params"] = std::move(params);
        return j.dump();
    } catch (...) {
        return {};
    }
}

StateParse parseState(std::string_view text, StateData& out) noexcept {
    try {
        const nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            return StateParse::Malformed;
        }
        const auto schema = j.find("state_schema_version");
        if (schema == j.end() || !schema->is_number_integer()) {
            return StateParse::Malformed;
        }
        const auto version = schema->get<std::int64_t>();
        if (version > kStateSchemaVersion) {
            return StateParse::NewerSchema;
        }
        if (version < 1) {
            return StateParse::Malformed;
        }
        StateData data;
        data.schemaVersion = static_cast<int>(version);
        if (const auto it = j.find("plugin_version"); it != j.end() && it->is_string()) {
            data.pluginVersion = it->get<std::string>();
        }
        if (const auto it = j.find("interfaces_version");
            it != j.end() && it->is_number_integer()) {
            data.interfacesVersion = it->get<int>();
        }
        if (const auto it = j.find("instrument"); it != j.end() && it->is_string()) {
            data.instrument = it->get<std::string>();
        }
        std::map<ParamId, double> sorted;
        if (const auto it = j.find("params"); it != j.end()) {
            if (!it->is_object()) {
                return StateParse::Malformed;
            }
            for (const auto& [key, value] : it->items()) {
                ParamId id = 0;
                const char* first = key.data();
                const char* last = key.data() + key.size();
                const auto r = std::from_chars(first, last, id);
                if (r.ec != std::errc() || r.ptr != last || !value.is_number()) {
                    return StateParse::Malformed;
                }
                sorted[id] = value.get<double>();
            }
        }
        data.params.assign(sorted.begin(), sorted.end());
        out = std::move(data);
        return StateParse::Ok;
    } catch (...) {
        return StateParse::Malformed;
    }
}

}  // namespace pme::plugin
