// SPDX-License-Identifier: MIT
// pme_golden_compare <reference.json> <candidate.json>
// Compares two golden-feature files (P0.3-R4). Exit 0: every feature within tolerance. Exit 1: at
// least one feature outside tolerance (listed on stdout). Exit 2: usage or file error.

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "pme/analysis/golden.hpp"

int main(int argc, char** argv) {
    bool verbose = false;
    std::array<std::string, 2> paths;
    std::size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (count < 2) {
            paths[count++] = std::string(arg);
        } else {
            count = 3;
        }
    }
    if (count != 2) {
        std::cerr << "usage: pme_golden_compare [-v] <reference.json> <candidate.json>\n"
                  << "  exit 0: all features within tolerance, 1: outside tolerance, 2: error\n";
        return 2;
    }
    try {
        const auto reference = pme::analysis::read_golden_file(paths[0]);
        const auto candidate = pme::analysis::read_golden_file(paths[1]);
        const auto result = pme::analysis::compare_golden(reference, candidate);
        std::cout << pme::analysis::format_comparison(result, !verbose);
        return result.pass ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "pme_golden_compare: " << e.what() << '\n';
        return 2;
    }
}
