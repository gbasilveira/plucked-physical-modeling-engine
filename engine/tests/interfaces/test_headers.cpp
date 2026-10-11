// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>

#include "pme/version.hpp"

TEST_CASE("P0.4-T1 every public header compiles on its own") {
    // The CMake glob generated one translation unit per header (each including it twice and
    // nothing else); they are part of this executable, so reaching this point means they compiled.
    std::size_t headers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(PME_INCLUDE_PME_DIR)) {
        if (entry.path().extension() == ".hpp") {
            ++headers;
        }
    }
    CHECK(headers > 0);
    CHECK(headers == static_cast<std::size_t>(PME_T1_GENERATED_TU_COUNT));
}

TEST_CASE("P0.4-T1 ICR-0001 interfaces version is 2") { CHECK(pme::interfacesVersionMajor == 2); }
