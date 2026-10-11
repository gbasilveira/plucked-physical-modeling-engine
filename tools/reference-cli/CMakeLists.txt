# SPDX-License-Identifier: MIT
# pme_reference_cli (P0.5-R7): renders scenario files with the reference solver. Links only
# pme_reference_core (and through it pme_analysis); never an engine module (checked in
# reference/CMakeLists.txt).
add_executable(pme_reference_cli main.cpp)
target_link_libraries(pme_reference_cli PRIVATE pme_build_options pme_reference_core)
set_target_properties(pme_reference_cli PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

# ---- Tests (P0.5-R7) ---------------------------------------------------------------------------
set(PME_REFERENCE_SCENARIOS "${PROJECT_SOURCE_DIR}/reference/scenarios")
set(_cli_out "${CMAKE_CURRENT_BINARY_DIR}/smoke")
add_test(NAME "P0.5-R7 CLI render writes features, wav and energy"
  COMMAND pme_reference_cli render "${PME_REFERENCE_SCENARIOS}/smoke.json"
          --features "${_cli_out}.features.json" --wav "${_cli_out}.wav"
          --energy "${_cli_out}.energy.json")
add_test(NAME "P0.5-R7 CLI features file passes the P0.3 comparator"
  COMMAND pme_golden_compare "${_cli_out}.features.json" "${_cli_out}.features.json")
add_test(NAME "P0.5-R7 CLI refine reports orders"
  COMMAND pme_reference_cli refine "${PME_REFERENCE_SCENARIOS}/smoke.json"
          --grids 32,64,128 --quantity partial-frequencies --partials 3)
add_test(NAME "P0.5-R7 CLI rejects a missing scenario"
  COMMAND pme_reference_cli render "${CMAKE_CURRENT_BINARY_DIR}/does-not-exist.json")
set_tests_properties("P0.5-R7 CLI render writes features, wav and energy"
  PROPERTIES FIXTURES_SETUP pme_reference_cli_smoke)
set_tests_properties("P0.5-R7 CLI features file passes the P0.3 comparator"
  PROPERTIES FIXTURES_REQUIRED pme_reference_cli_smoke)
set_tests_properties("P0.5-R7 CLI rejects a missing scenario" PROPERTIES WILL_FAIL TRUE)
set_tests_properties(
  "P0.5-R7 CLI render writes features, wav and energy"
  "P0.5-R7 CLI features file passes the P0.3 comparator"
  "P0.5-R7 CLI refine reports orders"
  "P0.5-R7 CLI rejects a missing scenario"
  PROPERTIES LABELS integration)
