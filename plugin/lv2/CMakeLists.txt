# SPDX-License-Identifier: MIT
# The LV2 plug-in (D3), Linux only (plugin/CMakeLists.txt). Builds
#   pme_lv2_turtle : Turtle generator library (no LV2 headers)
#   pme_lv2_gen    : host tool that writes manifest.ttl and the plug-in description
#   pme_lv2_shell  : the adapter over the controller layer (static, linked by the tests)
#   pme_lv2        : the module, inside the bundle directory build/<preset>/plugin/lv2/<bundle>.lv2/

option(PME_REQUIRE_LV2_TOOLS
       "Fail the configuration when lv2lint or lilv is missing (CI) instead of skipping D3-T1/D3-T2" OFF)

# The identifiers are provisional (decision D-3) and live in ids.hpp only; read them from there.
set(PME_IDS_HEADER "${PROJECT_SOURCE_DIR}/plugin/common/include/pme/plugin/ids.hpp")
function(_pme_lv2_id variable name)
  # The semicolon at the end of the declaration is not matched: it would split the CMake list.
  file(STRINGS "${PME_IDS_HEADER}" _line REGEX "${name} = \"[^\"]+\"")
  if(NOT _line MATCHES "${name} = \"([^\"]+)\"")
    message(FATAL_ERROR "plugin/lv2: ${name} not found in ${PME_IDS_HEADER}")
  endif()
  set(${variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
_pme_lv2_id(PME_LV2_URI kLv2PluginUri)
_pme_lv2_id(PME_LV2_BUNDLE_DIR kLv2BundleDir)
_pme_lv2_id(PME_LV2_BINARY kLv2BinaryName)

set(PME_LV2_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/${PME_LV2_BUNDLE_DIR}")
file(MAKE_DIRECTORY "${PME_LV2_BUNDLE}")

function(_pme_lv2_target_defaults target)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
endfunction()

# ---- generator ---------------------------------------------------------------------------------
add_library(pme_lv2_turtle STATIC src/turtle.cpp)
target_include_directories(pme_lv2_turtle PUBLIC src)
target_link_libraries(pme_lv2_turtle PUBLIC pme_plugin_common PRIVATE pme_build_options)
_pme_lv2_target_defaults(pme_lv2_turtle)

add_executable(pme_lv2_gen src/gen_ttl.cpp)
target_link_libraries(pme_lv2_gen PRIVATE pme_lv2_turtle pme_build_options)
_pme_lv2_target_defaults(pme_lv2_gen)

# ---- adapter and module ------------------------------------------------------------------------
add_library(pme_lv2_shell STATIC src/lv2_shell.cpp)
target_include_directories(pme_lv2_shell PUBLIC src)
# The LV2 headers are a system include: third-party warnings never reach us (ADR-0003).
target_include_directories(pme_lv2_shell SYSTEM PUBLIC "${lv2_SOURCE_DIR}/include")
target_link_libraries(pme_lv2_shell PUBLIC pme_lv2_turtle pme_plugin_common
                      PRIVATE pme_build_options)
_pme_lv2_target_defaults(pme_lv2_shell)

add_library(pme_lv2 MODULE src/entry.cpp)
target_link_libraries(pme_lv2 PRIVATE pme_lv2_shell pme_build_options)
_pme_lv2_target_defaults(pme_lv2)
set_target_properties(pme_lv2 PROPERTIES
  OUTPUT_NAME "${PME_LV2_BINARY}"
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${PME_LV2_BUNDLE}")
# Only lv2_descriptor is exported: hide the symbols of the static libraries too.
target_link_options(pme_lv2 PRIVATE "LINKER:--exclude-libs,ALL")

# manifest.ttl and the plug-in description are generated from ids.hpp and the registry.
set(PME_LV2_GENERATED "${PME_LV2_BUNDLE}/manifest.ttl" "${PME_LV2_BUNDLE}/${PME_LV2_BINARY}.ttl")
add_custom_command(
  OUTPUT ${PME_LV2_GENERATED}
  COMMAND pme_lv2_gen "${PME_LV2_BUNDLE}" "$<TARGET_FILE_NAME:pme_lv2>"
  DEPENDS pme_lv2_gen
  COMMENT "Generating the LV2 Turtle files"
  VERBATIM)
add_custom_target(pme_lv2_bundle ALL DEPENDS pme_lv2 ${PME_LV2_GENERATED})

# ---- tools -------------------------------------------------------------------------------------
# The LV2 specification bundles come from the pinned LV2 source (ADR-0003), so the tests need no
# system lv2-dev; the host test additionally needs lilv (pkg-config lilv-0), test-only.
# Hosts scan every directory of LV2_PATH for bundles, so the search directory holds only a link to
# the bundle (the build directory also holds libraries and CMake files).
set(PME_LV2_SEARCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/lv2path")
file(MAKE_DIRECTORY "${PME_LV2_SEARCH_DIR}")
file(CREATE_LINK "${PME_LV2_BUNDLE}" "${PME_LV2_SEARCH_DIR}/${PME_LV2_BUNDLE_DIR}" SYMBOLIC)
set(PME_LV2_PATH "${PME_LV2_SEARCH_DIR}:${lv2_SOURCE_DIR}/lv2")

find_program(PME_LV2LINT lv2lint)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PME_LILV QUIET IMPORTED_TARGET lilv-0)
endif()
if(PME_REQUIRE_LV2_TOOLS)
  if(NOT PME_LV2LINT)
    message(FATAL_ERROR "PME_REQUIRE_LV2_TOOLS is ON but lv2lint was not found (D3-T1)")
  endif()
  if(NOT PME_LILV_FOUND)
    message(FATAL_ERROR "PME_REQUIRE_LV2_TOOLS is ON but lilv-0 was not found (pkg-config; D3-T2)")
  endif()
endif()

# D3-T1: lv2lint on the built bundle.
add_test(NAME "D3-T1 lv2lint passes"
  COMMAND "${CMAKE_COMMAND}"
    "-DLV2LINT=${PME_LV2LINT}"
    "-DBUNDLE=${PME_LV2_BUNDLE}"
    "-DLV2_PATH=${PME_LV2_PATH}"
    "-DURI=${PME_LV2_URI}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_lv2lint.cmake")
set_tests_properties("D3-T1 lv2lint passes" PROPERTIES
  LABELS "integration" SKIP_REGULAR_EXPRESSION "D3-T1 skipped" TIMEOUT 300)

# D3-T2: the headless lilv host.
if(PME_LILV_FOUND)
  find_package(Threads REQUIRED)
  add_executable(pme_lv2_host tests/lv2_host.cpp)
  target_link_libraries(pme_lv2_host PRIVATE PkgConfig::PME_LILV Threads::Threads pme_build_options)
  target_include_directories(pme_lv2_host SYSTEM PRIVATE "${lv2_SOURCE_DIR}/include")
  target_include_directories(pme_lv2_host PRIVATE src)
  target_link_libraries(pme_lv2_host PRIVATE pme_lv2_turtle)
  _pme_lv2_target_defaults(pme_lv2_host)
  add_dependencies(pme_lv2_host pme_lv2_bundle)
  add_test(NAME "D3-T2 a headless LV2 host loads the plug-in and renders a test scenario"
    COMMAND pme_lv2_host "${PME_LV2_URI}")
  set_tests_properties("D3-T2 a headless LV2 host loads the plug-in and renders a test scenario"
    PROPERTIES LABELS "integration" TIMEOUT 300
    ENVIRONMENT "LV2_PATH=${PME_LV2_PATH}")
else()
  add_test(NAME "D3-T2 a headless LV2 host loads the plug-in and renders a test scenario"
    COMMAND "${CMAKE_COMMAND}" -E echo "D3-T2 skipped: lilv-0 (pkg-config) not found")
  set_tests_properties("D3-T2 a headless LV2 host loads the plug-in and renders a test scenario"
    PROPERTIES LABELS "integration" SKIP_REGULAR_EXPRESSION "D3-T2 skipped")
endif()

add_subdirectory(tests)
