# SPDX-License-Identifier: MIT
# D3-T1 driver: run lv2lint over the built bundle.
# Inputs: -DLV2LINT=<path or empty>, -DBUNDLE=<bundle dir>, -DLV2_PATH=<search path: the bundle's
# parent plus the LV2 specification bundles of the pinned LV2 source>, -DURI=<plug-in URI>.
# Without lv2lint the script prints "D3-T1 skipped", which ctest reports as a skip
# (SKIP_REGULAR_EXPRESSION). No check is disabled: see "lv2lint" in docs/dev/lv2.md.

if(NOT LV2LINT)
  message(STATUS "D3-T1 skipped: lv2lint not found")
  return()
endif()
if(NOT EXISTS "${BUNDLE}/manifest.ttl")
  message(FATAL_ERROR "bundle not built: ${BUNDLE}/manifest.ttl")
endif()

execute_process(COMMAND "${LV2LINT}" -v OUTPUT_VARIABLE version ERROR_VARIABLE version_err
                OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "lv2lint: ${version}${version_err}")
set(ENV{LV2_PATH} "${LV2_PATH}")
execute_process(
  COMMAND "${LV2LINT}" -Mpack "${URI}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "lv2lint failed (exit status ${result})")
endif()
