// SPDX-License-Identifier: MIT
// D3-R2: the generated Turtle names every registry entry exactly once with the registry's range and
// default; continuous entries are control ports, structural ones are patch:writable parameters;
// port indices match the shared header; identifiers come from ids.hpp.
#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

#include "pme/plugin/ids.hpp"
#include "ports.hpp"
#include "turtle.hpp"

using namespace pme::lv2;

namespace {

std::string plugin() {
    std::string error;
    std::string text = generatePlugin(error);
    REQUIRE_MESSAGE(!text.empty(), error);
    return text;
}

std::size_t occurrences(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

/// The text from `start` to the first `end` after it.
std::string between(const std::string& text, const std::string& start, const std::string& end) {
    const std::size_t a = text.find(start);
    REQUIRE(a != std::string::npos);
    const std::size_t b = text.find(end, a);
    REQUIRE(b != std::string::npos);
    return text.substr(a, b - a);
}

double numberAfter(const std::string& block, const std::string& key) {
    const std::size_t a = block.find(key);
    REQUIRE(a != std::string::npos);
    return std::strtod(block.c_str() + a + key.size(), nullptr);
}

}  // namespace

TEST_CASE("D3-R2 the generated Turtle names every continuous entry once as a control port") {
    const std::string ttl = plugin();
    std::uint32_t index = kFirstControlPort;
    for (const pme::ParamInfo& info : pme::paramRegistry()) {
        if (info.cls != pme::ParamClass::Continuous) {
            continue;
        }
        const std::string symbol = "lv2:symbol \"" + portSymbol(info) + "\"";
        CHECK(occurrences(ttl, symbol) == 1);
        // The port block: from its index to the closing bracket.
        const std::string block =
            between(ttl, "lv2:index " + std::to_string(index) + " ;\n        " + symbol, "\n    ]");
        CHECK(numberAfter(block, "lv2:minimum ") == info.min);
        CHECK(numberAfter(block, "lv2:maximum ") == info.max);
        CHECK(numberAfter(block, "lv2:default ") == info.defaultValue);
        CHECK(occurrences(ttl, "<" + structuralParamUri(info) + ">") == 0);
        ++index;
    }
    CHECK(index == kPortCount);
    CHECK(occurrences(ttl, "a lv2:InputPort, lv2:ControlPort") == kControlPortCount);
}

TEST_CASE(
    "D3-R2 the generated Turtle names every structural entry once as a patch:writable parameter") {
    const std::string ttl = plugin();
    std::size_t structural = 0;
    for (const pme::ParamInfo& info : pme::paramRegistry()) {
        if (info.cls != pme::ParamClass::Structural) {
            continue;
        }
        ++structural;
        const std::string uri = "<" + structuralParamUri(info) + ">";
        // Once as the definition (a line of its own) and once in patch:writable.
        CHECK(occurrences(ttl, "\n" + uri + "\n    a lv2:Parameter") == 1);
        CHECK(occurrences(ttl, uri) == 2);
        const std::string block = between(ttl, "\n" + uri + "\n", " .\n");
        CHECK(numberAfter(block, "lv2:minimum ") == info.min);
        CHECK(numberAfter(block, "lv2:maximum ") == info.max);
        CHECK(numberAfter(block, "lv2:default ") == info.defaultValue);
        CHECK(block.find("atom:Double") != std::string::npos);
        // Structural parameters are not ports: no symbol of theirs appears.
        CHECK(occurrences(ttl, "lv2:symbol \"" + portSymbol(info) + "\"") == 0);
    }
    CHECK(structural > 0);
    CHECK(occurrences(ttl, "a lv2:Parameter") == structural + 1);  // plus the instrument
    CHECK(occurrences(ttl, "<" + instrumentUri() + ">") == 2);
    CHECK(ttl.find("rdfs:range atom:Path") != std::string::npos);
}

TEST_CASE("D3-R2 port indices match the shared header and the fixed ports are described") {
    const std::string ttl = plugin();
    CHECK(ttl.find("lv2:index 0 ;\n        lv2:symbol \"in\"") != std::string::npos);
    CHECK(ttl.find("lv2:index 1 ;\n        lv2:symbol \"notify\"") != std::string::npos);
    CHECK(ttl.find("lv2:index 2 ;\n        lv2:symbol \"out_l\"") != std::string::npos);
    CHECK(ttl.find("lv2:index 3 ;\n        lv2:symbol \"out_r\"") != std::string::npos);
    static_assert(kPortIn == 0 && kPortNotify == 1 && kPortOutLeft == 2 && kPortOutRight == 3);
    static_assert(kFirstControlPort == 4);
    for (std::size_t i = 0; i < kControlPortCount; ++i) {
        const pme::ParamInfo& info = *pme::findParam(kControlPortParams[i]);
        CHECK(info.cls == pme::ParamClass::Continuous);
        CHECK(ttl.find("lv2:index " + std::to_string(kFirstControlPort + i) +
                       " ;\n        lv2:symbol \"" + portSymbol(info) + "\"") != std::string::npos);
    }
    // The pinned contract: these ids hold these ports (append-only policy, ports.hpp).
    CHECK(kControlPortParams[0] == 1001);
    CHECK(kControlPortParams[3] == 2001);
    CHECK(kControlPortParams[7] == 4003);
    // Every continuous registry entry is in the table, and nothing else.
    std::size_t continuous = 0;
    for (const pme::ParamInfo& info : pme::paramRegistry()) {
        continuous += info.cls == pme::ParamClass::Continuous ? 1U : 0U;
    }
    CHECK(continuous == kControlPortCount);
}

TEST_CASE("D3-R2 the plug-in description declares the features, extensions and ports") {
    const std::string ttl = plugin();
    CHECK(ttl.find("a lv2:Plugin, lv2:InstrumentPlugin") != std::string::npos);
    CHECK(ttl.find("doap:license <https://spdx.org/licenses/MIT>") != std::string::npos);
    CHECK(ttl.find("lv2:requiredFeature urid:map ;") != std::string::npos);
    CHECK(ttl.find("lv2:optionalFeature work:schedule, opts:options, log:log, lv2:hardRTCapable") !=
          std::string::npos);
    CHECK(ttl.find("lv2:extensionData state:interface, work:interface") != std::string::npos);
    CHECK(ttl.find("atom:supports midi:MidiEvent, patch:Message") != std::string::npos);
    CHECK(occurrences(ttl, "a lv2:AudioPort, lv2:OutputPort") == 2);
    CHECK(occurrences(ttl, "a lv2:InputPort, atom:AtomPort") == 1);
    CHECK(occurrences(ttl, "a lv2:OutputPort, atom:AtomPort") == 1);
    // Units map for dB and s.
    CHECK(ttl.find("units:unit units:db") != std::string::npos);
    CHECK(ttl.find("units:unit units:s") != std::string::npos);
}

TEST_CASE("D3-R2 the plug-in URI and the bundle names come from ids.hpp") {
    const std::string manifest = generateManifest(std::string(pme::plugin::kLv2BinaryName) + ".so");
    CHECK(manifest.find(std::string("<") + pme::plugin::kLv2PluginUri + ">") != std::string::npos);
    CHECK(manifest.find(std::string("lv2:binary <") + pme::plugin::kLv2BinaryName + ".so>") !=
          std::string::npos);
    CHECK(manifest.find("rdfs:seeAlso <" + pluginFileName() + ">") != std::string::npos);
    CHECK(pluginFileName() == std::string(pme::plugin::kLv2BinaryName) + ".ttl");
    const std::string ttl = plugin();
    CHECK(ttl.find(std::string("<") + pme::plugin::kLv2PluginUri + ">") != std::string::npos);
    CHECK(structuralParamUri(*pme::findParam(5001)).starts_with(pme::plugin::kLv2PluginUri));
    CHECK(std::string(pme::plugin::kLv2BundleDir).ends_with(".lv2"));
}
