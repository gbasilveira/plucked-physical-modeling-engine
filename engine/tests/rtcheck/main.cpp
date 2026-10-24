// SPDX-License-Identifier: MIT
// Test driver for pme_rtcheck_tests. Doctest has no death tests, so the executable re-launches
// itself with --pme-rtcheck-child=<mode>; the child runs one scenario and either aborts (the trap)
// or returns the exit code kChildSurvived.
#define DOCTEST_CONFIG_IMPLEMENT
#include <cstdio>
#include <cstring>

#include "child_modes.hpp"
#include "doctest/doctest.h"

int main(int argc, char** argv) {
    constexpr const char* kFlag = "--pme-rtcheck-child=";
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], kFlag, std::strlen(kFlag)) == 0) {
            return pme::rtcheck::test::runChildMode(argv[i] + std::strlen(kFlag));
        }
    }
    doctest::Context context(argc, argv);
    return context.run();
}
