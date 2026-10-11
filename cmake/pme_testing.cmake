# SPDX-License-Identifier: MIT
#
# Test and benchmark helpers (P0.3-R1, P0.3-R6, P0.3-R5).
#
#   pme_add_doctest(<target> SOURCES <files...> [LIBRARIES <targets...>] LABELS <labels...> [RT]
#                   [TIMEOUT <seconds>])
#   pme_add_benchmark(<target> SOURCES <files...> [LIBRARIES <targets...>] [ARGS <args...>])
#
# Every doctest TEST_CASE becomes its own CTest test (discovered at build time by running the
# executable with --list-test-cases), so JUnit reports and `ctest -R` work per test case. Test names
# must not contain a comma, an asterisk or a question mark (doctest filters treat them specially);
# tools/tests/test_p0_3_r1.py enforces this.
#
# TIMEOUT is the CTest timeout in seconds of every discovered test case (default 300; the rtcheck
# death tests pass 120). The discovery script writes it into set_tests_properties.
#
# This file doubles as the discovery script: build rules run `cmake -DPME_DOCTEST_DISCOVER=ON ... -P`
# on it, which is why the first block below handles script mode.

set(PME_TEST_LABELS unit oracle integration golden rt perf fuzz soak)

# ---- Discovery (script mode) ------------------------------------------------------------------
function(_pme_doctest_discover)
  execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-test-cases
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
    WORKING_DIRECTORY "${TEST_WORKING_DIR}")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Error running '${TEST_EXECUTABLE}' (result ${result}):\n${output}")
  endif()
  string(REPLACE "," ";" labels "${TEST_LABELS}")
  string(REPLACE "\n" ";" lines "${output}")
  set(script "")
  set(names "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^=+$" OR line MATCHES "^\\[doctest\\] " OR line STREQUAL "")
      continue()
    endif()
    string(REPLACE "," "\\," filter "${line}")
    string(APPEND script
      "add_test([==[${line}]==] [==[${TEST_EXECUTABLE}]==] [==[--test-case=${filter}]==])\n"
      "set_tests_properties([==[${line}]==] PROPERTIES WORKING_DIRECTORY [==[${TEST_WORKING_DIR}]==]"
      " LABELS [==[${labels}]==] TIMEOUT ${TEST_TIMEOUT})\n")
    list(APPEND names "${line}")
  endforeach()
  string(APPEND script "set(${TEST_TARGET}_TESTS [==[${names}]==])\n")
  file(WRITE "${CTEST_FILE}" "${script}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND PME_DOCTEST_DISCOVER)
  _pme_doctest_discover()
  return()
endif()

# ---- Helpers ----------------------------------------------------------------------------------
function(pme_add_doctest target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "RT" "TIMEOUT" "SOURCES;LIBRARIES;LABELS")
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "pme_add_doctest(${target}): SOURCES is required")
  endif()
  if(NOT ARG_LABELS)
    message(FATAL_ERROR "pme_add_doctest(${target}): LABELS is required "
                        "(one or more of: ${PME_TEST_LABELS})")
  endif()
  if(NOT DEFINED ARG_TIMEOUT)
    set(ARG_TIMEOUT 300)
  endif()
  if(NOT ARG_TIMEOUT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "pme_add_doctest(${target}): TIMEOUT must be a positive integer number of "
                        "seconds, got '${ARG_TIMEOUT}'")
  endif()
  set(labels ${ARG_LABELS})
  if(ARG_RT)
    list(APPEND labels rt)
  endif()
  list(REMOVE_DUPLICATES labels)
  foreach(label IN LISTS labels)
    if(NOT label IN_LIST PME_TEST_LABELS)
      message(FATAL_ERROR "pme_add_doctest(${target}): unknown label '${label}' "
                          "(allowed: ${PME_TEST_LABELS})")
    endif()
  endforeach()

  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE
    pme_build_options pme_test_framework doctest::doctest ${ARG_LIBRARIES})
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON)

  # A comma, not a pipe: cmd.exe (the Ninja generator on Windows) treats an unquoted-looking | as a pipe.
  string(REPLACE ";" "," label_arg "${labels}")
  set(tests_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_tests.cmake")
  set(include_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_include.cmake")
  add_custom_command(TARGET ${target} POST_BUILD
    BYPRODUCTS "${tests_file}"
    COMMAND "${CMAKE_COMMAND}"
      -D PME_DOCTEST_DISCOVER=ON
      -D "TEST_TARGET=${target}"
      -D "TEST_EXECUTABLE=$<TARGET_FILE:${target}>"
      -D "TEST_WORKING_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      -D "TEST_LABELS=${label_arg}"
      -D "TEST_TIMEOUT=${ARG_TIMEOUT}"
      -D "CTEST_FILE=${tests_file}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
    VERBATIM)
  file(WRITE "${include_file}"
    "if(EXISTS [==[${tests_file}]==])\n"
    "  include([==[${tests_file}]==])\n"
    "else()\n"
    "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)\n"
    "endif()\n")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${include_file}")
endfunction()

function(pme_add_benchmark target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES;ARGS")
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "pme_add_benchmark(${target}): SOURCES is required")
  endif()
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE pme_build_options pme_bench ${ARG_LIBRARIES})
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON)
  add_test(NAME ${target}
    COMMAND ${target} --out "${CMAKE_CURRENT_BINARY_DIR}/${target}.json" ${ARG_ARGS})
  set_tests_properties(${target} PROPERTIES LABELS perf)
endfunction()

# ---- Engine isolation (P0.3-R5, ADR-0005) -----------------------------------------------------
# The analysis library (WAV, JSON), the test framework and the benchmark harness are never linked
# into the engine. Checked after all targets exist, over the transitive link closure.
set(PME_ENGINE_TARGETS pme_osc pme_string pme_bridge pme_body pme_nonlinear pme_exciters
    pme_dampers pme_barriers pme_runtime pme_sched pme_simd pme_engine)
set(PME_ENGINE_FORBIDDEN pme_analysis pme_test_framework pme_bench nlohmann_json)

function(_pme_real_target name out)
  set(real "${name}")
  if(TARGET "${real}")
    get_target_property(aliased "${real}" ALIASED_TARGET)
    if(aliased)
      set(real "${aliased}")
    endif()
  endif()
  set(${out} "${real}" PARENT_SCOPE)
endfunction()

function(_pme_check_engine_isolation)
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
      get_target_property(type ${current} TYPE)
      set(deps "")
      foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${current} ${prop})
        if(value)
          list(APPEND deps ${value})
        endif()
      endforeach()
      foreach(dep IN LISTS deps)
        string(REGEX REPLACE "^\\$<(LINK_ONLY|BUILD_INTERFACE|BUILD_LOCAL_INTERFACE):(.*)>$" "\\2"
               dep "${dep}")
        if(dep MATCHES "[$<>]" OR NOT TARGET "${dep}")
          continue()
        endif()
        _pme_real_target("${dep}" real)
        if(real IN_LIST PME_ENGINE_FORBIDDEN)
          message(FATAL_ERROR
            "Engine isolation violated: ${root} reaches '${real}' (through ${current}). "
            "pme_analysis, pme_test_framework, pme_bench and nlohmann_json must never be linked "
            "into engine targets (P0.3-R5, ADR-0005).")
        endif()
        list(APPEND todo ${real})
      endforeach()
    endwhile()
  endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _pme_check_engine_isolation)
