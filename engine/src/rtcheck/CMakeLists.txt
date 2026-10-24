# SPDX-License-Identifier: MIT
# pme_rtcheck: RT-safety trap library (P0.2-R5). Not part of pme_engine; tests and the plug-in
# link it. See docs/dev/rtcheck.md.

if(PME_RTSAFETY AND NOT "${PME_SANITIZER}" STREQUAL "")
  message(FATAL_ERROR "PME_RTSAFETY cannot be combined with PME_SANITIZER=${PME_SANITIZER}: "
                      "the trap replaces the global allocation functions, which the sanitizer "
                      "runtimes also own.")
endif()

add_library(pme_rtcheck STATIC rtcheck.cpp)
add_library(pme::rtcheck ALIAS pme_rtcheck)
target_include_directories(pme_rtcheck PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(pme_rtcheck PRIVATE pme_build_options ${CMAKE_DL_LIBS})
if(PME_RTSAFETY)
  # Consumers see the same enabled() as the library itself (one definition).
  target_compile_definitions(pme_rtcheck PUBLIC PME_RTSAFETY=1)
endif()
set_target_properties(pme_rtcheck PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  COMPILE_WARNING_AS_ERROR ON)
