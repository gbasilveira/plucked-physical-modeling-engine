// SPDX-License-Identifier: MIT
#pragma once

/// @file task_runner.hpp
/// Task runner of ADR-0007 (D1-R6, shell side). Contract for C5-R5 (the engine-side adapter):
///
///  - `run(count, fn, context)` is called on the audio thread only, never concurrently and never
///    from a pool task. It returns when all `count` tasks have finished.
///  - With a host pool, the runner calls `HostPool::requestExec(count)`; the host then calls
///    `exec(index)` once per index in [0, count) from its pool threads (the audio thread may take
///    part). Without a host pool, or if the host rejects the request, `run` executes the indices
///    0..count-1 in order on the calling thread.
///  - Every index runs exactly once. `fn` must be real-time safe and must not throw.
///  - `run` allocates nothing, takes no lock and makes no system call of its own.

#include <atomic>
#include <cstdint>

namespace pme::plugin {

class TaskRunner {
public:
    /// The host's thread pool (CLAP `clap.thread-pool`) as seen by the runner.
    class HostPool {
    public:
        HostPool() = default;
        virtual ~HostPool() = default;
        HostPool(const HostPool&) = delete;
        HostPool& operator=(const HostPool&) = delete;
        HostPool(HostPool&&) = delete;
        HostPool& operator=(HostPool&&) = delete;

        /// Audio thread. Blocks until the host has run all tasks; false if it rejected them.
        virtual bool requestExec(std::uint32_t taskCount) noexcept = 0;
    };

    /// A task body: `context` is the pointer given to `run`.
    using TaskFn = void (*)(void* context, std::uint32_t index) noexcept;

    /// Main thread, while the plug-in is inactive. nullptr means "the host has no pool".
    void setHostPool(HostPool* pool) noexcept { pool_ = pool; }
    [[nodiscard]] bool hasHostPool() const noexcept { return pool_ != nullptr; }

    /// Audio thread. See the contract above.
    void run(std::uint32_t taskCount, TaskFn fn, void* context) noexcept;

    /// Pool threads (and the audio thread inside `requestExec`): run task `index`. Ignored when no
    /// `run` is in progress.
    void exec(std::uint32_t index) noexcept;

    /// Default `EngineConfig::threads` when the host has no pool: hardware threads minus one,
    /// at least 1, at most 8 (D1-R6).
    [[nodiscard]] static int defaultThreads() noexcept;

private:
    struct Job {
        TaskFn fn;
        void* context;
    };
    HostPool* pool_{nullptr};
    std::atomic<const Job*> job_{nullptr};
};

}  // namespace pme::plugin
