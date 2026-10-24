# SPDX-License-Identifier: MIT
#
# Third-party C++ dependencies (ADR-0003). Every CPMAddPackage call lives in this file.
# Every GIT_TAG is a full 40-hex commit SHA, never a tag or branch. To change a pin, follow
# "Changing a pin" in ADR-0003 and update the dependency table in docs/dev/build.md.
# CPM_SOURCE_CACHE is honoured if set; CPM_USE_LOCAL_PACKAGES stays off.
#
# CPM.cmake (vendored in cmake/CPM.cmake)
#   upstream : https://github.com/cpm-cmake/CPM.cmake
#   release  : v0.43.2 (commit 01678cfe175eab53143640429fdaa57c3c621cc6)
#   asset    : https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.2/CPM.cmake
#   sha256   : 49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa
#   licence  : MIT

set(PME_CPM_SHA256 "49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa")
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/CPM.cmake" PME_CPM_ACTUAL_SHA256)
if(NOT PME_CPM_ACTUAL_SHA256 STREQUAL PME_CPM_SHA256)
  message(FATAL_ERROR "cmake/CPM.cmake does not match the recorded SHA-256 (${PME_CPM_ACTUAL_SHA256}); "
                      "see cmake/dependencies.cmake and ADR-0003.")
endif()

# Consumers of the optional dependencies. PME_FETCH_ALL_DEPS fetches everything (proves every pin
# resolves); the two options below fetch only what their consumer needs.
option(PME_BUILD_PLUGINS "Build the plug-in shells (D1+)" OFF)
option(PME_BUILD_PYTHON_BINDINGS "Build the nanobind Python modules (P0.5+)" OFF)

set(CPM_USE_LOCAL_PACKAGES OFF)
include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

# Highway
#   upstream : https://github.com/google/highway
#   release  : 1.4.0 (lightweight tag; commit below)
#   licence  : Apache-2.0 OR BSD-3-Clause (ADR-0001)
#   purpose  : SIMD kernels with runtime dispatch; linked privately into pme_simd only
#   first built: P0.1 (every preset); kernels in A8
CPMAddPackage(
  NAME highway
  GIT_REPOSITORY https://github.com/google/highway
  GIT_TAG 2607d3b5b0113992fe84d3848859eae13b3b52c1
  VERSION 1.4.0
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
  OPTIONS
    "HWY_ENABLE_TESTS OFF"
    "HWY_ENABLE_EXAMPLES OFF"
    "HWY_ENABLE_CONTRIB OFF"
    "HWY_ENABLE_INSTALL OFF"
)

# doctest
#   upstream : https://github.com/doctest/doctest
#   release  : v2.5.3 (lightweight tag; commit below)
#   licence  : MIT
#   purpose  : C++ unit-test framework (header-only)
#   first built: P0.3
CPMAddPackage(
  NAME doctest
  GIT_REPOSITORY https://github.com/doctest/doctest
  GIT_TAG 2d0a9359a60c51affe2a9bebb1be1dca47868151
  VERSION 2.5.3
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
  OPTIONS
    "DOCTEST_WITH_TESTS OFF"
    "DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF"
    "DOCTEST_NO_INSTALL ON"
)

# nlohmann/json (ADR-0005)
#   upstream : https://github.com/nlohmann/json
#   release  : v3.12.0 (annotated tag; peeled commit below)
#   licence  : MIT
#   purpose  : JSON in C++ tests and tools (golden features, benchmark output); never linked into
#              the engine modules (checked at configure time in tools/analysis/CMakeLists.txt)
#   first built: P0.3
CPMAddPackage(
  NAME nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72
  VERSION 3.12.0
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
  OPTIONS
    "JSON_BuildTests OFF"
    "JSON_Install OFF"
    "JSON_MultipleHeaders OFF"
)

if(PME_FETCH_ALL_DEPS OR PME_BUILD_PYTHON_BINDINGS)
  # nanobind
  #   upstream : https://github.com/wjakob/nanobind
  #   release  : v3.1.0 (annotated tag; peeled commit below)
  #   licence  : BSD-3-Clause
  #   purpose  : Python bindings
  #   first built: P0.5 (PME_BUILD_PYTHON_BINDINGS); bindings in C8
  CPMAddPackage(
    NAME nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind
    GIT_TAG 68480a9e6883bb1bf65e1925423321e2a89d07f9
    VERSION 3.1.0
    DOWNLOAD_ONLY YES
  )
endif()

if(PME_FETCH_ALL_DEPS OR PME_BUILD_PLUGINS)
  # CLAP
  #   upstream : https://github.com/free-audio/clap
  #   release  : 1.2.10 (lightweight tag; commit below)
  #   licence  : MIT
  #   purpose  : CLAP plug-in API headers
  #   first built: D1
  CPMAddPackage(
    NAME clap
    GIT_REPOSITORY https://github.com/free-audio/clap
    GIT_TAG 195b42a004144fab0b3cf95e9c067187d15365b7
    VERSION 1.2.10
    DOWNLOAD_ONLY YES
  )
endif()

if(PME_FETCH_ALL_DEPS)
  # clap-wrapper
  #   upstream : https://github.com/free-audio/clap-wrapper
  #   release  : v0.16.0 (lightweight tag; commit below)
  #   licence  : MIT (the wrapper); the SDKs it can download keep their own licences
  #   purpose  : VST3, AUv2 and standalone builds of the CLAP plug-in
  #   first built: D2
  #   For D1/D2: pass our pinned CLAP and VST 3 SDK source directories to clap-wrapper
  #   (CLAP_SDK_ROOT, VST3_SDK_ROOT) so it downloads nothing unpinned. The AudioUnit SDK
  #   pin is added by D2. Nothing in P0.1 builds clap-wrapper, so none of its downloads run.
  CPMAddPackage(
    NAME clap_wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper
    GIT_TAG 1cca996e96f29ab2be7ae9f8cfe532bbc92e1dd6
    VERSION 0.16.0
    DOWNLOAD_ONLY YES
  )

  # VST 3 SDK
  #   upstream : https://github.com/steinbergmedia/vst3sdk
  #   release  : v3.8.1_build_84 (lightweight tag; commit below)
  #   licence  : MIT (3.8 and later)
  #   purpose  : VST3 plug-in API
  #   first built: D2
  CPMAddPackage(
    NAME vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk
    GIT_TAG 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
    VERSION 3.8.1
    DOWNLOAD_ONLY YES
  )
endif()

if(PME_FETCH_ALL_DEPS OR (PME_BUILD_PLUGINS AND CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  # LV2 (GitHub mirror of https://gitlab.com/lv2/lv2)
  #   upstream : https://github.com/lv2/lv2
  #   release  : v1.18.10 (annotated tag; peeled commit below)
  #   licence  : ISC (mixed: some files 0BSD, MIT or W3C terms; see LICENSES/ in the source)
  #   purpose  : LV2 plug-in API
  #   first built: D3
  CPMAddPackage(
    NAME lv2
    GIT_REPOSITORY https://github.com/lv2/lv2
    GIT_TAG 0bcde338db1c63bbc503b4d1f6d7b55ed43154af
    VERSION 1.18.10
    DOWNLOAD_ONLY YES
  )
endif()
