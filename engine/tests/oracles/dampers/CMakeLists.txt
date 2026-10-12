# SPDX-License-Identifier: MIT
# A6 oracle test ORC-13 (SPEC-ORC "Damper (diagonal)"; SPEC-DMP "Damper model", "Diagonal").
# It uses the public headers pme/osc.hpp and pme/string.hpp, the A1 ModeBank (internal header of
# pme_osc, documented in docs/dev/osc.md), pme_analysis for the decay fit, and the A2 test-author's
# SPEC helpers (quadrature, raised cosine, wound-string formulas) from ../string.
#
# Run:
#   ctest --preset linux-debug -L oracle -R "ORC-13" --output-on-failure
#
# Wiring A6's diagonal-damper coefficient function in after A6 is merged: see the comment block
# "A6 SEAM" at the top of test_orc13_damper_diagonal.cpp. In short, write a6_adapter.hpp next to the
# test that defines pme::oracle::dmp::a6Coefficients, add the A6 library to LIBRARIES below, and
# enable the commented target_compile_definitions line (PME_ORC13_WIRE_A6).
pme_add_doctest(pme_oracle_dampers_tests
  SOURCES test_orc13_damper_diagonal.cpp
  LIBRARIES pme_string pme_osc pme_analysis
  LABELS oracle unit
  TIMEOUT 600)
target_include_directories(pme_oracle_dampers_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../string")
# target_compile_definitions(pme_oracle_dampers_tests PRIVATE PME_ORC13_WIRE_A6)
