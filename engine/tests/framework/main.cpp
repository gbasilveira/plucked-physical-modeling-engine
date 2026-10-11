// SPDX-License-Identifier: MIT
// doctest entry point shared by every doctest executable (P0.3-R1).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
