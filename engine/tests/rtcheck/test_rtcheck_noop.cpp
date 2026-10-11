// SPDX-License-Identifier: MIT
// Behaviour of pme_rtcheck outside the rt-safety build (P0.2-R5): the scope only counts, nothing
// aborts and no global function is replaced.
#include <memory>
#include <vector>

#include "doctest/doctest.h"
#include "pme/rtcheck/rtcheck.hpp"

namespace {
// The volatile store keeps the allocation observable. A test scratch variable, not engine state.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
int* volatile g_sink = nullptr;
}  // namespace

TEST_CASE("P0.2-R5 the trap is disabled outside the rt-safety build") {
    CHECK_FALSE(pme::rtcheck::enabled());
}

TEST_CASE("P0.2-R5 scope nesting is tracked without the trap") {
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

TEST_CASE("P0.2-R5 allocation and blockingCall inside a scope do not abort without the trap") {
    const pme::rtcheck::ScopedRealtimeThread scope;
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): the point is a raw new/delete in a scope.
    g_sink = new int(1);
    delete g_sink;
    // NOLINTEND(cppcoreguidelines-owning-memory)
    g_sink = nullptr;
    pme::rtcheck::blockingCall("test primitive");
    const std::vector<int> v(16, 1);
    CHECK(v.size() == 16);
}
