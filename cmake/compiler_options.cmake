# SPDX-License-Identifier: MIT
# pme_build_options: compile settings for project targets only (never third-party targets).
#
# Numerics rule 2: never -ffast-math, -Ofast, -funsafe-math-optimizations or /fp:fast.
# FMA contraction is fixed per build: off on GCC/Clang/AppleClang/clang-cl, /fp:precise on MSVC.

add_library(pme_build_options INTERFACE)
target_compile_features(pme_build_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(pme_build_options INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl: /fp:precise is -ffp-model=precise, which turns contraction on, and combining it with
    # -ffp-contract=off is an error (-Woverriding-option). Without /fp:precise clang-cl is already
    # strict IEEE (no fast-math); switch contraction off explicitly.
    target_compile_options(pme_build_options INTERFACE /clang:-ffp-contract=off)
  else()
    target_compile_options(pme_build_options INTERFACE /fp:precise)
  endif()
else()
  target_compile_options(pme_build_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -ffp-contract=off)
endif()

if(PME_RTSAFETY)
  target_compile_definitions(pme_build_options INTERFACE PME_RTSAFETY=1)
endif()
