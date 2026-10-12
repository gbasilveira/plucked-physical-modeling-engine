# SPDX-License-Identifier: MIT
# A5 oracle tests ORC-08 (hammer-potential case), ORC-09, ORC-19 and A5-T2 (SPEC-ORC "Impact
# time", "Hammer-string energy", "Una corda aftersound"; SPEC-HAM; phase card A5 against the P0.5
# reference solver). They use the public headers pme/string.hpp and pme/exciters.hpp, A3's generic
# SavSegment/savStep, pme_analysis, and one rig header (hammer_rig.hpp) that is the only place
# touching a hammer implementation.
#
# Backends of the rig:
#   ScratchRig<S>  hammer_rig_scratch.hpp, the SPEC-HAM / SPEC-NL / SPEC-BRG scheme written from the
#                  specification (reusing only A3's generic savStep). Always compiled. Test cases
#                  named "(SPEC scratch scheme)".
#   A5Rig<S>       hammer_rig_a5.hpp, the adapter onto the A5 StruckNote (the code under test).
#                  Compiled when PME_A5_ORACLE_BACKEND is A5 (or AUTO and the A5 header named in
#                  PME_A5_STRUCK_NOTE_HEADER exists); test cases named "(A5 StruckNote)". The
#                  lines to adapt to the merged A5 headers are marked "ADAPT" in that file.
# Default backend: SCRATCH until the adapter is adapted and verified (post-merge action of the A5
# test-author's report); then switch the default to AUTO.
#
# Run (ORC-08/09/19 in any build; A5-T2 reference case only in an optimised build):
#   cmake --preset linux-debug && cmake --build --preset linux-debug
#   ctest --preset linux-debug -L oracle -R "ORC-0[89]|ORC-19" --output-on-failure
#   cmake --preset linux-release && cmake --build --preset linux-release
#   ctest --preset linux-release -R "A5-T2" --output-on-failure
# A5-T2 reads PME_A5T2_N0 (default 200: coarsest reference grid intervals) and PME_A5T2_FS0
# (default 96000: coarsest reference simulation rate); levels 0, 1, 2 double both.

set(PME_A5_ORACLE_BACKEND "SCRATCH" CACHE STRING "Backend of the A5 oracle rig: SCRATCH, A5 or AUTO")
set_property(CACHE PME_A5_ORACLE_BACKEND PROPERTY STRINGS SCRATCH A5 AUTO)
set(PME_A5_STRUCK_NOTE_HEADER "engine/src/exciters/include/pme/exciters/struck_note.hpp"
    CACHE STRING "Header AUTO looks for to decide whether the A5 adapter can be built")

set(_a5_use_a5 OFF)
if(PME_A5_ORACLE_BACKEND STREQUAL "A5")
  set(_a5_use_a5 ON)
elseif(PME_A5_ORACLE_BACKEND STREQUAL "AUTO" AND TARGET pme_exciters AND
       EXISTS "${PROJECT_SOURCE_DIR}/${PME_A5_STRUCK_NOTE_HEADER}")
  set(_a5_use_a5 ON)
endif()
message(STATUS "A5 oracle tests: rig backend ${PME_A5_ORACLE_BACKEND} -> A5 adapter ${_a5_use_a5} "
               "(OFF = SPEC scratch scheme only)")

set(_a5_libs pme_nonlinear pme_string pme_osc pme_analysis)
if(_a5_use_a5)
  list(APPEND _a5_libs pme_exciters pme_bridge)
endif()

function(pme_a5_oracle_target target)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/engine/tests/oracles/string")  # string_oracle_common.hpp, read only
  if(_a5_use_a5)
    target_compile_definitions(${target} PRIVATE PME_ORACLE_HAMMER_A5=1)
  endif()
endfunction()

pme_add_doctest(pme_oracle_exciters_tests
  SOURCES test_orc08_orc09.cpp
  LIBRARIES ${_a5_libs}
  LABELS oracle unit
  TIMEOUT 600)
pme_a5_oracle_target(pme_oracle_exciters_tests)
