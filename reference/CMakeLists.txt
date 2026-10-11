# SPDX-License-Identifier: MIT
# P0.5 reference solver: an independent float64 time-domain solver that validates the engine.
#
# Independence rule: targets under reference/ and tools/reference-cli/ use the description structs
# in engine/include (header-only use), pme_analysis, nlohmann/json and the standard library. They
# never link an engine module or pme_engine, and never include engine/src. The check at the bottom
# of this file enforces the link rule at configure time.

add_library(pme_reference STATIC
  src/banded.cpp
  src/common.cpp
  src/plate.cpp
  src/scenario.cpp
  src/simulation.cpp
  src/geometric.cpp
  src/string_ops.cpp)
add_library(pme::reference ALIAS pme_reference)
target_include_directories(pme_reference PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(pme_reference
  PUBLIC pme_analysis
  PRIVATE pme_build_options nlohmann_json::nlohmann_json)
set_target_properties(pme_reference PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
  POSITION_INDEPENDENT_CODE ON)

# Cases that need fine grids to meet the card tolerances are compiled only into optimised builds
# (Release, RelWithDebInfo), which still run in CI on all three systems (rt-safety presets and the
# release builds). docs/dev/reference-solver.md lists which case runs where.
set(PME_REFERENCE_OPTIMISED_DEFINE "$<$<CONFIG:Release,RelWithDebInfo>:PME_REF_OPTIMISED=1>")

pme_add_doctest(pme_reference_unit_tests
  SOURCES
    tests/test_string.cpp
    tests/test_hammer.cpp
    tests/test_plate.cpp
    tests/test_bridge.cpp
    tests/test_barrier.cpp
    tests/test_geometric.cpp
  LIBRARIES pme_reference nlohmann_json::nlohmann_json
  LABELS unit integration)
target_compile_definitions(pme_reference_unit_tests PRIVATE ${PME_REFERENCE_OPTIMISED_DEFINE})

# ---- Independence check (P0.5 brief, "Independence rules") ----------------------------------------
set(PME_REFERENCE_TARGETS pme_reference pme_reference_unit_tests pme_reference_cli
    pme_reference_python)
set(PME_REFERENCE_FORBIDDEN_MODULES pme_osc pme_string pme_bridge pme_body pme_nonlinear
    pme_exciters pme_dampers pme_barriers pme_runtime pme_sched pme_simd pme_engine)

# Scratch-build switch that proves the check fires: links an engine module into the reference.
option(PME_REFERENCE_ISOLATION_SELFTEST "Link pme_string into pme_reference (the check must fail)" OFF)
mark_as_advanced(PME_REFERENCE_ISOLATION_SELFTEST)
if(PME_REFERENCE_ISOLATION_SELFTEST)
  target_link_libraries(pme_reference PRIVATE pme_string)
endif()

function(_pme_ref_real_target name out)
  set(real "${name}")
  if(TARGET "${real}")
    get_target_property(aliased "${real}" ALIASED_TARGET)
    if(aliased)
      set(real "${aliased}")
    endif()
  endif()
  set(${out} "${real}" PARENT_SCOPE)
endfunction()

function(_pme_check_reference_isolation)
  foreach(root IN LISTS PME_REFERENCE_TARGETS)
    if(NOT TARGET ${root})
      continue()
    endif()
    set(todo ${root})
    set(seen "")
    while(todo)
      list(POP_FRONT todo current)
      if(current IN_LIST seen)
        continue()
      endif()
      list(APPEND seen ${current})
      get_target_property(type ${current} TYPE)
      set(deps "")
      foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${current} ${prop})
        if(value)
          list(APPEND deps ${value})
        endif()
      endforeach()
      foreach(dep IN LISTS deps)
        string(REGEX REPLACE "^\\$<(LINK_ONLY|BUILD_INTERFACE|BUILD_LOCAL_INTERFACE):(.*)>$" "\\2"
               dep "${dep}")
        if(dep MATCHES "[$<>]" OR NOT TARGET "${dep}")
          continue()
        endif()
        _pme_ref_real_target("${dep}" real)
        if(real IN_LIST PME_REFERENCE_FORBIDDEN_MODULES)
          message(FATAL_ERROR
            "Reference independence violated: ${root} reaches '${real}' (through ${current}). "
            "Reference targets may use engine/include description structs, pme_analysis, "
            "nlohmann_json and the standard library only (P0.5).")
        endif()
        list(APPEND todo ${real})
      endforeach()
    endwhile()
  endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _pme_check_reference_isolation)
