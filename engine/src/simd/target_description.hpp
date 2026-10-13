// SPDX-License-Identifier: MIT
// What a compiled target reports about itself (dispatch.cpp). One type for every target, so that
// the dispatch table of Highway's HWY_EXPORT has a single function type.
#pragma once

namespace pme::simd {

struct TargetDescription {
    const char* name;
    bool fused;  ///< MulAdd is a fused instruction on this target.
};

}  // namespace pme::simd
