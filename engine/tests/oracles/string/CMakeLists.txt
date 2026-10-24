# SPDX-License-Identifier: MIT
# A2 oracle tests ORC-03, ORC-04, ORC-05 and A2-T3 (SPEC-ORC "Stiff-string frequencies", "Tension
# for pitch", "Window shape"; phase card A2). They use the public header engine/include/pme/string.hpp
# and, for ORC-05 only, the A2 internal window functions through string_internal.hpp.
#
# Run after A2 is merged:
#   ctest --preset linux-debug -L oracle -R "ORC-0[345]" --output-on-failure
#
# A2-T3 compares against the P0.5 reference solver. The reference case is compiled in only when the
# target pme_reference_cli exists (P0.5 merged) and the build is optimised (Release or
# RelWithDebInfo; a fine reference grid is too slow in a debug build). Run it after merging both A2
# and P0.5 with:
#   cmake --preset <an optimised preset>    # for example linux-release
#   cmake --build --preset <that preset>
#   ctest --preset <that preset> -R "A2-T3" --output-on-failure
# In any build, "A2-T3 engine side ..." (no reference needed) runs with the label integration.
# PME_A2T3_N0 (default 400) and PME_A2T3_OS0 (default 4) set the coarsest reference grid
# intervals and oversampling; levels 0, 1, 2 double both.

pme_add_doctest(pme_oracle_string_tests
  SOURCES test_orc03_orc04_frequencies.cpp
  LIBRARIES pme_string
  LABELS oracle unit)

pme_add_doctest(pme_oracle_string_window_tests
  SOURCES test_orc05_window.cpp
  LIBRARIES pme_string
  LABELS oracle unit)

pme_add_doctest(pme_oracle_string_t3_tests
  SOURCES test_a2t3_reference.cpp
  LIBRARIES pme_string pme_osc pme_analysis
  LABELS integration
  TIMEOUT 600)

# P0.5's targets are defined after this directory is processed (the root CMakeLists.txt appends the
# P0.5 block at its end), so decide at the end of the root directory's configuration.
function(pme_a2t3_register_reference)
  if(TARGET pme_reference_cli)
    target_compile_definitions(pme_oracle_string_t3_tests PRIVATE
      "$<$<CONFIG:Release,RelWithDebInfo>:PME_A2T3_REFERENCE_CLI=\"$<TARGET_FILE:pme_reference_cli>\">")
    add_dependencies(pme_oracle_string_t3_tests pme_reference_cli)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL pme_a2t3_register_reference)
