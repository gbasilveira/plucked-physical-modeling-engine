// SPDX-License-Identifier: MIT
// P0.2-T1 and the P0.2-R5 controls: death tests for the RT-safety trap. Each aborting scenario
// runs in a child process (see main.cpp); the in-process cases avoid allocating inside a scope.
#include <string>

#include "child_modes.hpp"
#include "doctest/doctest.h"
#include "pme/rtcheck/rtcheck.hpp"
#include "process.hpp"

namespace {

using pme::rtcheck::test::ChildResult;
using pme::rtcheck::test::endedByAbort;
using pme::rtcheck::test::kChildSurvived;
using pme::rtcheck::test::runSelfAsChild;

constexpr const char* kDiagnostic = "pme_rtcheck: real-time violation: ";

bool survived(const ChildResult& r) {
    return r.launched && !r.signaled && r.exitCode == kChildSurvived;
}

bool trappedWith(const ChildResult& r, const std::string& what) {
    return endedByAbort(r) && r.standardError.find(kDiagnostic + what) != std::string::npos;
}

}  // namespace

TEST_CASE("P0.2-T1 allocation inside ScopedRealtimeThread aborts") {
    static_assert(pme::rtcheck::enabled(), "this executable is only built with PME_RTSAFETY");
    const ChildResult r = runSelfAsChild("alloc-in-scope");
    REQUIRE(r.launched);
    CHECK(endedByAbort(r));
    CHECK_MESSAGE(r.standardError.find(kDiagnostic) != std::string::npos, r.standardError);
    CHECK_MESSAGE(trappedWith(r, "operator new"), r.standardError);
}

TEST_CASE("P0.2-R5 allocation outside a scope does not abort") {
    CHECK(survived(runSelfAsChild("alloc-outside-scope")));
}

TEST_CASE("P0.2-R5 allocation after the scope ends does not abort") {
    CHECK(survived(runSelfAsChild("alloc-after-scope")));
}

TEST_CASE("P0.2-R5 a nested scope still traps after the inner scope ends") {
    const ChildResult r = runSelfAsChild("alloc-nested-scope");
    REQUIRE(r.launched);
    CHECK_MESSAGE(trappedWith(r, "operator new"), r.standardError);
}

TEST_CASE("P0.2-R5 scope nesting is tracked in process") {
    CHECK_FALSE(pme::rtcheck::isRealtimeThread());
    bool outer = false;
    bool inner = false;
    bool afterInner = false;
    {
        const pme::rtcheck::ScopedRealtimeThread a;
        outer = pme::rtcheck::isRealtimeThread();
        {
            const pme::rtcheck::ScopedRealtimeThread b;
            inner = pme::rtcheck::isRealtimeThread();
        }
        afterInner = pme::rtcheck::isRealtimeThread();
    }
    CHECK(outer);
    CHECK(inner);
    CHECK(afterInner);
    CHECK_FALSE(pme::rtcheck::isRealtimeThread());
}

TEST_CASE("P0.2-R5 blockingCall inside a scope aborts and outside it does not") {
    const ChildResult trapped = runSelfAsChild("blocking-call-in-scope");
    REQUIRE(trapped.launched);
    CHECK_MESSAGE(trappedWith(trapped, "test primitive"), trapped.standardError);
    CHECK(survived(runSelfAsChild("blocking-call-outside-scope")));
}

#if !defined(_WIN32) && !defined(__APPLE__)
// Linux: libc interposition. macOS has no libc interposition (docs/dev/rtcheck.md), so these
// scenarios are not exercised there.
TEST_CASE("P0.2-R5 pthread_mutex_lock inside a scope aborts and outside it does not") {
    const ChildResult trapped = runSelfAsChild("mutex-lock-in-scope");
    REQUIRE(trapped.launched);
    CHECK_MESSAGE(trappedWith(trapped, "pthread_mutex_lock"), trapped.standardError);
    CHECK(survived(runSelfAsChild("mutex-lock-outside-scope")));
}

TEST_CASE("P0.2-R5 sleeping inside a scope aborts") {
    const ChildResult usleepRun = runSelfAsChild("usleep-in-scope");
    CHECK_MESSAGE(trappedWith(usleepRun, "usleep"), usleepRun.standardError);
    const ChildResult nanosleepRun = runSelfAsChild("nanosleep-in-scope");
    CHECK_MESSAGE(trappedWith(nanosleepRun, "nanosleep"), nanosleepRun.standardError);
}

TEST_CASE("P0.2-R5 malloc inside a scope aborts") {
    const ChildResult r = runSelfAsChild("malloc-in-scope");
    CHECK_MESSAGE(trappedWith(r, "malloc"), r.standardError);
}
#endif
