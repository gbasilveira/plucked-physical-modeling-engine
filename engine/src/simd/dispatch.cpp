// SPDX-License-Identifier: MIT
// Runtime dispatch control (A8-R5, ADR-0001): `initialise` resolves Highway's dynamic dispatch off
// the audio thread; the test hook restricts it to one supported target at a time.
//
// SPEC sections implemented: PLAN-COMP "SIMD" (runtime dispatch); Real-time rules (no CPU
// detection inside `process`).
#include "highway_config.hpp"  // first: defines HWY_DISABLED_TARGETS

#include "pme/simd/dispatch.hpp"
#include "target_description.hpp"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "../dispatch.cpp"  // relative to engine/src/simd/include
#include "hwy/foreach_target.h"  // IWYU pragma: keep; must come before highway.h
#include "hwy/highway.h"
#include "block-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace pme::simd::HWY_NAMESPACE {

TargetDescription Describe() { return TargetDescription{hwy::TargetName(HWY_TARGET), kFusedMulAdd}; }

}  // namespace pme::simd::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
#include <array>

namespace pme::simd {

HWY_EXPORT(Describe);

namespace {

constexpr std::size_t kMaxTargets = 64;

struct TargetList {
    std::array<std::int64_t, kMaxTargets> bits{};
    std::size_t count{0};
};

// Targets that were compiled in and that the CPU supports, best first. Built once, before any
// test-hook restriction, by the first call (a non-audio thread, via initialise()).
const TargetList& supportedTargets() {
    static const TargetList list = [] {
        TargetList l;
        // Highway numbers its targets so that the lowest set bit is the best one.
        std::size_t n = 0;
        for (std::int64_t targets = hwy::SupportedTargets() & HWY_TARGETS;
             targets != 0 && n < kMaxTargets; targets = targets & (targets - 1)) {
            l.bits[n++] = targets & ~(targets - 1);
        }
        l.count = n;
        return l;
    }();
    return list;
}

}  // namespace

const char* initialise() noexcept {
    static_cast<void>(supportedTargets());
    return HWY_DYNAMIC_DISPATCH(Describe)().name;
}

bool activeTargetIsFused() noexcept { return HWY_DYNAMIC_DISPATCH(Describe)().fused; }

const char* activeTargetName() noexcept { return HWY_DYNAMIC_DISPATCH(Describe)().name; }

std::size_t testTargetCount() noexcept { return supportedTargets().count; }

const char* testTargetName(std::size_t index) noexcept {
    const TargetList& l = supportedTargets();
    return index < l.count ? hwy::TargetName(l.bits[index]) : "";
}

bool testSelectTarget(std::size_t index) noexcept {
    const TargetList& l = supportedTargets();
    if (index >= l.count) {
        return false;
    }
    hwy::SetSupportedTargetsForTest(l.bits[index]);
    return true;
}

void testResetTarget() noexcept { hwy::SetSupportedTargetsForTest(0); }

}  // namespace pme::simd
#endif  // HWY_ONCE
