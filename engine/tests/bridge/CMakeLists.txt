# SPDX-License-Identifier: MIT
# A4 tests: bridge terms (A4-R1), models and coupling order (A4-R2), unison (A4-R3), polarisation
# (A4-R4), hooks (A4-R5), energy (A4-T1), partial shift (A4-T2), real-time safety and the
# nightly benchmark.

pme_add_doctest(pme_bridge_tests
  SOURCES test_terms.cpp test_models.cpp test_coupling.cpp test_energy.cpp
  LIBRARIES pme_bridge pme_string pme_osc pme::engine
  LABELS unit)
# pme::toString(Status) is found by ADL and hijacks doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_bridge_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# A4-T2 runs 20 s of audio per case and uses the analysis library (tests only, never the engine).
pme_add_doctest(pme_bridge_shift_tests
  SOURCES test_shift.cpp
  LIBRARIES pme_bridge pme_string pme_osc pme::engine pme::analysis
  LABELS integration
  TIMEOUT 900)
target_compile_definitions(pme_bridge_shift_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

# A4-R2: the per-sample path runs under ScopedRealtimeThread; with the rt-safety preset any
# allocation aborts.
pme_add_doctest(pme_bridge_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_bridge pme_string pme_osc pme::engine pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_bridge_rt_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_benchmark(pme_bridge_bench SOURCES bridge_bench.cpp LIBRARIES pme_bridge pme_string pme_osc)
