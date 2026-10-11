// SPDX-License-Identifier: MIT
// Loader and canonical writer of instrument files (B1-R2 to B1-R5).
//
// SPEC sections implemented: SPEC-INST "Instrument definitions" and "Instrument file"; SPEC-API
// "Descriptions". Pipeline: text checks, parse, version, migration, structural validation into
// the frozen description structs, cross-field rules, blob checks. Non-real-time; no exception
// leaves this file (nlohmann/json is used without exceptions where possible and every entry point
// catches the rest).
#include "pme/loader/instrument_file.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <variant>

#include "reader.hpp"

namespace pme::loader {
namespace detail {

#include "fields_generated.hpp"

}  // namespace detail

namespace {

using detail::Code;
using detail::ErrorList;
using detail::Json;
using detail::OJson;

// True if arrays and objects nest deeper than kMaxDepth (strings and escapes are skipped).
bool tooDeep(std::string_view text) {
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > detail::kMaxDepth) {
                return true;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

bool allFinite(const OJson& v) {
    if (v.is_number_float()) {
        return std::isfinite(v.get<double>());
    }
    if (v.is_structured()) {
        for (const auto& item : v) {
            if (!allFinite(item)) {
                return false;
            }
        }
    }
    return true;
}

// Reads the schema version of a parsed root object. Returns false (error reported) if the version
// is present but not a supported integer.
bool readVersion(const Json& doc, int& version, ErrorList& errors) {
    version = kCurrentSchemaVersion;
    const auto it = doc.find("schemaVersion");
    if (it == doc.end()) {
        return true;  // reported as `missing` by the structural pass
    }
    double v = -1.0;
    if (it->is_number() && !it->is_boolean()) {
        v = it->get<double>();
    }
    if (!it->is_number() || std::floor(v) != v) {
        errors.add(LoadError{"/schemaVersion", "version", "expected an integer schema version"});
        return false;
    }
    if (v < 0.0 || v > static_cast<double>(kCurrentSchemaVersion)) {
        errors.add(LoadError{"/schemaVersion", "version",
                             "expected a schema version from 0 to " +
                                 std::to_string(kCurrentSchemaVersion)});
        return false;
    }
    version = static_cast<int>(v);
    return true;
}

Status loadImpl(std::string_view text, const std::filesystem::path* directory, bool verifyBlobs,
                InstrumentFile& out, std::vector<LoadError>& errors) {
    ErrorList list;
    const auto fail = [&]() {
        errors = list.take();
        return Status::InvalidArgument;
    };
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
        list.add(LoadError{"", "parse", "a byte order mark is not allowed"});
        return fail();
    }
    if (tooDeep(text)) {
        list.add(LoadError{"", "structure",
                           "nesting is deeper than " + std::to_string(detail::kMaxDepth) +
                               " levels"});
        return fail();
    }
    Json doc = Json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded()) {
        list.add(LoadError{"", "parse", "the text is not valid JSON"});
        return fail();
    }
    if (!doc.is_object()) {
        list.add(LoadError{"", "type", "expected an object"});
        return fail();
    }
    int version = kCurrentSchemaVersion;
    if (!readVersion(doc, version, list)) {
        return fail();
    }
    if (version < kCurrentSchemaVersion) {
        detail::migrateToCurrent(doc, version, list);
        if (!list.empty()) {
            return fail();
        }
    }
    InstrumentFile file;
    file.sourceVersion = version;
    {
        const detail::Reader root(list, doc, "");
        detail::ObjectScope s(root);
        if (s.ok()) {
            std::string format;
            s.choice("format", format, true, {"pme-instrument"});
            int ignored = 0;
            s.integer("schemaVersion", ignored, true,
                      detail::IntRule{true, kCurrentSchemaVersion, true, kCurrentSchemaVersion,
                                      {0, 0}, false});
            s.object("instrument", file.desc, true);
            s.objects("blobs", file.blobs, false, 0);
            s.finish();
        }
    }
    if (!list.empty()) {
        return fail();
    }
    detail::checkCrossFieldRules(file, list);
    detail::checkManifest(file.blobs, list);
    if (directory != nullptr) {
        detail::checkBlobFiles(file.blobs, *directory, verifyBlobs, list);
    }
    if (!list.empty()) {
        return fail();
    }
    out = std::move(file);
    errors.clear();
    return Status::Ok;
}

Status guarded(std::string_view text, const std::filesystem::path* directory, bool verifyBlobs,
               InstrumentFile& out, std::vector<LoadError>& errors) {
    try {
        return loadImpl(text, directory, verifyBlobs, out, errors);
    } catch (const std::exception& e) {
        errors = {LoadError{"", "structure", std::string("internal error: ") + e.what()}};
    } catch (...) {
        errors = {LoadError{"", "structure", "internal error"}};
    }
    return Status::InvalidArgument;
}

}  // namespace

Status loadInstrument(std::string_view json, InstrumentFile& out, std::vector<LoadError>& errors) {
    return guarded(json, nullptr, false, out, errors);
}

Status loadInstrumentFile(const std::filesystem::path& path, bool verifyBlobs, InstrumentFile& out,
                          std::vector<LoadError>& errors) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            errors = {LoadError{"", "parse", "cannot open " + path.string()}};
            return Status::InvalidArgument;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        const std::filesystem::path directory = path.parent_path();
        return guarded(text, &directory, verifyBlobs, out, errors);
    } catch (...) {
        errors = {LoadError{"", "parse", "cannot read " + path.string()}};
        return Status::InvalidArgument;
    }
}

Status writeInstrument(const InstrumentFile& file, std::string& json) {
    try {
        OJson root = OJson::object();
        root["format"] = "pme-instrument";
        root["schemaVersion"] = kCurrentSchemaVersion;
        detail::write(file.desc, root["instrument"]);
        root["blobs"] = detail::blobsToJson(file.blobs);
        if (!allFinite(root)) {
            return Status::InvalidArgument;
        }
        json = detail::serialise(root);
        return Status::Ok;
    } catch (...) {
        return Status::InvalidArgument;
    }
}

}  // namespace pme::loader
