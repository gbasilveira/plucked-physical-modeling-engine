# SPDX-License-Identifier: MIT
# A1 tests: coefficients (A1-T1, A1-T2, A1-R1, A1-R2), mode bank (A1-R3 to A1-R5), ramp (A1-R6, A1-T3)
# and the nightly benchmark.

pme_add_doctest(pme_osc_tests
  SOURCES test_coefficients.cpp test_mode_bank.cpp test_ramp.cpp test_chirp.cpp
  LIBRARIES pme_osc pme::engine nlohmann_json::nlohmann_json
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_osc_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS
  "PME_A1_T1_REFERENCE=\"${CMAKE_CURRENT_SOURCE_DIR}/a1_t1_reference.json\"")

# A1-R3: the update runs under ScopedRealtimeThread; with the rt-safety preset any allocation aborts.
pme_add_doctest(pme_osc_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_osc pme::engine pme_rtcheck
  LABELS unit
  RT)

pme_add_benchmark(pme_osc_bench SOURCES osc_bench.cpp LIBRARIES pme_osc)
target_compile_definitions(pme_osc_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)
