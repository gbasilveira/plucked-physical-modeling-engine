// SPDX-License-Identifier: MIT
// P0.3-T3 (harness side): the harness measures, summarises and writes the JSON format that
// tools/bench/compare.py reads.

#include <cstdint>
#include <nlohmann/json.hpp>
#include <pme/bench/bench.hpp>
#include <pme/test/tolerance.hpp>
#include <string>

namespace {

// A dependent chain (linear congruential step) that the compiler cannot fold into closed form.
void busy_work() {
    std::uint64_t x = 88172645463325252ULL;
    for (int i = 0; i < 2000; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    pme::bench::do_not_optimize(x);
}

}  // namespace

TEST_CASE("P0.3-T3 harness reports median and minimum and IQR per call") {
    pme::bench::Options opt;
    opt.repetitions = 12;
    opt.warmup = 2;
    opt.min_repetition_ns = 2e5;
    const pme::bench::Benchmark b{"busy", "samples", 2000.0, busy_work};
    const pme::bench::Result r = pme::bench::run_benchmark(b, opt);
    CHECK(r.name == "busy");
    CHECK(r.repetitions == 12);
    CHECK(r.inner_iterations >= 1);
    CHECK(r.min_ns > 0.0);
    CHECK(r.median_ns >= r.min_ns);
    CHECK(r.iqr_ns >= 0.0);
}

TEST_CASE("P0.3-T3 harness JSON has schema_version 1 and machine and build and results") {
    pme::bench::Options opt;
    opt.repetitions = 10;
    opt.warmup = 1;
    opt.min_repetition_ns = 1e5;
    const pme::bench::Benchmark b{"busy", "samples", 2000.0, busy_work};
    const std::vector<pme::bench::Result> results = {pme::bench::run_benchmark(b, opt)};
    const nlohmann::json doc =
        nlohmann::json::parse(pme::bench::to_json(results, opt, pme::bench::collect_info()));
    CHECK(doc.at("schema_version") == 1);
    CHECK(doc.at("machine").contains("cpu_model"));
    CHECK(doc.at("machine").at("logical_cores").get<unsigned>() >= 1U);
    CHECK_FALSE(doc.at("build").at("compiler").get<std::string>().empty());
    CHECK(doc.at("build").contains("preset"));
    CHECK(doc.at("settings").at("repetitions") == 10);
    REQUIRE(doc.at("results").size() == 1);
    const auto& entry = doc.at("results")[0];
    for (const char* key : {"name", "unit", "work_per_call", "repetitions", "inner_iterations",
                            "median_ns", "min_ns", "iqr_ns", "throughput_per_s"}) {
        CHECK(entry.contains(key));
    }
    CHECK(entry.at("median_ns").get<double>() > 0.0);
}

TEST_CASE("P0.3-T3 harness measures a slower function as slower") {
    pme::bench::Options opt;
    opt.repetitions = 10;
    opt.warmup = 2;
    opt.min_repetition_ns = 2e5;
    const pme::bench::Benchmark fast{"fast", "samples", 1.0, busy_work};
    const pme::bench::Benchmark slow{"slow", "samples", 1.0, [] {
                                         busy_work();
                                         busy_work();
                                         busy_work();
                                     }};
    // The minimum is the most stable estimator for "slower": scheduling noise on a loaded runner
    // only ever adds time. If the comparison fails, measure both again (up to two more times)
    // before failing; the 1.5x threshold for a 3x workload is unchanged.
    bool slower = false;
    double t_fast = 0.0;
    double t_slow = 0.0;
    for (int attempt = 0; attempt < 3 && !slower; ++attempt) {
        t_fast = pme::bench::run_benchmark(fast, opt).min_ns;
        t_slow = pme::bench::run_benchmark(slow, opt).min_ns;
        slower = t_slow > 1.5 * t_fast;
    }
    CHECK(t_slow > 1.5 * t_fast);
}
