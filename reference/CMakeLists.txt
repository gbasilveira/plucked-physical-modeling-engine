# SPDX-License-Identifier: MIT
# P0.5 reference solver: an independent float64 time-domain solver that validates the engine.
#
# Independence rule: targets under reference/ and tools/reference-cli/ use the description structs
# in engine/include (header-only use), pme_analysis, nlohmann/json and the standard library. They
# never link an engine module or pme_engine, and never include engine/src. The check at the bottom
# of this file enforces the link rule at configure time.

add_library(pme_reference_core STATIC
  src/banded.cpp
  src/common.cpp
  src/plate.cpp
  src/scenario.cpp
  src/simulation.cpp
  src/geometric.cpp
  src/string_ops.cpp
  src/features.cpp
  src/refine.cpp)
add_library(pme::reference ALIAS pme_reference_core)
target_include_directories(pme_reference_core PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(pme_reference_core
  PUBLIC pme_analysis
  PRIVATE pme_build_options nlohmann_json::nlohmann_json)
set_target_properties(pme_reference_core PROPERTIES
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
    tests/test_scenario.cpp
    tests/test_string.cpp
    tests/test_hammer.cpp
    tests/test_plate.cpp
    tests/test_bridge.cpp
    tests/test_barrier.cpp
    tests/test_geometric.cpp
  LIBRARIES pme_reference_core nlohmann_json::nlohmann_json
  LABELS unit integration)
target_compile_definitions(pme_reference_unit_tests PRIVATE
  ${PME_REFERENCE_OPTIMISED_DEFINE}
  PME_REFERENCE_SCENARIOS="${CMAKE_CURRENT_SOURCE_DIR}/scenarios")

# ---- Python bindings (P0.5-R7) -------------------------------------------------------------------
# Built only when PME_BUILD_PYTHON_BINDINGS is ON (the option and the nanobind fetch live in
# cmake/dependencies.cmake, owned by D1; until that lands configure with
# -DPME_FETCH_ALL_DEPS=ON -DPME_BUILD_PYTHON_BINDINGS=ON). The default build builds and tests
# everything except this module. The module `pme_reference` is written to
# ${CMAKE_BINARY_DIR}/reference/python; point PME_REFERENCE_PYDIR at that directory for the tests.
if(PME_BUILD_PYTHON_BINDINGS)
  if(NOT nanobind_SOURCE_DIR)
    message(FATAL_ERROR
      "PME_BUILD_PYTHON_BINDINGS is ON but nanobind's source directory is unknown: "
      "cmake/dependencies.cmake must fetch nanobind (CPMAddPackage NAME nanobind) when "
      "PME_BUILD_PYTHON_BINDINGS is ON.")
  endif()
  find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)
  find_package(nanobind CONFIG REQUIRED PATHS "${nanobind_SOURCE_DIR}/cmake" NO_DEFAULT_PATH)
  # The static libraries end up inside a shared module.
  set_target_properties(pme_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)
  nanobind_add_module(pme_reference NB_STATIC python/module.cpp)
  target_link_libraries(pme_reference PRIVATE pme_reference_core)
  set_target_properties(pme_reference PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python")
  foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(pme_reference PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}/python"
      RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}/python")
  endforeach()
endif()

# ---- Independence check (P0.5 brief, "Independence rules") ----------------------------------------
# pme_reference is the Python extension module (built only with PME_BUILD_PYTHON_BINDINGS).
set(PME_REFERENCE_TARGETS pme_reference_core pme_reference_unit_tests pme_reference_cli
    pme_reference)
set(PME_REFERENCE_FORBIDDEN_MODULES pme_osc pme_string pme_bridge pme_body pme_nonlinear
    pme_exciters pme_dampers pme_barriers pme_runtime pme_sched pme_simd pme_engine)

# Scratch-build switch that proves the check fires: links an engine module into the reference.
option(PME_REFERENCE_ISOLATION_SELFTEST "Link pme_string into pme_reference_core (the check must fail)" OFF)
mark_as_advanced(PME_REFERENCE_ISOLATION_SELFTEST)
if(PME_REFERENCE_ISOLATION_SELFTEST)
  target_link_libraries(pme_reference_core PRIVATE pme_string)
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

# Called at the end of the top-level directory (cmake_language DEFER), when every target exists. The
# lists are passed as arguments, because variables of this directory are not visible there.
function(_pme_check_reference_isolation targets forbidden)
  foreach(root IN LISTS targets)
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
        if(real IN_LIST forbidden)
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

# EVAL expands the lists now; a plain DEFER would expand the variables when the call runs.
cmake_language(EVAL CODE "
  cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _pme_check_reference_isolation
    [[${PME_REFERENCE_TARGETS}]] [[${PME_REFERENCE_FORBIDDEN_MODULES}]])")
