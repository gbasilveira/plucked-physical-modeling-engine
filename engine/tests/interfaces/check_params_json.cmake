# SPDX-License-Identifier: MIT
# Runs pme_params_json and fails unless its output equals the committed params.json (P0.4-R4).
# Inputs: GENERATOR, OUTPUT, COMMITTED.
execute_process(COMMAND "${GENERATOR}" "${OUTPUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "pme_params_json failed with exit code ${rc}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}" "${COMMITTED}" RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
  message(FATAL_ERROR "params.json differs from the registry. Regenerate it: "
                      "cmake --build <build-dir> --target update_params_json")
endif()
