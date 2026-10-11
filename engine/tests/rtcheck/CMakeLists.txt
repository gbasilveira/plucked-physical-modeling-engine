# SPDX-License-Identifier: MIT
# Tests of pme_rtcheck (P0.2-T1, P0.2-R5), built with P0.3's pme_add_doctest.

if(PME_RTSAFETY)
  # The trap build: death tests re-launch this executable as a child process (P0.2-T1), so it keeps
  # its own main() (which handles --pme-rtcheck-child=<mode>) instead of the framework's.
  pme_add_doctest(pme_rtcheck_tests
    SOURCES main.cpp child_modes.cpp process.cpp test_rtcheck.cpp
    LIBRARIES pme::rtcheck
    LABELS unit RT)
else()
  # Every other build: the library is a no-op and replaces nothing.
  pme_add_doctest(pme_rtcheck_noop_tests
    SOURCES test_rtcheck_noop.cpp
    LIBRARIES pme::rtcheck
    LABELS unit)
endif()
