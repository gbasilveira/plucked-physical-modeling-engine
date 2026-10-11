# SPDX-License-Identifier: MIT
# The CLAP plug-in (D1): pme_clap_shell (static, linked by the tests) and the module pme_clap.

add_library(pme_clap_shell STATIC src/plugin.cpp)
target_include_directories(pme_clap_shell PUBLIC src)
# The CLAP headers are a system include: third-party warnings never reach us (ADR-0003).
target_include_directories(pme_clap_shell SYSTEM PUBLIC "${clap_SOURCE_DIR}/include")
target_link_libraries(pme_clap_shell PUBLIC pme_plugin_common PRIVATE pme_build_options)
target_compile_definitions(pme_clap_shell PRIVATE
  PME_PLUGIN_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(pme_clap_shell PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
  POSITION_INDEPENDENT_CODE ON)

# The module: pme.clap (a bundle on macOS, with a minimal Info.plist).
add_library(pme_clap MODULE src/entry.cpp)
target_link_libraries(pme_clap PRIVATE pme_clap_shell pme_build_options)
set_target_properties(pme_clap PROPERTIES
  OUTPUT_NAME pme
  PREFIX ""
  SUFFIX ".clap"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
if(APPLE)
  set_target_properties(pme_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION clap
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "pme"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.pme.clap"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
  set(PME_CLAP_PATH "$<TARGET_BUNDLE_DIR:pme_clap>")
else()
  set(PME_CLAP_PATH "$<TARGET_FILE:pme_clap>")
endif()

# D1-T1: clap-validator over the built module. Skipped unless the validator's path is
# given in the cache variable or environment variable PME_CLAP_VALIDATOR (docs/dev/clap-shell.md).
set(PME_CLAP_VALIDATOR "" CACHE FILEPATH "Path to clap-validator (enables D1-T1 locally)")
add_test(NAME "D1-T1 clap-validator passes with the stub engine"
  COMMAND "${CMAKE_COMMAND}"
    "-DVALIDATOR=${PME_CLAP_VALIDATOR}"
    "-DPLUGIN=${PME_CLAP_PATH}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_validator.cmake")
set_tests_properties("D1-T1 clap-validator passes with the stub engine" PROPERTIES
  LABELS "integration" SKIP_REGULAR_EXPRESSION "D1-T1 skipped" TIMEOUT 600)

add_subdirectory(tests)
