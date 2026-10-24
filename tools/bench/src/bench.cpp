// SPDX-License-Identifier: MIT
#include "pme/bench/bench.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

#ifndef PME_BENCH_BUILD_TYPE
#define PME_BENCH_BUILD_TYPE "unknown"
#endif
#ifndef PME_BENCH_PRESET
#define PME_BENCH_PRESET "unknown"
#endif

namespace pme::bench {

namespace {

using Clock = std::chrono::steady_clock;

double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.size() == 1) {
        return sorted.front();
    }
    const double pos = p * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

double elapsed_ns(const std::function<void()>& fn, long long calls) {
    const auto start = Clock::now();
    for (long long i = 0; i < calls; ++i) {
        fn();
    }
    const auto stop = Clock::now();
    return std::chrono::duration<double, std::nano>(stop - start).count();
}

std::string cpu_model() {
#if defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("model name") || line.starts_with("Model") ||
            line.starts_with("Hardware")) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                const auto start = line.find_first_not_of(" \t", colon + 1);
                if (start != std::string::npos) {
                    return line.substr(start);
                }
            }
        }
    }
#elif defined(__APPLE__)
    char buffer[256];
    std::size_t size = sizeof buffer;
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0 && size > 0) {
        return std::string(buffer);
    }
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, static_cast<int>(0x80000000U));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004U) {
        char brand[49] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, static_cast<int>(0x80000002U + static_cast<unsigned>(i)));
            std::copy_n(reinterpret_cast<const char*>(regs), 16, brand + 16 * i);
        }
        std::string text(brand);
        const auto first = text.find_first_not_of(' ');
        return first == std::string::npos ? std::string("unknown") : text.substr(first);
    }
#endif
    return "unknown";
}

}  // namespace

std::vector<Benchmark>& registry() {
    static std::vector<Benchmark> benchmarks;
    return benchmarks;
}

Registrar::Registrar(const char* name, const char* unit, double work_per_call, void (*fn)()) {
    registry().push_back({name, unit, work_per_call, fn});
}

Result run_benchmark(const Benchmark& benchmark, const Options& options) {
    Result result;
    result.name = benchmark.name;
    result.unit = benchmark.unit;
    result.work_per_call = benchmark.work_per_call;
    const int reps = std::max(options.repetitions, 1);
    result.repetitions = reps;

    // Calibrate: calls per repetition so that a repetition lasts at least min_repetition_ns. Grow
    // the count until a measured repetition reaches the target; one timed call is not trusted,
    // because on a coarse clock (Windows: 100 ns ticks) a single call can read as zero or as a
    // cold-start outlier.
    constexpr long long kMaxInner = 1'000'000'000LL;
    long long inner = 1;
    while (inner < kMaxInner) {
        const double measured = elapsed_ns(benchmark.fn, inner);
        if (measured >= options.min_repetition_ns) {
            break;
        }
        const double scale = options.min_repetition_ns / std::max(measured, 1.0);
        const double grown = std::min(std::ceil(static_cast<double>(inner) * scale),
                                      static_cast<double>(inner) * 10.0);
        inner =
            std::clamp<long long>(std::max(static_cast<long long>(grown), inner + 1), 1, kMaxInner);
    }
    result.inner_iterations = inner;

    for (int i = 0; i < options.warmup; ++i) {
        elapsed_ns(benchmark.fn, inner);
    }
    std::vector<double> per_call;
    per_call.reserve(static_cast<std::size_t>(reps));
    for (int i = 0; i < reps; ++i) {
        per_call.push_back(elapsed_ns(benchmark.fn, inner) / static_cast<double>(inner));
    }
    std::sort(per_call.begin(), per_call.end());
    result.min_ns = per_call.front();
    result.median_ns = percentile(per_call, 0.5);
    result.iqr_ns = percentile(per_call, 0.75) - percentile(per_call, 0.25);
    return result;
}

