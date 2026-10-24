# SPDX-License-Identifier: MIT
# P0.4 interface tests: header self-containment (T1), stub Engine (T2, R3), parameter registry (T3, R4),
# events (R5). Built with P0.3's pme_add_doctest.

# T1: one translation unit per public header, each including it twice and nothing else.
file(GLOB PME_T1_HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/engine/include/pme/*.hpp")
set(PME_T1_SOURCES "")
foreach(header IN LISTS PME_T1_HEADERS)
  get_filename_component(header_name "${header}" NAME_WE)
  set(tu "${CMAKE_CURRENT_BINARY_DIR}/header_tus/header_${header_name}.cpp")
  file(CONFIGURE OUTPUT "${tu}"
    CONTENT "// SPDX-License-Identifier: MIT\n// Generated: proves pme/${header_name}.hpp compiles on its own.\n#include \"pme/${header_name}.hpp\"\n#include \"pme/${header_name}.hpp\"\n"
    @ONLY)
  list(APPEND PME_T1_SOURCES "${tu}")
endforeach()
list(LENGTH PME_T1_HEADERS PME_T1_HEADER_COUNT)
list(LENGTH PME_T1_SOURCES PME_T1_SOURCE_COUNT)

# T2 (rt-safety build) runs the stub Engine under ScopedRealtimeThread, which needs P0.2's pme_rtcheck;
# the whole executable then also carries the rt label.
set(PME_INTERFACE_LABELS unit)
set(PME_INTERFACE_LIBS pme::engine)
if(PME_RTSAFETY AND TARGET pme_rtcheck)
  list(APPEND PME_INTERFACE_LABELS rt)
  list(APPEND PME_INTERFACE_LIBS pme_rtcheck)
elseif(PME_RTSAFETY)
  message(STATUS "P0.4-T2: pme_rtcheck target not found; the rt-safety stub Engine test is skipped until P0.2 is merged")
endif()

# main.cpp is kept: it is compiled with the definitions below, which the framework's main is not.
pme_add_doctest(pme_interface_tests
  SOURCES main.cpp test_headers.cpp test_params.cpp test_events.cpp test_stubs.cpp test_engine.cpp
          ${PME_T1_SOURCES}
  LIBRARIES ${PME_INTERFACE_LIBS}
  LABELS ${PME_INTERFACE_LABELS})
# pme::toString(Status) is found by ADL and would hijack doctest's unqualified toString(); doctest's
# DOUBLE_STRINGIFY option makes doctest convert its result (a const char*) again.
# USE_STD_HEADERS: MSVC needs a complete std::ostream when doctest probes operator<< for string_view.
target_compile_definitions(pme_interface_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS
  PME_T1_GENERATED_TU_COUNT=${PME_T1_SOURCE_COUNT}
  "PME_INCLUDE_PME_DIR=\"${PROJECT_SOURCE_DIR}/engine/include/pme\"")

# R3(a): count allocations with a test-local operator new, but only when rtcheck does not replace it and
# the TSan runtime (which defines operator new itself; static link with Clang) is not in use. The count
# is still asserted by every other configuration.
if(NOT PME_RTSAFETY AND NOT PME_SANITIZER STREQUAL "tsan")
  target_sources(pme_interface_tests PRIVATE alloc_counter.cpp)
  target_compile_definitions(pme_interface_tests PRIVATE PME_TEST_COUNT_ALLOCATIONS=1)
endif()

# T2: the rt-safety build runs the stub Engine under ScopedRealtimeThread (needs P0.2's pme_rtcheck).
if(PME_RTSAFETY AND TARGET pme_rtcheck)
  target_compile_definitions(pme_interface_tests PRIVATE PME_TEST_HAVE_RTCHECK=1)
endif()

# R4: the committed params.json is the generator's output.
add_test(NAME P0.4-R4_params_json_matches_registry
  COMMAND "${CMAKE_COMMAND}"
    "-DGENERATOR=$<TARGET_FILE:pme_params_json>"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/params.generated.json"
    "-DCOMMITTED=${PROJECT_SOURCE_DIR}/params.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_params_json.cmake")
set_tests_properties(P0.4-R4_params_json_matches_registry PROPERTIES LABELS "unit")
