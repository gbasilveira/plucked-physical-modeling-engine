// SPDX-License-Identifier: MIT
// Benchmark harness (P0.3-R6): registration macro, warm-up, N repetitions, median / minimum /
// inter-quartile range per call in nanoseconds, JSON output with machine and build information.
// Never linked into the engine. Regressions are judged by tools/bench/compare.py (5% rule).
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pme::bench {

struct Options {
    int repetitions = 11;            // timed repetitions per benchmark (>= 10 by default)
    int warmup = 3;                  // untimed repetitions before timing
    double min_repetition_ns = 1e6;  // each repetition runs enough calls to last at least this long
    std::string filter;              // run only benchmarks whose name contains this text
    std::string out_path;            // JSON output file; "-" for standard output; empty for none
};

struct Result {
    std::string name;
    std::string unit;            // work unit per call, for example "samples"
    double work_per_call = 1.0;  // units of work done by one call
    int repetitions = 0;
    long long inner_iterations = 0;  // calls per timed repetition
    double median_ns = 0.0;          // per call
    double min_ns = 0.0;
    double iqr_ns = 0.0;  // Q3 - Q1 over the repetitions
};

struct Info {
    std::string os;
    std::string arch;
    std::string cpu_model;
    unsigned logical_cores = 0;
    std::string compiler;
    std::string compiler_version;
    std::string build_type;
    std::string preset;
    long cxx_standard = 0;
};

struct Benchmark {
    std::string name;
    std::string unit;
    double work_per_call = 1.0;
    std::function<void()> fn;
};

// Registry of benchmarks defined with PME_BENCHMARK.
std::vector<Benchmark>& registry();

struct Registrar {
    Registrar(const char* name, const char* unit, double work_per_call, void (*fn)());
};

// Keeps `value` observable so the compiler cannot delete the work that produced it.
template <class T>
inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    static volatile const void* sink;
    sink = &value;
    (void)sink;
#endif
}

// Runs one benchmark: warm-up, calibration of the inner loop, then `repetitions` timed repetitions.
Result run_benchmark(const Benchmark& benchmark, const Options& options);

// Machine and build information; the CPU model is read without privileges ("unknown" if not found).
Info collect_info();

// The JSON document: {"schema_version": 1, "machine": ..., "build": ..., "settings": ...,
// "results": [...]}.
std::string to_json(const std::vector<Result>& results, const Options& options, const Info& info);

// Entry point for a benchmark executable: --repetitions N, --warmup N, --min-time-ms X, --filter
// TEXT,
// --out FILE, --list. Returns the process exit code.
int run_main(int argc, char** argv);

}  // namespace pme::bench

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): static registration needs a macro.
#define PME_BENCHMARK(identifier, unit_text, work_per_call)                 \
    static void pme_bench_fn_##identifier();                                \
    static const ::pme::bench::Registrar pme_bench_registrar_##identifier(  \
        #identifier, unit_text, work_per_call, &pme_bench_fn_##identifier); \
    static void pme_bench_fn_##identifier()
