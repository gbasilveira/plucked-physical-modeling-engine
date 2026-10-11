# SPDX-License-Identifier: MIT
# B1: non-real-time instrument file loader and canonical writer. Not an engine module: it is never
# linked into pme_engine or any pme_<module> target (the engine-isolation check in
# cmake/pme_testing.cmake does not list it), and pme_runtime's non-recursive glob does not see this
# directory.

add_library(pme_loader STATIC
  blobs.cpp
  checks.cpp
  instrument_file.cpp
  migrate.cpp
  reader.cpp
  sha256.cpp)
add_library(pme::loader ALIAS pme_loader)
target_include_directories(pme_loader
  PUBLIC "${PROJECT_SOURCE_DIR}/engine/include" "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pme_loader PRIVATE pme_build_options nlohmann_json::nlohmann_json)
set_target_properties(pme_loader PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)
