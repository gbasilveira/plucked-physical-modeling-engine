// SPDX-License-Identifier: MIT
// Smoke benchmark (P0.3-R6): a trivial loop that proves the harness runs and writes valid JSON.

#include <cstdint>

#include <pme/bench/bench.hpp>

PME_BENCHMARK(smoke_sum_loop, "samples", 4096) {
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 4096; ++i) {
        sum += i * i;
    }
    pme::bench::do_not_optimize(sum);
}

int main(int argc, char** argv) {
    return pme::bench::run_main(argc, argv);
}
