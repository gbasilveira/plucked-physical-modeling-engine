# SPDX-License-Identifier: MIT
# Tests of pme_rtcheck (P0.2-T1, P0.2-R5). Until P0.3's pme_add_doctest helper is adopted, the
# executables are built directly: doctest::doctest, pme_build_options, own main.cpp.

if(PME_RTSAFETY)
  # The trap build: death tests re-launch this executable as a child process (P0.2-T1).
  add_executable(pme_rtcheck_tests main.cpp child_modes.cpp process.cpp test_rtcheck.cpp)
  target_link_libraries(pme_rtcheck_tests PRIVATE pme::rtcheck doctest::doctest pme_build_options)
  set_target_properties(pme_rtcheck_tests PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF COMPILE_WARNING_AS_ERROR ON)
  add_test(NAME pme_rtcheck_tests COMMAND pme_rtcheck_tests)
  set_tests_properties(pme_rtcheck_tests PROPERTIES LABELS "unit;rt" TIMEOUT 120)
else()
  # Every other build: the library is a no-op and replaces nothing.
  add_executable(pme_rtcheck_noop_tests test_rtcheck_noop.cpp)
  target_link_libraries(pme_rtcheck_noop_tests PRIVATE pme::rtcheck doctest::doctest
                                                       pme_build_options)
  target_compile_definitions(pme_rtcheck_noop_tests PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
  set_target_properties(pme_rtcheck_noop_tests PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF COMPILE_WARNING_AS_ERROR ON)
  add_test(NAME pme_rtcheck_noop_tests COMMAND pme_rtcheck_noop_tests)
  set_tests_properties(pme_rtcheck_noop_tests PROPERTIES LABELS "unit" TIMEOUT 60)
endif()
