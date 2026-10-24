# SPDX-License-Identifier: MIT
# A7 oracle tests ORC-11, ORC-12 (SPEC-ORC "Longitudinal forcing", "Tension modulation") and A7-T3
# (phase card A7, against the P0.5 reference solver). They use the public header pme/string.hpp
# (compileString), the A1 ModeBank, pme_analysis, and one rig header (geometric_rig.hpp) that is the
# only place touching a geometric-nonlinearity implementation.
#
# Backend of the rig (cache variable PME_A7_ORACLE_BACKEND):
#   SCRATCH  geometric_rig_scratch.hpp, the SPEC-GEO scheme written from the specification alone,
#            no call into engine/src/{string,nonlinear}. Builds before A7 is merged. With this
#            backend ORC-11 and ORC-12 check the test chain (quadrature, pair-sum formula,
#            estimator, initial conditions) and A7-T3 checks the reference, not the A7 code.
#   A7       geometric_rig_a7.hpp, the adapter onto the merged A7 code (longitudinal.hpp,
#            tension.hpp, SavSegment with S = double; verified against A7). Links pme_nonlinear.
#   AUTO     (default) A7 when both A7 headers exist (PME_A7_ADAPTER_VERIFIED, ON), else SCRATCH.
#
# Run (ORC-11 and ORC-12 in any build; A7-T3 reference case only in an optimised build):
#   cmake --preset linux-debug && cmake --build --preset linux-debug
#   ctest --preset linux-debug -L oracle -R "ORC-1[12]" --output-on-failure
#   cmake --preset linux-release && cmake --build --preset linux-release
#   ctest --preset linux-release -R "A7-T3" --output-on-failure -V
# A7-T3 reads PME_A7T3_N0 (default 190, coarsest reference grid intervals) and PME_A7T3_FS0
# (default 96000, coarsest simulation rate in Hz); levels 0, 1, 2 double both.

set(PME_A7_ORACLE_BACKEND "AUTO" CACHE STRING "Backend of the A7 oracle rig: AUTO, SCRATCH or A7")
set_property(CACHE PME_A7_ORACLE_BACKEND PROPERTY STRINGS AUTO SCRATCH A7)
option(PME_A7_ADAPTER_VERIFIED "geometric_rig_a7.hpp compiles against the merged A7 headers" ON)

set(_a7_use_a7 OFF)
if(PME_A7_ORACLE_BACKEND STREQUAL "A7")
  set(_a7_use_a7 ON)
elseif(PME_A7_ORACLE_BACKEND STREQUAL "AUTO" AND PME_A7_ADAPTER_VERIFIED AND
       EXISTS "${PROJECT_SOURCE_DIR}/engine/src/string/include/pme/string/longitudinal.hpp" AND
       EXISTS "${PROJECT_SOURCE_DIR}/engine/src/nonlinear/include/pme/nonlinear/tension.hpp")
  set(_a7_use_a7 ON)
endif()
message(STATUS "A7 oracle tests: rig backend ${PME_A7_ORACLE_BACKEND} -> "
               "${_a7_use_a7} (ON = A7 code, OFF = scratch SPEC scheme)")

set(_a7_libs pme_string pme_osc pme_analysis)
if(_a7_use_a7)
  list(APPEND _a7_libs pme_nonlinear)
endif()

function(pme_a7_oracle_target target)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/engine/tests/oracles/string")  # string_oracle_common.hpp, read only
  if(_a7_use_a7)
    target_compile_definitions(${target} PRIVATE PME_ORACLE_GEO_A7=1)
  endif()
endfunction()

pme_add_doctest(pme_oracle_geometric_tests
  SOURCES test_orc11_orc12.cpp
  LIBRARIES ${_a7_libs}
  LABELS oracle unit
  TIMEOUT 300)
pme_a7_oracle_target(pme_oracle_geometric_tests)

pme_add_doctest(pme_oracle_geometric_t3_tests
  SOURCES test_a7t3_reference.cpp
  LIBRARIES ${_a7_libs}
  LABELS integration
  TIMEOUT 600)
pme_a7_oracle_target(pme_oracle_geometric_t3_tests)

# P0.5's targets are defined after this directory is processed (the root CMakeLists.txt appends the
# P0.5 block at its end), so decide at the end of the root directory's configuration.
function(pme_a7t3_register_reference)
  if(TARGET pme_reference_cli)
    target_compile_definitions(pme_oracle_geometric_t3_tests PRIVATE
      "$<$<CONFIG:Release,RelWithDebInfo>:PME_A7T3_REFERENCE_CLI=\"$<TARGET_FILE:pme_reference_cli>\">")
    add_dependencies(pme_oracle_geometric_t3_tests pme_reference_cli)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL pme_a7t3_register_reference)
