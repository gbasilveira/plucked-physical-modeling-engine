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

add_library(pme_engine INTERFACE)
add_library(pme::engine ALIAS pme_engine)
foreach(module IN LISTS PME_MODULES)
  target_link_libraries(pme_engine INTERFACE pme_${module})
endforeach()

# ---- P0.2: RT-safety trap library ----
add_subdirectory(engine/src/rtcheck)
add_subdirectory(engine/tests/rtcheck)
# ---- end P0.2 ----
