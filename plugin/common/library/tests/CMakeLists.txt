# SPDX-License-Identifier: MIT
# D5 tests (D5-R1 to D5-R5, D5-T1 to D5-T4). Every test uses fresh temporary directories through an
# explicit Locations; none reads or writes the real user folders. Fixtures are copied at test time
# from instruments/examples and compiler/tests/data/b1.

pme_add_doctest(pme_plugin_library_tests
  SOURCES test_locations.cpp test_presets.cpp test_instruments.cpp test_cache.cpp
          test_integrity.cpp
  LIBRARIES pme_plugin_library nlohmann_json::nlohmann_json
  LABELS unit)
target_include_directories(pme_plugin_library_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# pme::toString(Status) is found by ADL and would hijack doctest's unqualified toString().
target_compile_definitions(pme_plugin_library_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS
  "PME_B1_FIXTURES=\"${PROJECT_SOURCE_DIR}/compiler/tests/data/b1\""
  "PME_B1_EXAMPLES=\"${PROJECT_SOURCE_DIR}/instruments/examples\"")
