# SPDX-License-Identifier: MIT
# D1-T1 driver: run `clap-validator validate <plugin>` with every test enabled.
# Inputs: -DVALIDATOR=<path> (or environment PME_CLAP_VALIDATOR), -DPLUGIN=<.clap path>.
# Without a validator the script prints "D1-T1 skipped", which ctest reports as a skip
# (SKIP_REGULAR_EXPRESSION).

if(NOT VALIDATOR)
  set(VALIDATOR "$ENV{PME_CLAP_VALIDATOR}")
endif()
if(NOT VALIDATOR)
  message(STATUS "D1-T1 skipped: set PME_CLAP_VALIDATOR to the clap-validator executable")
  return()
endif()
if(NOT EXISTS "${VALIDATOR}")
  message(FATAL_ERROR "clap-validator not found: ${VALIDATOR}")
endif()
if(NOT EXISTS "${PLUGIN}")
  message(FATAL_ERROR "plug-in not found: ${PLUGIN}")
endif()

execute_process(COMMAND "${VALIDATOR}" --version OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "validator: ${version}")
execute_process(
  COMMAND "${VALIDATOR}" validate "${PLUGIN}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "clap-validator failed (exit status ${result})")
endif()
