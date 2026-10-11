# SPDX-License-Identifier: MIT
# pme_add_module(<name>): static library pme_<name> (alias pme::<name>) from engine/src/<name>/*.cpp.

function(pme_add_module name)
  file(GLOB sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/engine/src/${name}/*.cpp")
  add_library(pme_${name} STATIC ${sources})
  add_library(pme::${name} ALIAS pme_${name})
  target_include_directories(pme_${name} PUBLIC "${PROJECT_SOURCE_DIR}/engine/include")
  target_link_libraries(pme_${name} PRIVATE pme_build_options)
  set_target_properties(pme_${name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON)
endfunction()
