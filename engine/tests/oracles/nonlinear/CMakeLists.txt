# SPDX-License-Identifier: MIT
# A3 oracle test ORC-08 (SPEC-ORC "Impact time") with a rigid-wall test potential. It carries its own
# SPEC-NL scheme (ReferenceSavStepper) and through the engine (A3Stepper, pme::nl::savStep).
#
#   ctest --preset linux-debug -L oracle -R "ORC-08" --output-on-failure
pme_add_doctest(pme_oracle_nonlinear_tests
  SOURCES test_orc08_impact_time.cpp
  LIBRARIES pme_nonlinear
  LABELS oracle unit)
