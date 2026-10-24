# SPDX-License-Identifier: MIT
# A8 tests: parity with the scalar references on every supported instruction set (A8-T1, A8-R1 to
# A8-R4), bit-identical repeated runs (A8-T2), AVX2 throughput (A8-T3), dispatch and denormals
# (A8-R5), plus the nightly benchmarks simd_bench and bridge_scheme_bench.

set(PME_SIMD_TEST_DEFINITIONS DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)

pme_add_doctest(pme_simd_tests
  SOURCES test_parity.cpp test_reviewer.cpp test_matmul.cpp test_determinism.cpp test_dispatch.cpp test_speed.cpp
  LIBRARIES pme_simd pme_osc pme_nonlinear pme::engine
  LABELS unit)
target_compile_definitions(pme_simd_tests PRIVATE ${PME_SIMD_TEST_DEFINITIONS})
# The 4x throughput bar of A8-T3 is a statement about optimised code; an unoptimised (Debug) build
# measures the ratio and logs it without enforcing it.
target_compile_definitions(pme_simd_tests PRIVATE
  "PME_SIMD_UNOPTIMISED_BUILD=$<IF:$<CONFIG:Debug>,1,0>")

# A8-R5: the kernels and the dispatch run under ScopedRealtimeThread; with the rt-safety preset any
# allocation or blocking call aborts.
pme_add_doctest(pme_simd_rt_tests
  SOURCES test_rt.cpp
  LIBRARIES pme_simd pme::engine pme_rtcheck
  LABELS unit
  RT)
target_compile_definitions(pme_simd_rt_tests PRIVATE ${PME_SIMD_TEST_DEFINITIONS})

pme_add_benchmark(pme_simd_bench SOURCES simd_bench.cpp LIBRARIES pme_simd pme_osc)
pme_add_benchmark(pme_bridge_scheme_bench SOURCES bridge_scheme_bench.cpp LIBRARIES pme_simd pme_osc)
