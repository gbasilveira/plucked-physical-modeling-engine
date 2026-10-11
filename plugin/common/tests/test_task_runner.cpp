// SPDX-License-Identifier: MIT
// D1-R6: the task runner of ADR-0007 (shell side) with a mock host pool.
#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "pme/plugin/task_runner.hpp"

using pme::plugin::TaskRunner;

namespace {

struct Counts {
    std::array<int, 16> runs{};
    std::array<std::uint32_t, 16> order{};
    int next{0};
};

void task(void* context, std::uint32_t index) noexcept {
    auto* c = static_cast<Counts*>(context);
    ++c->runs[index];
    c->order[static_cast<std::size_t>(c->next++)] = index;
}

/// Mock host: runs the tasks through the runner's exec in reverse order, like a pool would.
struct MockPool final : TaskRunner::HostPool {
    explicit MockPool(TaskRunner& r) : runner(r) {}
    bool requestExec(std::uint32_t count) noexcept override {
        ++requests;
        if (reject) {
            return false;
        }
        for (std::uint32_t i = count; i > 0; --i) {
            runner.exec(i - 1);
        }
        return true;
    }
    TaskRunner& runner;
    int requests{0};
    bool reject{false};
};

}  // namespace

TEST_CASE("D1-R6 without a host pool the tasks run inline in order") {
    TaskRunner r;
    Counts c;
    r.run(5, task, &c);
    for (std::uint32_t i = 0; i < 5; ++i) {
        CHECK(c.runs[i] == 1);
        CHECK(c.order[i] == i);
    }
    CHECK(c.runs[5] == 0);
}

TEST_CASE("D1-R6 with a host pool each index runs exactly once through request_exec") {
    TaskRunner r;
    MockPool pool(r);
    r.setHostPool(&pool);
    CHECK(r.hasHostPool());
    Counts c;
    r.run(8, task, &c);
    CHECK(pool.requests == 1);
    for (std::uint32_t i = 0; i < 8; ++i) {
        CHECK(c.runs[i] == 1);
    }
    CHECK(c.runs[8] == 0);
}

TEST_CASE("D1-R6 a rejected request falls back to inline execution exactly once") {
    TaskRunner r;
    MockPool pool(r);
    pool.reject = true;
    r.setHostPool(&pool);
    Counts c;
    r.run(4, task, &c);
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(c.runs[i] == 1);
    }
}

TEST_CASE("D1-R6 exec outside run is ignored and zero tasks do nothing") {
    TaskRunner r;
    MockPool pool(r);
    r.setHostPool(&pool);
    Counts c;
    r.exec(0);
    r.run(0, task, &c);
    CHECK(pool.requests == 0);
    CHECK(c.runs[0] == 0);
}

TEST_CASE("D1-R6 default thread count is hardware threads minus one within 1 to 8") {
    const int n = TaskRunner::defaultThreads();
    CHECK(n >= 1);
    CHECK(n <= 8);
}
