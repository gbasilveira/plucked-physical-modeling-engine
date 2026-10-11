// SPDX-License-Identifier: MIT
// ADR-0007 (shell side of the host-pool split); the engine-side adapter is C5-R5, routing is D6.
#include "pme/plugin/task_runner.hpp"

#include <algorithm>
#include <thread>

namespace pme::plugin {

void TaskRunner::run(std::uint32_t taskCount, TaskFn fn, void* context) noexcept {
    if (taskCount == 0 || fn == nullptr) {
        return;
    }
    if (pool_ != nullptr) {
        const Job job{fn, context};
        job_.store(&job, std::memory_order_release);
        const bool done = pool_->requestExec(taskCount);
        job_.store(nullptr, std::memory_order_release);
        if (done) {
            return;
        }
    }
    for (std::uint32_t i = 0; i < taskCount; ++i) {
        fn(context, i);
    }
}

void TaskRunner::exec(std::uint32_t index) noexcept {
    const Job* job = job_.load(std::memory_order_acquire);
    if (job != nullptr) {
        job->fn(job->context, index);
    }
}

int TaskRunner::defaultThreads() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    const int n = hw > 1 ? static_cast<int>(hw) - 1 : 1;
    return std::clamp(n, 1, 8);
}

}  // namespace pme::plugin