Info collect_info() {
    Info info;
#if defined(_WIN32)
    info.os = "windows";
#elif defined(__APPLE__)
    info.os = "macos";
#elif defined(__linux__)
    info.os = "linux";
#else
    info.os = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    info.arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    info.arch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    info.arch = "x86";
#else
    info.arch = "unknown";
#endif
    info.cpu_model = cpu_model();
    info.logical_cores = std::thread::hardware_concurrency();
#if defined(__clang__)
    info.compiler = "clang";
    info.compiler_version = __clang_version__;
#elif defined(__GNUC__)
    info.compiler = "gcc";
    info.compiler_version = __VERSION__;
#elif defined(_MSC_VER)
    info.compiler = "msvc";
    info.compiler_version = std::to_string(_MSC_VER);
#else
    info.compiler = "unknown";
#endif
    info.build_type = PME_BENCH_BUILD_TYPE;
    info.preset = PME_BENCH_PRESET;
    info.cxx_standard = static_cast<long>(__cplusplus);
    return info;
}

std::string to_json(const std::vector<Result>& results, const Options& options, const Info& info) {
    using Json = nlohmann::ordered_json;
    Json doc = Json::object();
    doc["schema_version"] = 1;
    doc["machine"] = {{"os", info.os},
                      {"arch", info.arch},
                      {"cpu_model", info.cpu_model},
                      {"logical_cores", info.logical_cores}};
    doc["build"] = {{"compiler", info.compiler},
                    {"compiler_version", info.compiler_version},
                    {"build_type", info.build_type},
                    {"preset", info.preset},
                    {"cxx_standard", info.cxx_standard}};
    doc["settings"] = {{"repetitions", options.repetitions},
                       {"warmup", options.warmup},
                       {"min_repetition_ns", options.min_repetition_ns}};
    Json list = Json::array();
    for (const Result& r : results) {
        Json entry = {
            {"name", r.name},
            {"unit", r.unit},
            {"work_per_call", r.work_per_call},
            {"repetitions", r.repetitions},
            {"inner_iterations", r.inner_iterations},
            {"median_ns", r.median_ns},
            {"min_ns", r.min_ns},
            {"iqr_ns", r.iqr_ns},
            {"throughput_per_s", r.median_ns > 0.0 ? r.work_per_call * 1e9 / r.median_ns : 0.0}};
        list.push_back(entry);
    }
    doc["results"] = list;
    return doc.dump(2) + "\n";
}

int run_main(int argc, char** argv) {
    Options options;
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--repetitions") {
            options.repetitions = std::atoi(value("--repetitions"));
        } else if (arg == "--warmup") {
            options.warmup = std::atoi(value("--warmup"));
        } else if (arg == "--min-time-ms") {
            options.min_repetition_ns = std::atof(value("--min-time-ms")) * 1e6;
        } else if (arg == "--filter") {
            options.filter = value("--filter");
        } else if (arg == "--out") {
            options.out_path = value("--out");
        } else if (arg == "--list") {
            list_only = true;
        } else {
            std::cerr << "usage: " << argv[0]
                      << " [--repetitions N] [--warmup N] [--min-time-ms X] [--filter TEXT]"
                         " [--out FILE|-] [--list]\n";
            return 2;
        }
    }
    if (options.repetitions < 1) {
        std::cerr << "--repetitions must be >= 1\n";
        return 2;
    }

    std::vector<Result> results;
    for (const Benchmark& b : registry()) {
        if (!options.filter.empty() && b.name.find(options.filter) == std::string::npos) {
            continue;
        }
        if (list_only) {
            std::cout << b.name << '\n';
            continue;
        }
        const Result r = run_benchmark(b, options);
        std::cerr << std::left << std::setw(32) << r.name << std::right << std::fixed
                  << std::setprecision(1) << " median " << std::setw(12) << r.median_ns
                  << " ns  min " << std::setw(12) << r.min_ns << " ns  iqr " << std::setw(10)
                  << r.iqr_ns << " ns  (" << r.inner_iterations << " calls x " << r.repetitions
                  << ")\n";
        results.push_back(r);
    }
    if (list_only) {
        return 0;
    }
    const std::string json = to_json(results, options, collect_info());
    if (options.out_path == "-") {
        std::cout << json;
    } else if (!options.out_path.empty()) {
        std::ofstream out(options.out_path, std::ios::binary | std::ios::trunc);
        out << json;
        if (!out) {
            std::cerr << "cannot write " << options.out_path << '\n';
            return 2;
        }
    }
    return 0;
}

}  // namespace pme::bench
