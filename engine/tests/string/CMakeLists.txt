# SPDX-License-Identifier: MIT
# A2 tests: wound string (A2-T1, A2-R1), modes and truncation (A2-T2, A2-R3 to A2-R5), tuning
# (A2-R2), shapes and windows (A2-R6), mute (A2-R7) and the nightly benchmark.

pme_add_doctest(pme_string_tests
  SOURCES test_wound.cpp test_tuning.cpp test_modes.cpp test_shapes.cpp test_mute.cpp
  LIBRARIES pme_string pme::engine
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_string_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_string_bench SOURCES string_bench.cpp LIBRARIES pme_string pme::engine)
