// SPDX-License-Identifier: MIT
// Migration of older instrument file versions to the current one (B1-R2). Version 0 mirrors the
// interfaces-v1 structs (2daee85); docs/dev/instrument-format.md lists the steps. The Python loader
// (pme_compiler.schema.migrate) implements the same steps.
#include <initializer_list>

#include "reader.hpp"

namespace pme::loader::detail {

namespace {

// Rename `from` to `to` unless `to` is already present.
void rename(Json& obj, const char* from, const char* to) {
    if (obj.contains(from) && !obj.contains(to)) {
        obj[to] = std::move(obj[from]);
        obj.erase(from);
    }
}

void setDefault(Json& obj, const char* key, Json value) {
    if (!obj.contains(key)) {
        obj[key] = std::move(value);
    }
}

// Report `migration` at `path` if any of `keys` is missing.
bool requireSources(const Json& obj, const std::string& path, const char* what,
                    std::initializer_list<const char*> keys, ErrorList& errors) {
    std::string missing;
    for (const char* key : keys) {
        if (!obj.contains(key)) {
            missing += (missing.empty() ? "" : ", ") + std::string(key);
        }
    }
    if (missing.empty()) {
        return true;
    }
    errors.add(LoadError{path, toString(Code::Migration),
                         std::string("no version 0 source for ") + missing + " of a " + what +
                             "; supply them"});
    return false;
}

void migrateExciter(Json& exciter, const std::string& path, ErrorList& errors) {
    for (auto& item : exciter.items()) {
        Json& alt = item.value();
        if (!alt.is_object()) {
            continue;
        }
        const std::string& key = item.key();
        const std::string at = path + "/" + escapePointer(key);
        if (key == "pluckPulse") {
            rename(alt, "peakForce", "peakForceMax");
            setDefault(alt, "strengthGamma", Json(1));
        } else if (key == "plectrum") {
            if (requireSources(alt, at, "plectrum", {"position", "angle"}, errors)) {
                rename(alt, "driverSpeed", "driverSpeedMax");
                setDefault(alt, "strengthGamma", Json(1));
            }
        } else if (key == "finger") {
            if (requireSources(alt, at, "finger", {"driverStiffness", "position", "angle"},
                               errors)) {
                rename(alt, "mass", "tipMass");
                rename(alt, "stiffness", "contactK");
                rename(alt, "alpha", "contactAlpha");
                rename(alt, "driverSpeed", "driverSpeedMax");
                setDefault(alt, "strengthGamma", Json(1));
            }
        }
    }
}

void migrate0to1(Json& doc, ErrorList& errors) {
    const auto inst = doc.find("instrument");
    if (inst == doc.end() || !inst->is_object()) {
        return;
    }
    const auto groups = inst->find("groups");
    if (groups != inst->end() && groups->is_array()) {
        for (std::size_t gi = 0; gi < groups->size(); ++gi) {
            Json& group = (*groups)[gi];
            if (!group.is_object()) {
                continue;
            }
            const std::string gpath = "/instrument/groups/" + std::to_string(gi);
            const auto exciter = group.find("exciter");
            if (exciter != group.end() && exciter->is_object()) {
                migrateExciter(*exciter, gpath + "/exciter", errors);
            }
            const auto segments = group.find("segments");
            if (segments != group.end() && segments->is_array()) {
                for (Json& segment : *segments) {
                    if (segment.is_object()) {
                        setDefault(segment, "unisonDetuneCents", Json(0));
                    }
                }
            }
        }
    }
    const auto body = inst->find("body");
    if (body != inst->end() && body->is_object()) {
        const auto plate = body->find("plate");
        if (plate != body->end() && plate->is_object()) {
            setDefault(*plate, "outline", Json::array());
            setDefault(*plate, "thicknessNx", Json(0));
            setDefault(*plate, "thicknessNy", Json(0));
            setDefault(*plate, "thicknessGrid", Json::array());
            setDefault(*plate, "bridges", Json::array());
            setDefault(*plate, "rimRotationalStiffness", Json(0));
        }
    }
}

}  // namespace

void migrateToCurrent(Json& doc, int fromVersion, ErrorList& errors) {
    if (fromVersion == 0) {
        migrate0to1(doc, errors);
    }
    doc["schemaVersion"] = kCurrentSchemaVersion;
}

}  // namespace pme::loader::detail
