// SPDX-License-Identifier: MIT
// D1-R3: registry-backed parameter model, clamping, value-to-text, module paths.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "pme/plugin/param_model.hpp"

using pme::plugin::ParamModel;

TEST_CASE("D1-R3 one entry per registry entry with registry defaults") {
    ParamModel m;
    REQUIRE(m.count() == pme::paramRegistry().size());
    for (std::size_t i = 0; i < m.count(); ++i) {
        CHECK(m.info(i).id == pme::paramRegistry()[i].id);
        CHECK(m.value(i) == m.info(i).defaultValue);
        CHECK(m.indexOf(m.info(i).id) == i);
    }
    CHECK(m.indexOf(1010) == m.count());  // unused id (ICR-0001)
    CHECK(m.indexOf(0) == m.count());
}

TEST_CASE("D1-R3 values are clamped to the registry range") {
    ParamModel m;
    const std::size_t gain = m.indexOf(2001);  // output.gain, -96 .. 24 dB
    REQUIRE(gain < m.count());
    CHECK(m.setValue(gain, 100.0) == 24.0);
    CHECK(m.value(gain) == 24.0);
    CHECK(m.setValue(gain, -1000.0) == -96.0);
    CHECK(m.setValue(gain, -6.5) == -6.5);
    CHECK(m.setValue(gain, std::nan("")) == -6.5);                // NaN is ignored
    CHECK(ParamModel::clamp(m.info(gain), std::nan("")) == 0.0);  // NaN becomes the default
    m.resetToDefaults();
    CHECK(m.value(gain) == 0.0);
}

TEST_CASE("D1-R3 value to text shows the registry unit and text parses back") {
    ParamModel m;
    std::array<char, 64> buf{};
    const pme::ParamInfo& gain = m.info(m.indexOf(2001));
    ParamModel::valueToText(gain, -6.5, buf.data(), buf.size());
    CHECK(std::strcmp(buf.data(), "-6.5 dB") == 0);
    double v = 0.0;
    REQUIRE(ParamModel::textToValue(gain, buf.data(), v));
    CHECK(v == -6.5);
    REQUIRE(ParamModel::textToValue(gain, "12", v));
    CHECK(v == 12.0);
    CHECK_FALSE(ParamModel::textToValue(gain, "loud", v));

    const pme::ParamInfo& sustain = m.info(m.indexOf(1001));  // unit "1": no suffix
    ParamModel::valueToText(sustain, 0.25, buf.data(), buf.size());
    CHECK(std::strcmp(buf.data(), "0.25") == 0);
    // Truncation keeps the NUL terminator.
    std::array<char, 4> tiny{};
    ParamModel::valueToText(gain, -6.5, tiny.data(), tiny.size());
    CHECK(tiny[3] == '\0');
}

TEST_CASE("D1-R3 structural parameters are marked in the module path") {
    ParamModel m;
    CHECK(ParamModel::modulePath(m.info(m.indexOf(4001))) == "damper");
    CHECK(ParamModel::modulePath(m.info(m.indexOf(2001))) == "output");
    CHECK(ParamModel::modulePath(m.info(m.indexOf(6003))) == "structural/strings");
    for (std::size_t i = 0; i < m.count(); ++i) {
        const bool structural = m.info(i).cls == pme::ParamClass::Structural;
        CHECK(ParamModel::modulePath(m.info(i)).starts_with("structural") == structural);
    }
}
