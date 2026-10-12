# SPDX-License-Identifier: MIT
# D5: pme_plugin_library, the instrument and preset library (non-real-time). Not wired into the
# plug-ins yet; D6 connects it. Nothing in this target is reachable from Engine::process.

add_library(pme_plugin_library STATIC
  src/cache.cpp
  src/cache_keys.cpp
  src/files.cpp
  src/instruments.cpp
  src/locations.cpp
  src/preset.cpp)
add_library(pme::plugin_library ALIAS pme_plugin_library)
target_include_directories(pme_plugin_library PUBLIC include)
# nlohmann/json: preset files only (ADR-0005: non-real-time code; never in engine modules).
target_link_libraries(pme_plugin_library
  PUBLIC pme::engine pme_loader pme_plugin_common
  PRIVATE pme_build_options nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(pme_plugin_library PRIVATE shell32 ole32 uuid)
endif()
set_target_properties(pme_plugin_library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
  POSITION_INDEPENDENT_CODE ON)
# pme_loader (B1) is created after plugin/ is processed, so the name above resolves at generate
# time and the loader's PIC property is set from a deferred call. D6 links this library into
# plug-in modules, so the loader needs position-independent code as well (plugin/CMakeLists.txt
# does the same for the engine modules).
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL set_target_properties pme_loader PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(tests)
