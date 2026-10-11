# SPDX-License-Identifier: MIT
# pme_plugin_common: the controller layer (D1 "Architecture"). No CLAP types in its public headers.

add_library(pme_plugin_common STATIC
  src/controller.cpp
  src/event_translator.cpp
  src/param_model.cpp
  src/state.cpp
  src/task_runner.cpp)
add_library(pme::plugin_common ALIAS pme_plugin_common)
target_include_directories(pme_plugin_common PUBLIC include)
# nlohmann/json is used by state.cpp only (ADR-0005: non-real-time code; never in engine modules).
target_link_libraries(pme_plugin_common
  PUBLIC pme::engine pme_rtcheck
  PRIVATE pme_build_options nlohmann_json::nlohmann_json)
target_compile_definitions(pme_plugin_common PRIVATE
  PME_PLUGIN_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(pme_plugin_common PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
  POSITION_INDEPENDENT_CODE ON)

add_subdirectory(tests)
