# SPDX-License-Identifier: MIT
# A3 tests: Woodbury solve (A3-T1), energy and dashpots (A3-T2, A3-T3), contact rules (A3-T4,
# A3-R1, A3-R4), real-time safety (A3-R2) and the nightly benchmark.

pme_add_doctest(pme_nonlinear_tests
  SOURCES test_solve.cpp test_energy.cpp test_contact.cpp
  LIBRARIES pme_nonlinear pme_osc pme::engine
  LABELS unit)
target_compile_definitions(pme_nonlinear_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# A3-R2: savStep runs under ScopedRealtimeThread; with the rt-safety preset any allocation aborts.
pme_add_doctest(pme_nonlinear_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_nonlinear pme_osc pme::engine pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_nonlinear_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_nonlinear_bench SOURCES sav_bench.cpp LIBRARIES pme_nonlinear pme_osc)
