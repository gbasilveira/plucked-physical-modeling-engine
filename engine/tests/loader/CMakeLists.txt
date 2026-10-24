# SPDX-License-Identifier: MIT
# B1 tests: instrument file loader (B1-T1 to B1-T3, B1-R3, B1-R4). Fixtures live in
# compiler/tests/data/b1 and are shared with the Python tests; the examples live in
# instruments/examples.

pme_add_doctest(pme_loader_tests
  SOURCES test_sha256.cpp test_roundtrip.cpp test_malformed.cpp test_migration.cpp test_robustness.cpp test_compiled_body.cpp
  LIBRARIES pme_loader pme_runtime nlohmann_json::nlohmann_json
  LABELS unit)
# pme::toString(Status) is found by ADL and would hijack doctest's toString (see engine/tests/interfaces).
target_compile_definitions(pme_loader_tests PRIVATE
  DOCTEST_CONFIG_DOUBLE_STRINGIFY
  DOCTEST_CONFIG_USE_STD_HEADERS
  "PME_B1_FIXTURES=\"${PROJECT_SOURCE_DIR}/compiler/tests/data/b1\""
  "PME_B2_FIXTURES=\"${PROJECT_SOURCE_DIR}/compiler/tests/data/b2\""
  "PME_B1_EXAMPLES=\"${PROJECT_SOURCE_DIR}/instruments/examples\"")
# formatDouble and the other internals are tested directly.
target_include_directories(pme_loader_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src/runtime/loader")

# B1-R4: the loader is not an engine target and must never enter the link closure of one. This
# complements the nlohmann_json check in cmake/pme_testing.cmake, which fails the configure step.
function(_pme_check_loader_not_in_engine)
  foreach(root IN LISTS PME_ENGINE_TARGETS)
    if(NOT TARGET ${root})
      continue()
    endif()
    set(todo ${root})
    set(seen "")
    while(todo)
      list(POP_FRONT todo current)
      if(current IN_LIST seen)
        continue()
      endif()
      list(APPEND seen ${current})
      foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${current} ${prop})
        foreach(dep IN LISTS value)
          if(dep MATCHES "pme_loader|pme::loader")
            message(FATAL_ERROR "B1-R4: ${root} reaches the instrument loader (through ${current}); "
                                "pme_loader must not be linked into engine targets.")
          endif()
          if(TARGET "${dep}")
            list(APPEND todo "${dep}")
          endif()
        endforeach()
      endforeach()
    endwhile()
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _pme_check_loader_not_in_engine)
