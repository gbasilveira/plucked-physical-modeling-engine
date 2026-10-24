# SPDX-License-Identifier: MIT
# Plug-in shells (D1+). Built only with -DPME_BUILD_PLUGINS=ON (root CMakeLists.txt).
#   common/ : format-independent controller layer (D1; D3 reuses it for LV2)
#   clap/   : the CLAP plug-in (D1)
#   lv2/    : the LV2 plug-in (D3; Linux only)
# D2 adds its directory here.

# A plug-in module is a shared library: every static library linked into it needs
# position-independent code. The engine modules and the third-party libraries are created in
# the root scope before this directory, so the property is set on them explicitly.
foreach(_pme_target IN ITEMS ${PME_MODULES} rtcheck)
  set_target_properties(pme_${_pme_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
if(TARGET hwy)
  set_target_properties(hwy PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(common)
add_subdirectory(clap)

# D3: LV2 is built for Linux hosts only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(lv2)
endif()
