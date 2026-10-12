# SPDX-License-Identifier: MIT
# A6 tests: controller and timing (A6-T2, A6-R3 to A6-R5), diagonal damper (A6-R1, A6-T3), rank-one
# damper (A6-R2, A6-T1), engagement ramps (A6-T4), the example instrument's overdamping check,
# real-time safety and the nightly benchmark. ORC-13 is the test-author's, under
# engine/tests/oracles/dampers.

pme_add_doctest(pme_dampers_tests
  SOURCES test_controller.cpp test_diagonal.cpp test_rank_one.cpp test_engagement.cpp
  LIBRARIES pme_dampers pme_string pme_nonlinear pme_osc pme::engine
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_dampers_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# The example instrument's dampers at full engagement (A6-R1 overdamping check). Uses the B1 loader,
# which is a test-only dependency here.
pme_add_doctest(pme_dampers_instrument_tests
  SOURCES test_instrument.cpp
  LIBRARIES pme_dampers pme_string pme_nonlinear pme_osc pme_loader pme::engine
  LABELS unit)
target_compile_definitions(pme_dampers_instrument_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS
  "PME_A6_EXAMPLES=\"${PROJECT_SOURCE_DIR}/instruments/examples\"")

# A6 real-time safety: controller, refresh and rank-one step run under ScopedRealtimeThread.
pme_add_doctest(pme_dampers_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_dampers pme_string pme_nonlinear pme_osc pme::engine pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_dampers_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_dampers_bench SOURCES dampers_bench.cpp
  LIBRARIES pme_dampers pme_string pme_nonlinear pme_osc pme::engine)
