# SPDX-License-Identifier: MIT
# A4 oracle tests ORC-06, ORC-07 (SPEC-ORC "Bridge damping", "Unison coupling") and A4-T3 (phase
# card A4, against the P0.5 reference solver). They use the public headers pme/string.hpp and
# pme/osc.hpp (compileString, compileOscillator), the A1 ModeBank, pme_analysis, and one rig header
# (bridge_rig.hpp) that is the only place touching a bridge implementation.
#
# Backend of the rig (cache variable PME_A4_ORACLE_BACKEND):
#   SCRATCH  bridge_rig_scratch.hpp, the SPEC-BRG scheme written from the specification alone, no
#            call into engine/src/bridge. Builds before A4 is merged.
#   A4       bridge_rig_a4.hpp, the adapter onto the A4 BridgePoint (the code under test). Links
#            pme_bridge.
#   AUTO     (default) A4 when engine/src/bridge/include/pme/bridge/bridge_point.hpp exists and the
#            target pme_bridge is defined, else SCRATCH. The root CMakeLists.txt block of A4 adds
#            the include directory of pme_bridge, so with A4 merged AUTO selects A4.
#
# Run (ORC-06 and ORC-07 in any build; A4-T3 reference case only in an optimised build):
#   cmake --preset linux-debug && cmake --build --preset linux-debug
#   ctest --preset linux-debug -L oracle -R "ORC-0[67]" --output-on-failure
#   cmake --preset linux-release && cmake --build --preset linux-release
#   ctest --preset linux-release -R "A4-T3" --output-on-failure
# A4-T3 reads PME_A4T3_N0 (default 100, coarsest reference grid intervals) and PME_A4T3_OS0
# (default 1, coarsest oversampling); levels 0, 1, 2 double both.

set(PME_A4_ORACLE_BACKEND "AUTO" CACHE STRING "Backend of the A4 oracle rig: AUTO, SCRATCH or A4")
set_property(CACHE PME_A4_ORACLE_BACKEND PROPERTY STRINGS AUTO SCRATCH A4)

set(_a4_use_a4 OFF)
if(PME_A4_ORACLE_BACKEND STREQUAL "A4")
  set(_a4_use_a4 ON)
elseif(PME_A4_ORACLE_BACKEND STREQUAL "AUTO" AND TARGET pme_bridge AND
       EXISTS "${PROJECT_SOURCE_DIR}/engine/src/bridge/include/pme/bridge/bridge_point.hpp")
  set(_a4_use_a4 ON)
endif()
message(STATUS "A4 oracle tests: rig backend ${PME_A4_ORACLE_BACKEND} -> "
               "${_a4_use_a4} (ON = A4 BridgePoint, OFF = scratch SPEC scheme)")

set(_a4_libs pme_string pme_osc pme_analysis)
if(_a4_use_a4)
  list(APPEND _a4_libs pme_bridge)
endif()

function(pme_a4_oracle_target target)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/engine/tests/oracles/string")  # string_oracle_common.hpp, read only
  if(_a4_use_a4)
    target_compile_definitions(${target} PRIVATE PME_ORACLE_BRIDGE_A4=1)
  endif()
endfunction()

pme_add_doctest(pme_oracle_bridge_tests
  SOURCES test_orc06_orc07.cpp
  LIBRARIES ${_a4_libs}
  LABELS oracle unit
  TIMEOUT 600)
pme_a4_oracle_target(pme_oracle_bridge_tests)

pme_add_doctest(pme_oracle_bridge_t3_tests
  SOURCES test_a4t3_reference.cpp
  LIBRARIES ${_a4_libs}
  LABELS integration
  TIMEOUT 900)
pme_a4_oracle_target(pme_oracle_bridge_t3_tests)

# P0.5's targets are defined after this directory is processed (the root CMakeLists.txt appends the
# P0.5 block at its end), so decide at the end of the root directory's configuration.
function(pme_a4t3_register_reference)
  if(TARGET pme_reference_cli)
    target_compile_definitions(pme_oracle_bridge_t3_tests PRIVATE
      "$<$<CONFIG:Release,RelWithDebInfo>:PME_A4T3_REFERENCE_CLI=\"$<TARGET_FILE:pme_reference_cli>\">")
    add_dependencies(pme_oracle_bridge_t3_tests pme_reference_cli)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL pme_a4t3_register_reference)
