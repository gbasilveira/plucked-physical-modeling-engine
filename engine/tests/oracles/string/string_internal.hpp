// SPDX-License-Identifier: MIT
// The one place where the A2 oracle tests touch the A2 internal API (namespace pme::string,
// headers under engine/src/string/include/pme/string/). The A2 brief names the functions and
// their signatures but not the header file names, so this shim includes whichever of the likely
// headers exists. If none declares the names below, fix the include list here and nowhere else:
//
//   enum class pme::string::WindowKind { Point, Rectangular, RaisedCosine };
//   double pme::string::windowFactor(WindowKind kind, double k, double b);
//   void   pme::string::windowedShapes(const pme::StringModes&, double x0, WindowKind kind,
//                                      double b, std::span<float> out);
#pragma once

#include <span>

#include "pme/string.hpp"

// NOLINTBEGIN(llvm-include-order)
#if defined(__has_include)
#if __has_include(<pme/string/window.hpp>)
#include <pme/string/window.hpp>
#endif
#if __has_include(<pme/string/windows.hpp>)
#include <pme/string/windows.hpp>
#endif
#if __has_include(<pme/string/shapes.hpp>)
#include <pme/string/shapes.hpp>
#endif
#if __has_include(<pme/string/string_compiler.hpp>)
#include <pme/string/string_compiler.hpp>
#endif
#if __has_include(<pme/string/compile_string.hpp>)
#include <pme/string/compile_string.hpp>
#endif
#if __has_include(<pme/string/stiff_string.hpp>)
#include <pme/string/stiff_string.hpp>
#endif
#if __has_include(<pme/string/string.hpp>)
#include <pme/string/string.hpp>
#endif
#if __has_include(<pme/string/string_modes.hpp>)
#include <pme/string/string_modes.hpp>
#endif
#if __has_include(<pme/string/helpers.hpp>)
#include <pme/string/helpers.hpp>
#endif
#endif
// NOLINTEND(llvm-include-order)
