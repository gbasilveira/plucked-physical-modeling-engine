// SPDX-License-Identifier: MIT
// pme_params_json: writes the parameter registry (pme/params.hpp) as JSON (P0.4-R4).
//
// Usage: pme_params_json [output-file]     (standard output when no file is given)
//
// The output is deterministic: entries sorted by id (the registry order), numbers printed with
// "%.12g", LF line endings, no locale dependence (the program never calls setlocale).
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "pme/params.hpp"
#include "pme/version.hpp"

namespace {

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

std::string number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

std::string render() {
    std::string out;
    out += "{\n";
    out += "  \"schema_version\": 1,\n";
    out += "  \"interfaces_version\": " + std::to_string(pme::interfacesVersionMajor) + ",\n";
    out += "  \"parameters\": [\n";
    const auto registry = pme::paramRegistry();
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const pme::ParamInfo& p = registry[i];
        out += "    {\n";
        out += "      \"id\": " + std::to_string(p.id) + ",\n";
        out += "      \"name\": " + quote(p.name) + ",\n";
        out += "      \"unit\": " + quote(p.unit) + ",\n";
        out += "      \"min\": " + number(p.min) + ",\n";
        out += "      \"max\": " + number(p.max) + ",\n";
        out += "      \"default\": " + number(p.defaultValue) + ",\n";
        out += std::string("      \"class\": ") +
               (p.cls == pme::ParamClass::Continuous ? "\"continuous\"" : "\"structural\"") + ",\n";
        out += "      \"spec_key\": " + quote(p.specKey) + ",\n";
        out += "      \"description\": " + quote(p.description) + "\n";
        out += i + 1 < registry.size() ? "    },\n" : "    }\n";
    }
    out += "  ]\n";
    out += "}\n";
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string text = render();
    if (argc < 2) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }
    std::FILE* f = std::fopen(argv[1], "wb");  // binary: keep LF on Windows
    if (f == nullptr) {
        std::fprintf(stderr, "pme_params_json: cannot open %s\n", argv[1]);
        return 1;
    }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const int closed = std::fclose(f);
    return (written == text.size() && closed == 0) ? 0 : 1;
}
