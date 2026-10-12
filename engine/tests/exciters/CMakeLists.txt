# SPDX-License-Identifier: MIT
# A5 tests: velocity map (A5-R1), potential (A5-R2, A5-T1), felt dissipation (A5-R3), release and
# repetition (A5-R4, A5-T4), una corda (A5-R5, A5-T3), shank (A5-R6), diagnostics (A5-R7),
# real-time safety and the nightly benchmark. ORC-09, ORC-19 and A5-T2 are the test-author's, under
# engine/tests/oracles/exciters.

pme_add_doctest(pme_exciters_tests
  SOURCES test_velocity.cpp test_potential.cpp test_contact_time.cpp test_felt.cpp
          test_release.cpp test_una_corda.cpp test_shank.cpp test_diagnostics.cpp
  LIBRARIES pme_exciters pme_bridge pme_nonlinear pme_string pme_osc pme::engine
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_exciters_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# A5-R2: a trichord strike with a mass-spring-damper bridge runs under ScopedRealtimeThread; with
# the rt-safety preset any allocation aborts.
pme_add_doctest(pme_exciters_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_exciters pme_bridge pme_nonlinear pme_string pme_osc pme::engine pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_exciters_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_exciters_bench SOURCES hammer_bench.cpp
  LIBRARIES pme_exciters pme_bridge pme_nonlinear pme_string pme_osc pme::engine)
