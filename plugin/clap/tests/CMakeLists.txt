# SPDX-License-Identifier: MIT
# D1 CLAP-adapter tests (D1-R1, D1-T2, D1-T3 and the extension tests), built with pme_add_doctest.

pme_add_doctest(pme_clap_tests
  SOURCES test_clap_shell.cpp test_clap_state.cpp test_clap_process.cpp
  LIBRARIES pme_clap_shell
  LABELS unit
  RT)
target_include_directories(pme_clap_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/plugin/common/tests")
target_compile_definitions(pme_clap_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)
