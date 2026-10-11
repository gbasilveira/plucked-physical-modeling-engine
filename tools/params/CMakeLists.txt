# SPDX-License-Identifier: MIT
# pme_params_json: writes the parameter registry as JSON (P0.4-R4). The committed params.json at the
# repository root is its output; the test P0.4-R4_params_json_matches_registry compares them.

add_executable(pme_params_json main.cpp)
target_link_libraries(pme_params_json PRIVATE pme::engine pme_build_options)
set_target_properties(pme_params_json PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)

# `cmake --build <dir> --target update_params_json` regenerates the committed file.
add_custom_target(update_params_json
  COMMAND pme_params_json "${PROJECT_SOURCE_DIR}/params.json"
  COMMENT "Regenerating params.json"
  VERBATIM)
