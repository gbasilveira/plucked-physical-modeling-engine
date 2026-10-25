# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(piano_mvp_v4 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(CORE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(piano_core STATIC
  ${CORE}/engine/src/osc/oscillator.cpp
  ${CORE}/engine/src/osc/mode_bank.cpp
  ${CORE}/engine/src/string/string_model.cpp
  ${CORE}/engine/src/bridge/model.cpp
  ${CORE}/engine/src/bridge/terms.cpp
  ${CORE}/engine/src/nonlinear/sav.cpp
  ${CORE}/engine/src/exciters/hammer.cpp
  piano.cpp bridge_model.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../piano-v3/soundboard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../piano-v2/preset.cpp
  ${CORE}/engine/src/simd/denormals.cpp)
target_include_directories(piano_core PUBLIC ${CORE}/engine/include ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(piano_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../piano-v2)
target_include_directories(piano_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../piano-v3)
foreach(module osc string bridge nonlinear exciters simd)
  target_include_directories(piano_core PUBLIC ${CORE}/engine/src/${module}/include)
endforeach()
add_executable(piano-render render.cpp)
target_link_libraries(piano-render PRIVATE piano_core)
enable_testing()
add_executable(piano-check check.cpp)
target_link_libraries(piano-check PRIVATE piano_core)
add_test(NAME piano-behaviour COMMAND piano-check)
set_tests_properties(piano-behaviour PROPERTIES TIMEOUT 120)
add_executable(bridge-check bridge_check.cpp)
target_link_libraries(bridge-check PRIVATE piano_core)
add_test(NAME bridge-energy COMMAND bridge-check)
set_tests_properties(bridge-energy PROPERTIES TIMEOUT 120)
set(CLAP_INCLUDE_DIR "" CACHE PATH "Directory containing clap/clap.h (optional plug-in)")
if(CLAP_INCLUDE_DIR)
  add_library(piano-mvp-v4 MODULE clap.cpp)
  target_include_directories(piano-mvp-v4 PRIVATE ${CLAP_INCLUDE_DIR})
  target_link_libraries(piano-mvp-v4 PRIVATE piano_core)
  set_target_properties(piano-mvp-v4 PROPERTIES PREFIX "" SUFFIX ".clap")
  if(UNIX AND NOT APPLE)
    add_executable(piano-host-check host_check.cpp)
    target_include_directories(piano-host-check PRIVATE ${CLAP_INCLUDE_DIR})
    target_link_libraries(piano-host-check PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME piano-clap-playback COMMAND piano-host-check $<TARGET_FILE:piano-mvp-v4>)
    set_tests_properties(piano-clap-playback PROPERTIES TIMEOUT 120)
  endif()
endif()
