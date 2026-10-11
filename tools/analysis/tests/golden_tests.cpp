// SPDX-License-Identifier: MIT
// P0.3-T2: the golden-feature comparator passes identical features and flags a 1% decay change;
// P0.3-R4: file format round trip, per-partial overrides and structural failures.

#include <numbers>
#include <pme/analysis/golden.hpp>
#include <pme/test/tolerance.hpp>
#include <string>

using namespace pme::analysis;

namespace {

constexpr double kContactTime = 0.0031;

GoldenFeatures make_reference() {
    GoldenFeatures g;
    g.scenario = "unit-test";
    g.sample_rate_hz = 48000.0;
    g.producer_tool = "pme-tests";
    g.producer_version = "0";
    for (int n = 1; n <= 3; ++n) {
        GoldenPartial p;
        p.index = n;
        p.frequency_hz = 110.0 * n;
        p.amplitude = 1.0 / n;
        p.sigma_per_s = 0.5 + 0.25 * n;
        p.t60_s = 3.0 * std::numbers::ln10 / p.sigma_per_s;
        g.partials.push_back(p);
    }
    g.partials[1].second_decay = GoldenDecay2{0.2, 1.5};
    GoldenEnvelope env;
    env.frequencies_hz = {100.0, 200.0, 400.0};
    env.magnitude_db = {-20.0, -25.0, -40.0};
    g.spectral_envelope = env;
    g.contact_time_s = kContactTime;
    g.energy = GoldenEnergy{1e-12, 2e-13};
    return g;
}

const FeatureResult* find(const ComparisonResult& r, const std::string& feature) {
    for (const FeatureResult& f : r.results) {
        if (f.feature == feature) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("P0.3-T2 identical features pass") {
    const GoldenFeatures ref = make_reference();
    const GoldenFeatures copy = parse_golden(serialize_golden(ref));
    const ComparisonResult r = compare_golden(ref, copy);
    CHECK(r.pass);
    CHECK(r.results.size() > 10);
    for (const FeatureResult& f : r.results) {
        CHECK(f.pass);
        CHECK(f.difference == 0.0);
    }
}

TEST_CASE("P0.3-T2 a 1 percent decay change fails and names the partial and the feature") {
    const GoldenFeatures ref = make_reference();
    GoldenFeatures cand = ref;
    cand.partials[1].sigma_per_s *= 1.01;
    const ComparisonResult r = compare_golden(ref, cand);
    CHECK_FALSE(r.pass);
    const FeatureResult* f = find(r, "partial[2].decay_rate");
    REQUIRE(f != nullptr);
    CHECK_FALSE(f->pass);
    PME_CHECK_REL(f->difference, 0.01, 1e-9);
    CHECK(f->tolerance == 0.005);
    CHECK(f->unit == "relative");
    for (const FeatureResult& other : r.results) {
        if (other.feature != "partial[2].decay_rate") {
            CHECK(other.pass);
        }
    }
    const std::string text = format_comparison(r);
    CHECK(text.find("partial[2].decay_rate") != std::string::npos);
    CHECK(text.find("FAIL") != std::string::npos);
}

TEST_CASE("P0.3-T2 a 0.4 percent decay change passes at the 0.5 percent default") {
    const GoldenFeatures ref = make_reference();
    GoldenFeatures cand = ref;
    cand.partials[1].sigma_per_s *= 1.004;
    CHECK(compare_golden(ref, cand).pass);
}

TEST_CASE("P0.3-R4 each default tolerance flags a change just outside it") {
    const GoldenFeatures ref = make_reference();
    {
        GoldenFeatures c = ref;
        c.partials[0].frequency_hz *= std::exp2(0.011 / 1200.0);
        const auto r = compare_golden(ref, c);
        REQUIRE(find(r, "partial[1].frequency") != nullptr);
        CHECK_FALSE(find(r, "partial[1].frequency")->pass);
        c.partials[0].frequency_hz = ref.partials[0].frequency_hz * std::exp2(0.009 / 1200.0);
        CHECK(compare_golden(ref, c).pass);
    }
    {
        GoldenFeatures c = ref;
        c.partials[2].amplitude *= std::pow(10.0, 0.11 / 20.0);
        CHECK_FALSE(compare_golden(ref, c).pass);
    }
    {
        GoldenFeatures c = ref;
        if (c.spectral_envelope) {
            c.spectral_envelope->magnitude_db[1] += 1.1;
        }
        const auto r = compare_golden(ref, c);
        REQUIRE(find(r, "spectral_envelope") != nullptr);
        CHECK_FALSE(find(r, "spectral_envelope")->pass);
    }
    {
        GoldenFeatures c = ref;
        c.contact_time_s = kContactTime * 1.011;
        CHECK_FALSE(compare_golden(ref, c).pass);
        c.contact_time_s = kContactTime * 1.009;
        CHECK(compare_golden(ref, c).pass);
    }
    {
        GoldenFeatures c = ref;
        if (c.energy) {
            c.energy->max_relative_drift += 2e-9;
        }
        CHECK_FALSE(compare_golden(ref, c).pass);
    }
}

TEST_CASE("P0.3-R4 per-partial tolerance overrides replace the default") {
    GoldenFeatures ref = make_reference();
    PartialTolerance loose;
    loose.decay_rate_relative = 0.05;
    ref.tolerances.partials[2] = loose;
    GoldenFeatures cand = ref;
    cand.partials[1].sigma_per_s *= 1.04;
    CHECK(compare_golden(ref, cand).pass);
    cand.partials[0].sigma_per_s *= 1.04;  // partial 1 keeps the default
    const auto r = compare_golden(ref, cand);
    CHECK_FALSE(r.pass);
    CHECK_FALSE(find(r, "partial[1].decay_rate")->pass);

    // The overrides survive a serialisation round trip.
    const GoldenFeatures again = parse_golden(serialize_golden(ref));
    REQUIRE(again.tolerances.partials.count(2) == 1);
    CHECK(again.tolerances.partials.at(2).decay_rate_relative == 0.05);
    CHECK_FALSE(again.tolerances.partials.at(2).frequency_cents.has_value());
}

TEST_CASE("P0.3-R4 missing features and mismatched grids fail") {
    const GoldenFeatures ref = make_reference();
    GoldenFeatures c = ref;
    c.partials.pop_back();
    auto r = compare_golden(ref, c);
    CHECK_FALSE(r.pass);
    REQUIRE(find(r, "partial[3]") != nullptr);
    CHECK(find(r, "partial[3]")->note.find("missing") != std::string::npos);

    c = ref;
    if (c.spectral_envelope) {
        c.spectral_envelope->frequencies_hz[1] = 201.0;
    }
    CHECK_FALSE(compare_golden(ref, c).pass);

    c = ref;
    c.contact_time_s.reset();
    c.energy.reset();
    c.partials[1].second_decay.reset();
    r = compare_golden(ref, c);
    CHECK_FALSE(r.pass);

    // Extra candidate partials are reported, not failed.
    c = ref;
    GoldenPartial extra = ref.partials[0];
    extra.index = 9;
    c.partials.push_back(extra);
    r = compare_golden(ref, c);
    CHECK(r.pass);
    CHECK(r.notes.size() == 1);
}

TEST_CASE("P0.3-R4 parser rejects malformed files naming the field") {
    CHECK_THROWS_AS(parse_golden("{"), GoldenError);
    CHECK_THROWS_WITH_AS(parse_golden("{}"), doctest::Contains("schema_version"), GoldenError);
    CHECK_THROWS_WITH_AS(parse_golden(R"({"schema_version": 2})"),
                         doctest::Contains("schema_version"), GoldenError);
    std::string text = serialize_golden(make_reference());
    const auto pos = text.find("\"scenario\"");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 10, "\"scenari0\"");
    CHECK_THROWS_WITH_AS(parse_golden(text), doctest::Contains("scenario"), GoldenError);
}
