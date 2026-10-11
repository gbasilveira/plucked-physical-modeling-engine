// SPDX-License-Identifier: MIT
// pme_params_json: writes the parameter registry (pme/params.hpp) as JSON (P0.4-R4).
//
// Usage: pme_params_json [output-file]     (standard output when no file is given)
//
// The output is deterministic: entries sorted by id (the registry order), numbers printed with
// 12 significant digits (printf "%.12g"), LF line endings, no locale dependence (the program never
// calls setlocale).
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
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
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(12) << v;  // default float format: same as printf "%.12g"
    return os.str();
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
        std::cout << text;
        return std::cout.good() ? 0 : 1;
    }
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);  // binary: keep LF on Windows
    if (!file) {
        std::cerr << "pme_params_json: cannot open " << argv[1] << '\n';
        return 1;
    }
    file << text;
    file.close();
    return file.good() ? 0 : 1;
}
