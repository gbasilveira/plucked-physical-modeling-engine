# SPDX-License-Identifier: MIT
# Benchmark harness (P0.3-R6). Test and tool code only: never linked into an engine module (checked
# at configure time in cmake/pme_testing.cmake).

get_filename_component(PME_PRESET_NAME "${CMAKE_BINARY_DIR}" NAME)

add_library(pme_bench STATIC src/bench.cpp)
add_library(pme::bench ALIAS pme_bench)
target_include_directories(pme_bench PUBLIC include)
target_link_libraries(pme_bench PRIVATE pme_build_options nlohmann_json::nlohmann_json)
target_compile_definitions(pme_bench PRIVATE
  PME_BENCH_BUILD_TYPE="$<CONFIG>"
  PME_BENCH_PRESET="${PME_PRESET_NAME}")
set_target_properties(pme_bench PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

# Smoke benchmark: registered with the perf label by pme_add_benchmark.
pme_add_benchmark(pme_bench_smoke SOURCES smoke_bench.cpp ARGS --repetitions 10)

# Harness unit tests (P0.3-T3 part 2): the harness runs and writes valid JSON.
pme_add_doctest(pme_bench_tests
  SOURCES tests/bench_tests.cpp
  LIBRARIES pme_bench nlohmann_json::nlohmann_json
  LABELS unit)
