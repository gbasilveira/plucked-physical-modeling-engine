# SPDX-License-Identifier: MIT
# B1 tests: instrument file loader (B1-T1 to B1-T3, B1-R3, B1-R4). Fixtures live in
# compiler/tests/data/b1 and are shared with the Python tests; the examples live in
# instruments/examples.

pme_add_doctest(pme_loader_tests
  SOURCES test_sha256.cpp test_roundtrip.cpp test_malformed.cpp test_migration.cpp test_robustness.cpp
  LIBRARIES pme_loader nlohmann_json::nlohmann_json
  LABELS unit)
# pme::toString(Status) is found by ADL and would hijack doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_loader_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS
  "PME_B1_FIXTURES=\"${PROJECT_SOURCE_DIR}/compiler/tests/data/b1\""
  "PME_B1_EXAMPLES=\"${PROJECT_SOURCE_DIR}/instruments/examples\"")
