// SPDX-License-Identifier: MIT
// P0.5-R7 scenarios, features, WAV, energy trace and the refinement tool.
#include <doctest/doctest.h>

#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

#include "pme/analysis/golden.hpp"
#include "pme/ref/features.hpp"
#include "pme/ref/refine.hpp"
#include "pme/ref/scenario.hpp"
#include "test_common.hpp"

using namespace pme::ref;
using namespace pme::ref::test;

namespace {

std::string scenario_path(const char* name) {
    return std::string(PME_REFERENCE_SCENARIOS) + "/" + name;
}

}  // namespace

TEST_CASE("P0.5-R7 example scenarios parse") {
    const Scenario a = read_scenario_file(scenario_path("string_pluck.json"));
    CHECK(a.name == "string-pluck");
    CHECK(a.strings.size() == 1);
    CHECK(a.strings[0].intervals == 130);
    CHECK(a.strings[0].params.f1Target == doctest::Approx(196.0));
    CHECK(a.strings[0].initial.enabled);
    const Scenario b = read_scenario_file(scenario_path("hammer_two_strings_bridge.json"));
    CHECK(b.strings.size() == 2);
    CHECK(b.hammers.size() == 1);
    CHECK(b.bridges.size() == 1);
    CHECK(b.bridges[0].desc.thetaB == doctest::Approx(0.05));
    CHECK(b.observations.size() == 4);
}

TEST_CASE("P0.5-R7 scenario errors name the offending field") {
    CHECK_THROWS_WITH_AS(parse_scenario("{\"schema_version\": 2, \"fs\": 1, \"duration\": 1}"),
                         doctest::Contains("schema_version"), RefError);
    CHECK_THROWS_WITH_AS(
        parse_scenario("{\"schema_version\": 1, \"fs\": 48000, \"duration\": 1, \"bogus\": 1}"),
        doctest::Contains("scenario.bogus"), RefError);
    CHECK_THROWS_WITH_AS(
        parse_scenario("{\"schema_version\": 1, \"fs\": 48000, \"duration\": 1, \"strings\": "
                       "[{\"params\": {\"L\": 1, \"dCore\": 0.001, \"rhoCore\": 7850, "
                       "\"ECore\": 2e11, \"f1Target\": 100, \"typo\": 1}}]}"),
        doctest::Contains("scenario.strings[0].params.typo"), RefError);
    CHECK_THROWS_WITH_AS(parse_scenario("not json"), doctest::Contains("invalid JSON"), RefError);
    // b2 != 0 is parsed but rejected when the string is derived.
    const Scenario sc = parse_scenario(
        "{\"schema_version\": 1, \"fs\": 48000, \"duration\": 0.01, \"strings\": [{\"params\": "
        "{\"L\": 1, \"dCore\": 0.001, \"rhoCore\": 7850, \"ECore\": 2e11, \"f1Target\": 100, "
        "\"b2\": 0.01}, \"initial_displacement\": {\"amplitude\": 0.001, \"position\": 0.3}}]}");
    CHECK_THROWS_WITH_AS(simulate(sc), doctest::Contains("b2"), RefError);
    // Unmodelled plate features are named.
    const Scenario plate = parse_scenario(
        "{\"schema_version\": 1, \"fs\": 48000, \"duration\": 0.01, \"plate\": "
        "{\"length_x\": 1, \"length_y\": 1, \"thickness\": 0.003, \"youngs_l\": 1e10, "
        "\"youngs_r\": 1e9, \"shear_lr\": 1e9, \"poisson_lr\": 0.3, \"poisson_rl\": 0.03, "
        "\"density\": 400, \"ribs\": [{}]}}");
    CHECK_THROWS_WITH_AS(simulate(plate), doctest::Contains("ribs"), RefError);
}

