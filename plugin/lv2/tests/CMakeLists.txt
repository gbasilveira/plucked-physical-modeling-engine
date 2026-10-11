# SPDX-License-Identifier: MIT
# D3 adapter tests (D3-R1, D3-R2), built with pme_add_doctest. They drive the adapter's run
# directly (no lilv) with the recording fake EngineAdapter and hand-built atom sequences.

pme_add_doctest(pme_lv2_tests
  SOURCES test_lv2_run.cpp test_lv2_state.cpp test_lv2_turtle.cpp
  LIBRARIES pme_lv2_shell
  LABELS unit
  RT)
target_include_directories(pme_lv2_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/plugin/common/tests")
target_compile_definitions(pme_lv2_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)
