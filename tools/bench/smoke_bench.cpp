// SPDX-License-Identifier: MIT
// Smoke benchmark (P0.3-R6): a trivial loop that proves the harness runs and writes valid JSON.

#include <cstdint>
#include <pme/bench/bench.hpp>

PME_BENCHMARK(smoke_sum_loop, "samples", 4096) {
    std::uint64_t x = 88172645463325252ULL;
    for (int i = 0; i < 4096; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    pme::bench::do_not_optimize(x);
}

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
