# SPDX-License-Identifier: MIT
# D1 controller-layer tests (D1-R2, D1-R3, D1-R4, D1-R5, D1-R6, D1-R7), built with pme_add_doctest.
# The audio-path tests carry the rt label, so the rt-safety build runs them under the trap.

pme_add_doctest(pme_plugin_common_tests
  SOURCES test_event_translator.cpp test_param_model.cpp test_state.cpp test_task_runner.cpp
          test_controller.cpp
  LIBRARIES pme_plugin_common
  LABELS unit
  RT)
target_include_directories(pme_plugin_common_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# pme::toString(Status) is found by ADL and would hijack doctest's unqualified toString().
target_compile_definitions(pme_plugin_common_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY DOCTEST_CONFIG_USE_STD_HEADERS)
