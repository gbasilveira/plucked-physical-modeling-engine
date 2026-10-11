# SPDX-License-Identifier: MIT
# Analysis library (P0.3-R3, R4, R5): partial tracking, decay and inharmonicity fits, contact time,
# spectral envelope, energy accounting, golden-feature comparison and WAV I/O. Test and tool code
# only: never linked into an engine module (checked at configure time in cmake/pme_testing.cmake).

add_library(pme_analysis STATIC
  src/fft.cpp
  src/linalg.cpp
  src/partials.cpp
  src/measures.cpp
  src/wav.cpp
  src/golden.cpp)
add_library(pme::analysis ALIAS pme_analysis)
target_include_directories(pme_analysis PUBLIC include)
target_link_libraries(pme_analysis PRIVATE pme_build_options nlohmann_json::nlohmann_json)
set_target_properties(pme_analysis PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

add_executable(pme_golden_compare golden_compare_main.cpp)
target_link_libraries(pme_golden_compare PRIVATE pme_build_options pme_analysis)
set_target_properties(pme_golden_compare PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

# ---- Tests -------------------------------------------------------------------------------------
set(PME_ANALYSIS_TESTDATA "${CMAKE_CURRENT_SOURCE_DIR}/testdata")

pme_add_doctest(pme_analysis_tests
  SOURCES
    tests/fft_linalg_tests.cpp
    tests/wav_tests.cpp
    tests/measures_tests.cpp
    tests/golden_tests.cpp
  LIBRARIES pme_analysis nlohmann_json::nlohmann_json
  LABELS unit)

pme_add_doctest(pme_analysis_partials_tests
  SOURCES tests/partials_tests.cpp tests/testdata.cpp
  LIBRARIES pme_analysis nlohmann_json::nlohmann_json
  LABELS unit)
target_compile_definitions(pme_analysis_partials_tests PRIVATE
  PME_ANALYSIS_TESTDATA="${PME_ANALYSIS_TESTDATA}")

add_test(NAME "P0.3-T2 CLI passes an identical file"
  COMMAND pme_golden_compare "${PME_ANALYSIS_TESTDATA}/golden_reference.json"
                             "${PME_ANALYSIS_TESTDATA}/golden_reference.json")
add_test(NAME "P0.3-T2 CLI passes a 0.4 percent decay change"
  COMMAND pme_golden_compare "${PME_ANALYSIS_TESTDATA}/golden_reference.json"
                             "${PME_ANALYSIS_TESTDATA}/golden_decay_plus_0p4pct.json")
add_test(NAME "P0.3-T2 CLI fails a 1 percent decay change"
  COMMAND pme_golden_compare "${PME_ANALYSIS_TESTDATA}/golden_reference.json"
                             "${PME_ANALYSIS_TESTDATA}/golden_decay_plus_1pct.json")
set_tests_properties("P0.3-T2 CLI passes an identical file"
                     "P0.3-T2 CLI passes a 0.4 percent decay change" PROPERTIES LABELS golden)
set_tests_properties("P0.3-T2 CLI fails a 1 percent decay change" PROPERTIES
  LABELS golden
  PASS_REGULAR_EXPRESSION "FAIL +partial\\[2\\]\\.decay_rate")