TEST_CASE("P0.5-R7 render produces golden features that the P0.3 comparator reads") {
    Scenario sc = read_scenario_file(scenario_path("string_pluck.json"));
    sc.duration = 0.4;
    const Rendered r = render_scenario(sc);
    const std::string text = pme::analysis::serialize_golden(r.features);
    const auto parsed = pme::analysis::parse_golden(text);
    CHECK(parsed.partials.size() >= 3);
    // The file compares equal to itself within every default tolerance.
    CHECK(pme::analysis::compare_golden(parsed, parsed).pass);
    // Partial 1 sits at the scheme's frequency, close to the SPEC-STR value (0.1% at this grid).
    const StringPhysics ph = derive_string(sc.strings[0].params);
    REQUIRE(!parsed.partials.empty());
    CHECK(parsed.partials[0].index == 1);
    CHECK(parsed.partials[0].frequency_hz ==
          doctest::Approx(stiff_string_frequency_hz(ph, 1)).epsilon(1e-3));
    CHECK(parsed.energy.has_value());
    CHECK(parsed.spectral_envelope.has_value());
    CHECK(parsed.producer_tool == "pme_reference");
}

TEST_CASE("P0.5-R7 hammer scenarios report a contact time") {
    Scenario sc = read_scenario_file(scenario_path("hammer_two_strings_bridge.json"));
    sc.duration = 0.1;
    const Rendered r = render_scenario(sc);
    const double contact_time = r.features.contact_time_s.value_or(0.0);
    CHECK(contact_time > 1e-4);
    CHECK(contact_time < 1e-2);
}

TEST_CASE("P0.5-R7 WAV and energy outputs") {
    Scenario sc = read_scenario_file(scenario_path("string_pluck.json"));
    sc.duration = 0.1;
    const Rendered r = render_scenario(sc);
    const auto wav = results_to_wav(r.results, false);
    CHECK(wav.channels == 1);
    CHECK(wav.sample_rate_hz == 48000.0);
    CHECK(wav.format == pme::analysis::WavFormat::float32);
    CHECK(wav.frames() == r.results.steps / static_cast<std::size_t>(r.results.oversampling));
    const auto norm = results_to_wav(r.results, true);
    double peak = 0.0;
    for (const double v : norm.samples) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak == doctest::Approx(0.9));
    const auto ej = nlohmann::json::parse(energy_to_json(r.results));
    CHECK(ej["steps"].get<std::size_t>() == r.results.steps);
    CHECK(ej["total"].size() == r.results.steps + 1);
    CHECK(ej["summary"]["max_relative_imbalance"].get<double>() < 1e-12);
    CHECK(ej["balance"].size() == r.results.steps);
}

TEST_CASE("P0.5-R7 refinement tool reports second-order convergence") {
    const StringParams p = steel_string(1.0, 110.0, 1e-3);
    Scenario sc = string_scenario(p, 32, 8000.0, 0.4, 0.31, 0.27);
    const RefinementReport rep = refine_partial_frequencies(sc, {32, 64, 128}, 4);
    REQUIRE(rep.levels.size() == 3);
    CHECK(rep.levels[2].oversampling == 4);
    CHECK(rep.grid_ratio == doctest::Approx(2.0));
    for (const double order : rep.observed_order) {
        CHECK(order > 1.8);
        CHECK(order < 2.2);
    }
    for (std::size_t i = 0; i < rep.extrapolated_hz.size(); ++i) {
        // Richardson extrapolation lands closer to the analytic value than the finest grid.
        CHECK(std::fabs(rep.extrapolated_hz[i] / rep.analytic_hz[i] - 1.0) <
              std::fabs(rep.levels[2].frequencies_hz[i] / rep.analytic_hz[i] - 1.0));
    }
    const auto j = nlohmann::json::parse(refinement_to_json(rep));
    CHECK(j["levels"].size() == 3);
    CHECK_THROWS_WITH_AS(refine_partial_frequencies(sc, {32, 64, 100}, 4),
                         doctest::Contains("constant integer ratio"), RefError);
}
