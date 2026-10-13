# SPDX-License-Identifier: MIT
# A7 tests: tension modulation (A7-R1), longitudinal modes, pair-sum forcing and bridge force
# (A7-R2 to A7-R4), flags and tier (A7-R5, A7-R6), energy (SI-0015), the Kirchhoff-Carrier glide
# (A7-T1), phantom partials (A7-T2), real-time safety and the nightly benchmark.

set(PME_GEOMETRIC_LIBRARIES pme_string pme_nonlinear pme_bridge pme_osc pme::engine)

pme_add_doctest(pme_geometric_tests
  SOURCES test_potential.cpp test_longitudinal.cpp test_segment.cpp test_energy.cpp
  LIBRARIES ${PME_GEOMETRIC_LIBRARIES}
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_geometric_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# A7-T1 and A7-T2 run seconds of audio and use the analysis library (tests only, never the engine).
pme_add_doctest(pme_geometric_integration_tests
  SOURCES test_glide.cpp test_phantom.cpp
  LIBRARIES ${PME_GEOMETRIC_LIBRARIES} pme::analysis
  LABELS integration
  TIMEOUT 900)
target_compile_definitions(pme_geometric_integration_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# The per-sample path runs under ScopedRealtimeThread; with the rt-safety preset any allocation
# aborts.
pme_add_doctest(pme_geometric_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES ${PME_GEOMETRIC_LIBRARIES} pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_geometric_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_geometric_bench SOURCES geometric_bench.cpp LIBRARIES ${PME_GEOMETRIC_LIBRARIES})
