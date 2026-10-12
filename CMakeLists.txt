# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)

# MSVC debug information is selected by CMAKE_MSVC_DEBUG_INFORMATION_FORMAT (cmake/sanitizers.cmake).
cmake_policy(SET CMP0141 NEW)

project(pme VERSION 0.0.0 DESCRIPTION "Plucked physical modelling engine" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Options -----------------------------------------------------------------------------------------
option(PME_RTSAFETY "Build with the RT-safety trap (defines PME_RTSAFETY=1; the trap library is P0.2-R5)" OFF)
option(PME_FETCH_ALL_DEPS "Also fetch the plug-in, bindings and SDK dependencies (proves every pin resolves)" OFF)
set(PME_SANITIZER "" CACHE STRING "One of: empty, asan, tsan, coverage (set by the presets)")
set_property(CACHE PME_SANITIZER PROPERTY STRINGS "" asan tsan coverage)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizer and coverage flags apply to every target, third-party included, so that the
# instrumented runtime matches. Warning options do not (cmake/compiler_options.cmake).
include(sanitizers)
include(compiler_options)
include(dependencies)

enable_testing()

# Engine modules ----------------------------------------------------------------------------------
include(pme_module)

set(PME_MODULES osc string bridge body nonlinear exciters dampers barriers runtime sched simd)
foreach(module IN LISTS PME_MODULES)
  pme_add_module(${module})
endforeach()

# pme_simd is the only target that links Highway (ADR-0001).
target_link_libraries(pme_simd PRIVATE hwy)

# pme_engine: INTERFACE aggregate of all modules (alias pme::engine). It lists the public headers as
# a file set, so that CMake generates a build target for it, and depends on every module library, so
# that `cmake --build --preset <p> --target pme_engine` builds the whole engine (P0.4, docs/dev/interfaces.md).
file(GLOB PME_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/pme/*.hpp")
add_library(pme_engine INTERFACE)
add_library(pme::engine ALIAS pme_engine)
target_sources(pme_engine INTERFACE
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
  FILES ${PME_PUBLIC_HEADERS})
# CMake generates a build target for an INTERFACE library only when it has its own (non-interface)
# SOURCES; a header file set alone does not count. The headers are listed again as private sources
# (they are not compiled, and consumers do not inherit them) so that `--target pme_engine` exists.
target_sources(pme_engine PRIVATE ${PME_PUBLIC_HEADERS})
foreach(module IN LISTS PME_MODULES)
  target_link_libraries(pme_engine INTERFACE pme_${module})
  add_dependencies(pme_engine pme_${module})
endforeach()

# ---- P0.2: RT-safety trap library ----
add_subdirectory(engine/src/rtcheck)
# engine/tests/rtcheck is added below, after P0.3's test framework (pme_add_doctest)
# ---- end P0.2 ----

# ---- P0.3: test and analysis framework ----
include(pme_testing)
add_subdirectory(engine/tests/framework)
add_subdirectory(engine/tests/rtcheck)  # P0.2-T1, P0.2-R5
add_subdirectory(tools/analysis)
add_subdirectory(tools/bench)
# ---- end P0.3 ----

# ---- P0.4: interface tests and parameter generator ----
add_subdirectory(tools/params)
add_subdirectory(engine/tests/interfaces)
# ---- end P0.4 ----

# ---- A1 (test-author): oracle tests ----
add_subdirectory(engine/tests/oracles)
# ---- end A1 (test-author) ----

# ---- A1: modal oscillator ----
target_include_directories(pme_osc PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/osc/include")
add_subdirectory(engine/tests/osc)
# ---- end A1 ----

# ---- D1: CLAP plug-in shell ----
if(PME_BUILD_PLUGINS)
  add_subdirectory(plugin)
endif()
# ---- end D1 ----

# ---- A2: stiff string compiler ----
target_include_directories(pme_string PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/string/include")
target_link_libraries(pme_string PUBLIC pme_osc)
add_subdirectory(engine/tests/string)
# ---- end A2 ----

# ---- A3: SAV framework ----
target_include_directories(pme_nonlinear PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/nonlinear/include")
target_link_libraries(pme_nonlinear PUBLIC pme_osc)
add_subdirectory(engine/tests/nonlinear)
# ---- end A3 ----

# ---- P0.5: reference solver ----
add_subdirectory(reference)
add_subdirectory(tools/reference-cli)
# ---- end P0.5 ----

# ---- B1: instrument file loader ----
add_subdirectory(engine/src/runtime/loader)
add_subdirectory(engine/tests/loader)
# ---- end B1 ----

# ---- A4: bridge coupling ----
target_include_directories(pme_bridge PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/bridge/include")
target_link_libraries(pme_bridge PUBLIC pme_string pme_osc)
add_subdirectory(engine/tests/bridge)
# ---- end A4 ----

# ---- A6: dampers and pedals ----
target_include_directories(pme_dampers PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/dampers/include")
target_link_libraries(pme_dampers PUBLIC pme_string pme_nonlinear pme_osc)
add_subdirectory(engine/tests/dampers)
# ---- end A6 ----

# ---- A8: SIMD kernels ----
target_include_directories(pme_simd PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/simd/include")
# The scalar references must be truly scalar for A8-T3 (docs/dev/simd.md, "Scalar references").
if(MSVC)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(PME_SIMD_REFERENCE_OPTIONS /Qvec- /clang:-fno-slp-vectorize)
  else()
    set(PME_SIMD_REFERENCE_OPTIONS /Qvec-)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(PME_SIMD_REFERENCE_OPTIONS -fno-vectorize -fno-slp-vectorize)
else()
  set(PME_SIMD_REFERENCE_OPTIONS -fno-tree-vectorize -fno-tree-slp-vectorize)
endif()
set_source_files_properties("${PROJECT_SOURCE_DIR}/engine/src/simd/reference.cpp"
  PROPERTIES COMPILE_OPTIONS "${PME_SIMD_REFERENCE_OPTIONS}")
add_subdirectory(engine/tests/simd)
# ---- end A8 ----
