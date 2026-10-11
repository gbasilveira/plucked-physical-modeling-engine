# SPDX-License-Identifier: MIT
# A3 oracle test ORC-08 (SPEC-ORC "Impact time") with a rigid-wall test potential. It carries its own
# SPEC-NL scheme (ReferenceSavStepper), so it needs no engine target to build; see the comment at
# the top of test_orc08_impact_time.cpp for how to run the same cases through A3's savStep.
#
#   ctest --preset linux-debug -L oracle -R "ORC-08" --output-on-failure
pme_add_doctest(pme_oracle_nonlinear_tests
  SOURCES test_orc08_impact_time.cpp
  LABELS oracle unit)
