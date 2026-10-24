# SPDX-License-Identifier: MIT
# A1 oracle tests ORC-01 and ORC-02 (SPEC-ORC "Oscillator accuracy", "Oscillator energy").
# They use only the public header engine/include/pme/osc.hpp (pme::compileOscillator) and carry their
# own SPEC-OSC difference-form kernels, so they link against pme_osc and nothing else.
#
# Run after A1 is merged:
#   ctest --preset linux-debug -L oracle -R "ORC-0[12]" --output-on-failure
pme_add_doctest(pme_oracle_osc_tests
  SOURCES test_orc01_accuracy.cpp test_orc02_energy.cpp
  LIBRARIES pme_osc
  LABELS oracle unit)
