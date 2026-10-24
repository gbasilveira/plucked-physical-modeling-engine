// SPDX-License-Identifier: MIT
// Covers: A2-R5
// Compile time of one bass and one treble string at 48 kHz with 4 registered points. Nightly
// (perf); no baseline file until V2 and decision D-4.
#include <array>

#include "fixtures.hpp"
#include "pme/bench/bench.hpp"
#include "pme/string.hpp"

namespace {

constexpr double kFs = 48000.0;
constexpr double kFMax = 16000.0;

void compileOne(const pme::StringParams& p) {
    const std::array<double, 4> pts{0.05 * p.L, 0.12 * p.L, 0.5 * p.L, 0.91 * p.L};
    pme::StringModes out;
    const pme::Status s = pme::compileString(p, kFs, kFMax, pts, out);
    pme::bench::do_not_optimize(s);
    pme::bench::do_not_optimize(out.tension);
}

}  // namespace

PME_BENCHMARK(string_compile_bass, "strings", 1.0) { compileOne(pme::string_test::bass()); }

PME_BENCHMARK(string_compile_treble, "strings", 1.0) { compileOne(pme::string_test::treble()); }

int main(int argc, char** argv) { return pme::bench::run_main(argc, argv); }
