# SPDX-License-Identifier: MIT
# Sanitizer and coverage instrumentation selected by PME_SANITIZER (set by the presets).
# Applied globally so that third-party code is instrumented consistently.

if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

if(PME_SANITIZER STREQUAL "asan")
  if(MSVC)
    # MSVC AddressSanitizer: incompatible with /RTC1 and incremental linking; keep /Zi.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
elseif(PME_SANITIZER STREQUAL "tsan")
  if(MSVC)
    message(FATAL_ERROR "ThreadSanitizer is not available on Windows (ADR-0004).")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(PME_SANITIZER STREQUAL "coverage")
  if(MSVC)
    # Full PDBs for an external coverage tool (P0.2). No source-based coverage with clang-cl.
    add_compile_options(/Zi)
    add_link_options(/DEBUG:FULL /INCREMENTAL:NO)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(--coverage)
    add_link_options(--coverage)
  else()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
  endif()
elseif(NOT PME_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown PME_SANITIZER '${PME_SANITIZER}'")
endif()
