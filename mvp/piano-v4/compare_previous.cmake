# SPDX-License-Identifier: MIT
# Renders the pinned Chopin excerpt with `--previous` (0.5.3) and requires the committed 0.5.3 raw WAV bit for bit.
# usage: cmake -DRENDER=<piano-midi-render> -DEVENTS=<file> -DREFERENCE=<chopin-prelude-0.5.3-raw.wav> -DOUT=<tmp.wav> -P compare_previous.cmake
execute_process(COMMAND "${RENDER}" "${EVENTS}" "${OUT}" --previous --threads 1 RESULT_VARIABLE rc OUTPUT_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "render failed: ${log}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${REFERENCE}" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "--previous output differs from ${REFERENCE}")
endif()
message(STATUS "PASS: --previous reproduces the committed 0.5.3 raw render byte for byte (${log})")
