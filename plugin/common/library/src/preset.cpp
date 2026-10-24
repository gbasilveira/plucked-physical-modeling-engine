// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-API "Parameter classes" (presets store registry parameters by
// id); SPEC-INST "Instrument file" (the instrument is referenced). D5-R2, D5-R3.
#include "pme/plugin/library/preset.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>

#include "pme/plugin/ids.hpp"
#include "pme/plugin/library/files.hpp"

namespace pme::plugin::library {
namespace {

using Json = nlohmann::json;

bool isHash(const std::string& s) {
    if (s.size() != 71 || !s.starts_with("sha256:")) {
        return false;
    }
    return std::all_of(s.begin() + 7, s.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

const ParamInfo* findParamByName(std::string_view name) {
    for (const ParamInfo& info : paramRegistry()) {
        if (info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

/// Decimal id from a JSON key; false if the key is not a plain unsigned integer.
bool parseId(const std::string& key, ParamId& id) {
    if (key.empty() || key.size() > 10) {
        return false;
    }
    unsigned long long value = 0;
    const auto r = std::from_chars(key.data(), key.data() + key.size(), value);
    if (r.ec != std::errc() || r.ptr != key.data() + key.size() || value > 0xFFFFFFFFULL) {
        return false;
    }
    id = static_cast<ParamId>(value);
    return true;
}

std::string pointerEscape(const std::string& key) {
    std::string out;
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// Apply registry rules to (id, value) pairs: unknown id dropped, value clamped, default dropped.
void normaliseParams(const std::map<ParamId, double>& raw, const std::string& pointerBase,
                     const std::map<ParamId, std::string>& keyOf, ParamOverrides& out,
                     Report& report) {
    out.clear();
    for (const auto& [id, value] : raw) {
        const std::string where = pointerBase + "/" + pointerEscape(keyOf.at(id));
        const ParamInfo* info = findParam(id);
        if (info == nullptr) {
            report.diagnostic(
                where, code::kUnknownParam,
                "parameter id " + std::to_string(id) + " is not in the registry; ignored");
            continue;
        }
        double v = value;
        if (v < info->min || v > info->max) {
            v = std::clamp(v, info->min, info->max);
            report.diagnostic(where, code::kClamped,
                              "value outside [" + std::to_string(info->min) + ", " +
                                  std::to_string(info->max) + "] for " + std::string(info->name) +
                                  "; clamped");
        }
        if (v != info->defaultValue) {
            out.emplace_back(id, v);
        }
    }
}

/// Migrate version 0 to the version 1 document. Version 0: instrument is a bare string and params
/// are keyed by dotted registry names.
bool migrateV0(const Json& in, std::string_view stem, Json& out, Report& report) {
    static const std::vector<std::string> allowed = {"format", "presetSchemaVersion", "instrument",
                                                     "params", "name"};
    bool ok = true;
    for (const auto& [key, value] : in.items()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            report.error("/" + pointerEscape(key), code::kUnknownKey, "unknown member");
            ok = false;
        }
    }
    const auto instrument = in.find("instrument");
    if (instrument == in.end()) {
        report.error("/instrument", code::kMissing,
                     "version 0 needs the instrument id as a string");
        ok = false;
    } else if (!instrument->is_string()) {
        report.error("/instrument", code::kType, "version 0 stores the instrument as a string");
        ok = false;
    }
    Json params = Json::object();
    if (const auto it = in.find("params"); it != in.end()) {
        if (!it->is_object()) {
            report.error("/params", code::kType, "params must be an object");
            ok = false;
        } else {
            for (const auto& [name, value] : it->items()) {
                const ParamInfo* info = findParamByName(name);
                if (info == nullptr) {
                    report.diagnostic(
                        "/params/" + pointerEscape(name), code::kUnknownParam,
                        "parameter name '" + name + "' is not in the registry; dropped");
                    continue;
                }
                params[std::to_string(info->id)] = value;
            }
        }
    }
    std::string name(stem);
    if (const auto it = in.find("name"); it != in.end()) {
        if (!it->is_string()) {
            report.error("/name", code::kType, "name must be a string");
            ok = false;
        } else {
            name = it->get<std::string>();
        }
    }
    if (!ok) {
        return false;
    }
    Json result = Json::object();
    result["format"] = in.at("format");
    result["presetSchemaVersion"] = kPresetSchemaVersion;
    result["instrument"] = Json{{"id", instrument->get<std::string>()}};
    result["name"] = name;
    result["params"] = std::move(params);
    out = std::move(result);
    return true;
}

bool validateV1(const Json& doc, std::string_view stem, Preset& out, Report& report) {
    static const std::vector<std::string> allowed = {"format", "presetSchemaVersion", "instrument",
                                                     "params", "name"};
    bool ok = true;
    for (const auto& [key, value] : doc.items()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            report.error("/" + pointerEscape(key), code::kUnknownKey, "unknown member");
            ok = false;
        }
    }
    Preset preset;
    preset.name = std::string(stem);

    const auto instrument = doc.find("instrument");
    if (instrument == doc.end()) {
        report.error("/instrument", code::kMissing, "the instrument reference is required");
        ok = false;
    } else if (!instrument->is_object()) {
        report.error("/instrument", code::kType, "instrument must be an object {id, hash}");
        ok = false;
    } else {
        for (const auto& [key, value] : instrument->items()) {
            if (key != "id" && key != "hash") {
                report.error("/instrument/" + pointerEscape(key), code::kUnknownKey,
                             "unknown member");
                ok = false;
            }
        }
        const auto id = instrument->find("id");
        if (id == instrument->end()) {
            report.error("/instrument/id", code::kMissing, "the instrument id is required");
            ok = false;
        } else if (!id->is_string()) {
            report.error("/instrument/id", code::kType, "id must be a string");
            ok = false;
        } else {
            LibraryId parsed;
            Report idReport;
            if (parseLibraryId(id->get<std::string>(), parsed, idReport) != Status::Ok) {
                report.error("/instrument/id", code::kInvalidId,
                             "expected factory/<name> or user/<name>");
                ok = false;
            } else {
                preset.instrumentId = id->get<std::string>();
            }
        }
        if (const auto hash = instrument->find("hash"); hash != instrument->end()) {
            if (!hash->is_string() || !isHash(hash->get<std::string>())) {
                report.error("/instrument/hash", code::kType,
                             "hash must be sha256: followed by 64 lower-case hex digits");
                ok = false;
            } else {
                preset.instrumentHash = hash->get<std::string>();
            }
        }
    }

    if (const auto name = doc.find("name"); name != doc.end()) {
        if (!name->is_string()) {
            report.error("/name", code::kType, "name must be a string");
            ok = false;
        } else {
            preset.name = name->get<std::string>();
        }
    }

    std::map<ParamId, double> raw;
    std::map<ParamId, std::string> keyOf;
    if (const auto params = doc.find("params"); params != doc.end()) {
        if (!params->is_object()) {
            report.error("/params", code::kType, "params must be an object keyed by parameter id");
            ok = false;
        } else {
            for (const auto& [key, value] : params->items()) {
                const std::string where = "/params/" + pointerEscape(key);
                ParamId id = 0;
                if (!parseId(key, id)) {
                    report.error(where, code::kType, "key must be a decimal parameter id");
                    ok = false;
                    continue;
                }
                if (!value.is_number()) {
                    report.error(where, code::kType, "value must be a number");
                    ok = false;
                    continue;
                }
                const double v = value.get<double>();
                if (!std::isfinite(v)) {
                    report.error(where, code::kRange, "value must be finite");
                    ok = false;
                    continue;
                }
                if (raw.count(id) != 0) {
                    report.error(where, code::kDuplicate,
                                 "parameter id " + std::to_string(id) + " appears twice");
                    ok = false;
                    continue;
                }
                raw[id] = v;
                keyOf[id] = key;
            }
        }
    }
    if (!ok) {
        return false;
    }
    normaliseParams(raw, "/params", keyOf, preset.params, report);
    out = std::move(preset);
    return true;
}

std::string stemOf(const std::filesystem::path& file) {
    std::string name = pathToUtf8(file.filename());
    const std::string ext = kPresetExtension;
    if (name.size() > ext.size() && name.ends_with(ext)) {
        name.resize(name.size() - ext.size());
    } else {
        name = pathToUtf8(file.stem());
    }
    return name;
}

void sortErrors(Report& report) {
    std::sort(report.errors.begin(), report.errors.end(),
              [](const LibraryError& a, const LibraryError& b) {
                  return a.path != b.path ? a.path < b.path : a.code < b.code;
              });
}

}  // namespace

Status parsePreset(std::string_view text, std::string_view fileStem, Preset& out,
                   int& sourceVersion, Report& report) {
    try {
        Report local;
        const Json doc = Json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (doc.is_discarded() || !doc.is_object()) {
            report.error("", code::kParse, "not a JSON object");
            return Status::InvalidArgument;
        }
        const auto format = doc.find("format");
        if (format == doc.end()) {
            report.error("/format", code::kMissing, "format is required");
            return Status::InvalidArgument;
        }
        if (!format->is_string() || format->get<std::string>() != kPresetFormat) {
            report.error("/format", code::kEnum, std::string("expected \"") + kPresetFormat + "\"");
            return Status::InvalidArgument;
        }
        const auto version = doc.find("presetSchemaVersion");
        if (version == doc.end()) {
            report.error("/presetSchemaVersion", code::kMissing, "presetSchemaVersion is required");
            return Status::InvalidArgument;
        }
        if (!version->is_number_integer()) {
            report.error("/presetSchemaVersion", code::kVersion,
                         "presetSchemaVersion must be an integer");
            return Status::InvalidArgument;
        }
        const auto found = version->get<std::int64_t>();
        if (found < 0 || found > kPresetSchemaVersion) {
            report.error("/presetSchemaVersion", code::kVersion,
                         "unsupported preset schema version " + std::to_string(found) +
                             " (this build reads 0 to " + std::to_string(kPresetSchemaVersion) +
                             ")");
            return Status::InvalidArgument;
        }
        Json current;
        bool ok = false;
        if (found == 0) {
            ok = migrateV0(doc, fileStem, current, local);
        } else {
            current = doc;
            ok = true;
        }
        Preset preset;
        if (ok) {
            ok = validateV1(current, fileStem, preset, local);
        }
        report.diagnostics.insert(report.diagnostics.end(), local.diagnostics.begin(),
                                  local.diagnostics.end());
        if (!ok) {
            report.errors.insert(report.errors.end(), local.errors.begin(), local.errors.end());
            sortErrors(report);
            return Status::InvalidArgument;
        }
        out = std::move(preset);
        sourceVersion = static_cast<int>(found);
        return Status::Ok;
    } catch (...) {
        report.error("", code::kParse, "unexpected failure while reading the preset");
        return Status::InvalidArgument;
    }
}

Status serializePreset(const Preset& preset, std::string& text, Report& report) {
    try {
        Json params = Json::object();
        std::map<ParamId, double> sorted;
        for (const auto& [id, value] : preset.params) {
            if (!std::isfinite(value)) {
                report.error("/params/" + std::to_string(id), code::kRange, "value must be finite");
                return Status::InvalidArgument;
            }
            const ParamInfo* info = findParam(id);
            if (info == nullptr || value == info->defaultValue) {
                continue;
            }
            sorted[id] = value;
        }
        for (const auto& [id, value] : sorted) {
            params[std::to_string(id)] = value;
        }
        Json instrument = Json::object();
        instrument["id"] = preset.instrumentId;
        if (!preset.instrumentHash.empty()) {
            instrument["hash"] = preset.instrumentHash;
        }
        Json doc = Json::object();
        doc["format"] = kPresetFormat;
        doc["instrument"] = std::move(instrument);
        doc["name"] = preset.name;
        doc["params"] = std::move(params);
        doc["presetSchemaVersion"] = kPresetSchemaVersion;
        text = doc.dump() + "\n";
        return Status::Ok;
    } catch (...) {
        report.error("", code::kType, "preset cannot be written (invalid UTF-8 in a string?)");
        return Status::InvalidArgument;
    }
}

Status loadPresetFile(const std::filesystem::path& file, Preset& out, int& sourceVersion,
                      Report& report) {
    std::string text;
    if (readFile(file, text, report) != Status::Ok) {
        return Status::InvalidArgument;
    }
    Report local;
    const Status status = parsePreset(text, stemOf(file), out, sourceVersion, local);
    for (auto& e : local.errors) {
        if (e.path.empty() || e.path.front() == '/') {
            e.message += " [" + pathToUtf8(file) + "]";
        }
        report.errors.push_back(std::move(e));
    }
    report.diagnostics.insert(report.diagnostics.end(), local.diagnostics.begin(),
                              local.diagnostics.end());
    return status;
}

Status savePresetFile(const std::filesystem::path& file, const Preset& preset, Report& report) {
    std::string text;
    if (serializePreset(preset, text, report) != Status::Ok) {
        return Status::InvalidArgument;
    }
    return writeFileAtomic(file, text, report);
}

Status importPreset(const Locations& locations, const std::filesystem::path& sourceFile,
                    std::string& id, Report& report) {
    try {
        Preset preset;
        int sourceVersion = 0;
        if (loadPresetFile(sourceFile, preset, sourceVersion, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        // The instrument is only referenced: a dangling reference is a diagnostic here and an
        // error when the preset is loaded.
        {
            std::filesystem::path ignored;
            Report resolveReport;
            if (resolveInstrument(locations, preset.instrumentId, ignored, resolveReport) !=
                Status::Ok) {
                report.diagnostic(preset.instrumentId, code::kNotFound,
                                  "the preset's instrument is not in the library");
            } else if (!preset.instrumentHash.empty()) {
                loader::InstrumentFile instrument;
                Report loadReport;
                std::string hash;
                if (loadLibraryInstrument(locations, preset.instrumentId, instrument, loadReport) ==
                        Status::Ok &&
                    instrumentHash(instrument, hash, loadReport) == Status::Ok &&
                    hash != preset.instrumentHash) {
                    report.diagnostic(preset.instrumentId, code::kInstrumentChanged,
                                      "the instrument changed since the preset was saved");
                }
            }
        }
        std::string text;
        if (serializePreset(preset, text, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        const std::string base = reduceName(preset.name);
        const std::filesystem::path root = locations.userPresets();
        constexpr int kMaxCandidates = 1000;
        for (int n = 1; n <= kMaxCandidates; ++n) {
            const std::string name = n == 1 ? base : base + "-" + std::to_string(n);
            const std::filesystem::path target = root / pathFromUtf8(name + kPresetExtension);
            bool created = false;
            if (writeFileExclusive(target, text, created, report) != Status::Ok) {
                return Status::InvalidArgument;
            }
            if (created) {
                id = formatLibraryId(Source::User, name);
                return Status::Ok;
            }
            std::string existing;
            Report ignored;
            if (readFile(target, existing, ignored) == Status::Ok && existing == text) {
                id = formatLibraryId(Source::User, name);
                return Status::Ok;
            }
        }
        report.error(pathToUtf8(root), code::kIo, "no free preset name for " + base);
        return Status::InvalidArgument;
    } catch (...) {
        report.error(pathToUtf8(sourceFile), code::kIo, "unexpected failure while importing");
        return Status::InvalidArgument;
    }
}

Status exportPreset(const Locations& locations, std::string_view id,
                    const std::filesystem::path& targetFile, Report& report) {
    std::filesystem::path file;
    if (resolvePreset(locations, id, file, report) != Status::Ok) {
        return Status::InvalidArgument;
    }
    Preset preset;
    int sourceVersion = 0;
    if (loadPresetFile(file, preset, sourceVersion, report) != Status::Ok) {
        return Status::InvalidArgument;
    }
    return savePresetFile(targetFile, preset, report);
}

Status loadLibraryPreset(const Locations& locations, std::string_view id, Preset& preset,
                         loader::InstrumentFile& instrument, Report& report) {
    try {
        std::filesystem::path file;
        if (resolvePreset(locations, id, file, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        Preset loaded;
        int sourceVersion = 0;
        if (loadPresetFile(file, loaded, sourceVersion, report) != Status::Ok) {
            return Status::InvalidArgument;
        }
        loader::InstrumentFile loadedInstrument;
        if (loadLibraryInstrument(locations, loaded.instrumentId, loadedInstrument, report) !=
            Status::Ok) {
            return Status::InvalidArgument;  // not-found, blob, ... with the instrument's path
        }
        if (!loaded.instrumentHash.empty()) {
            std::string hash;
            if (instrumentHash(loadedInstrument, hash, report) != Status::Ok) {
                return Status::InvalidArgument;
            }
            if (hash != loaded.instrumentHash) {
                report.diagnostic(loaded.instrumentId, code::kInstrumentChanged,
                                  "the instrument changed since the preset was saved");
            }
        }
        preset = std::move(loaded);
        instrument = std::move(loadedInstrument);
        return Status::Ok;
    } catch (...) {
        report.error(std::string(id), code::kIo, "unexpected failure while loading");
        return Status::InvalidArgument;
    }
}

std::vector<ListEntry> listPresets(const Locations& locations) {
    std::vector<ListEntry> entries;
    const auto scan = [&entries](const std::filesystem::path& root, Source source) {
        try {
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                return;
            }
            std::vector<std::filesystem::path> files;
            for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code typeEc;
                const std::string name = pathToUtf8(it->path().filename());
                if (it->is_regular_file(typeEc) && !name.empty() && name.front() != '.' &&
                    name.ends_with(kPresetExtension) &&
                    name.size() > std::string(kPresetExtension).size()) {
                    files.push_back(it->path());
                }
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                ListEntry entry;
                entry.source = source;
                entry.id = formatLibraryId(source, stemOf(file));
                entry.schemaVersion = -1;
                Preset preset;
                int sourceVersion = 0;
                Report report;
                if (loadPresetFile(file, preset, sourceVersion, report) == Status::Ok) {
                    entry.name = preset.name;
                    entry.schemaVersion = sourceVersion;
                } else {
                    entry.errors = report.errors;
                }
                entries.push_back(std::move(entry));
            }
        } catch (...) {
            return;  // A directory that cannot be scanned contributes no entries.
        }
    };
    scan(locations.factoryPresets(), Source::Factory);
    scan(locations.userPresets(), Source::User);
    return entries;
}

StateData presetToState(const Preset& preset) {
    StateData state;
    state.instrument = preset.instrumentId;
    state.params = preset.params;
    return state;
}

Preset presetFromState(const StateData& state, std::string name, std::string instrumentHash) {
    Preset preset;
    preset.name = std::move(name);
    preset.instrumentId = state.instrument;
    preset.instrumentHash = std::move(instrumentHash);
    preset.params = state.params;
    return preset;
}

}  // namespace pme::plugin::library
