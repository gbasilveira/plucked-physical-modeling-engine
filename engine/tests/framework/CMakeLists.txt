# SPDX-License-Identifier: MIT
# Test framework (P0.3-R1): doctest main and tolerance helpers. Never linked into the engine.

add_library(pme_test_framework STATIC main.cpp)
add_library(pme::test_framework ALIAS pme_test_framework)
target_include_directories(pme_test_framework PUBLIC include)
target_link_libraries(pme_test_framework PUBLIC doctest::doctest PRIVATE pme_build_options)
set_target_properties(pme_test_framework PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

pme_add_doctest(pme_framework_tests SOURCES framework_tests.cpp LABELS unit RT)
