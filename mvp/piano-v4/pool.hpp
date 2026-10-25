// SPDX-License-Identifier: MIT
// 0.5.4 real-time worker pool of the piano MVP.
//
// Rules: threads are created in start() (non-audio) and joined in stop(); the audio thread never
// allocates, locks, makes a system call or waits on a mutex/condition variable. A phase is a set of
// independent tasks (fn(ctx, task)); every task writes only its own outputs, so the results do not
// depend on which thread ran a task, and the caller combines them in a fixed order afterwards:
// the output is bit-identical for any number of workers.
//
// Protocol. The audio thread publishes a phase (function, context, owner of each task, sequence
// number with release order) and runs its own tasks; each hot worker runs the tasks it owns. Tasks are
// claimed with a compare-and-swap on a per-task sequence stamp, so a task runs exactly once. The audio
// thread steals unclaimed tasks after a short delay (a preempted or late worker costs its unstarted
// tasks, not a deadline) and then waits, spinning, for the tasks that are already running. Workers spin
// while work is recent (kSpinNs after the last phase), then fall back to short sleeps; a sleeping
// worker is "cold", and the audio thread assigns everything to itself until the worker is hot again
// (it never waits for a cold worker). The only remaining wait is for a task a worker has already
// started: that task is a few microseconds long.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>
#if defined(__linux__) || defined(__APPLE__)
#  include <pthread.h>
#  include <sched.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#  include <xmmintrin.h>
#  if defined(__GNUC__)
#    include <x86intrin.h>
#  endif
#endif

namespace piano_mvp_v4 {
class WorkerPool {
public:
    static constexpr int kMaxWorkers = 7;
    static constexpr int kMaxTasks = 96;
    using Fn = void (*)(void* ctx, int task);
    WorkerPool() = default;
    ~WorkerPool() { stop(); }
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Non-audio. Creates `workers` threads (0..kMaxWorkers). Returns false if a thread cannot start.
    bool start(int workers) {
        stop();
        if (workers < 0 || workers > kMaxWorkers) return false;
        quit_.store(false);
        seq_.store(0);
        for (auto& c : claim_) c.store(0);
        for (auto& f : fin_) f.store(0);
        for (auto& b : banUntil_) b = 0;
        for (auto& k : strikes_) k = 0;
        active_.store(0);
        for (int w = 0; w < workers; ++w) state_[w].ready.store(false);
        try {
            for (int w = 0; w < workers; ++w) threads_.emplace_back([this, w] { workerMain(w); });
        } catch (...) { stop(); return false; }
        return true;
    }
    // Non-audio. Joins the threads.
    void stop() noexcept {
        quit_.store(true);
        for (auto& t : threads_) if (t.joinable()) t.join();
        threads_.clear();
        active_.store(0);
    }
    int workers() const noexcept { return static_cast<int>(threads_.size()); }
    // Audio thread (or before playback): workers that take part, 0..workers().
    void setActive(int n) noexcept {
        if (n < 0) n = 0;
        if (n > workers()) n = workers();
        active_.store(n, std::memory_order_relaxed);
    }
    int active() const noexcept { return active_.load(std::memory_order_relaxed); }
    // Slots are 0 (the audio thread) .. active(); a task's owner is clamped into that range.
    // Audio thread only. Returns when every task has run.
    void run(Fn fn, void* ctx, int count, const std::uint8_t* owner) noexcept {
        const int act = active();
        if (act == 0 || count < 2 || count > kMaxTasks) {
            for (int i = 0; i < count; ++i) fn(ctx, i);
            return;
        }
        // A worker that is not spinning does not get tasks (the audio thread never waits for it).
        const std::uint32_t s = seq_.load(std::memory_order_relaxed) + 1;
#if defined(__linux__) || defined(__APPLE__)
        // Tell the workers which thread is the audio thread (a read of a register, not a system call).
        const auto self = static_cast<std::uintptr_t>(pthread_self());
        if (audioThread_.load(std::memory_order_relaxed) != self) audioThread_.store(self, std::memory_order_relaxed);
#endif
        std::uint32_t hotMask = 1;
        for (int w = 0; w < act; ++w)
            if (state_[w].ready.load(std::memory_order_relaxed) && s >= banUntil_[w]) hotMask |= 1u << (w + 1);
        Desc& d = desc_[s & 3u];
        d.seq.store(0, std::memory_order_relaxed);   // invalidate while rewriting (seqlock)
        d.fn.store(fn, std::memory_order_relaxed); d.ctx.store(ctx, std::memory_order_relaxed);
        d.count.store(count, std::memory_order_relaxed); d.act.store(act, std::memory_order_relaxed);
        d.csr.store(readCsr(), std::memory_order_relaxed);
        for (int i = 0; i < count; ++i) {
            int o = owner ? owner[i] : 0;
            if (o > act || !(hotMask & (1u << o))) o = 0;
            owner_[i].store(static_cast<std::uint8_t>(o), std::memory_order_relaxed);
        }
        done_.store(0, std::memory_order_relaxed);
        d.seq.store(s, std::memory_order_release);
        seq_.store(s, std::memory_order_release);
        const std::uint64_t t0 = ticks();
        for (int i = 0; i < count; ++i)
            if (owner_[i].load(std::memory_order_relaxed) == 0) tryRun(i, s, fn, ctx, 0);
        bool stallChecked = false;
        while (done_.load(std::memory_order_acquire) < count) {
            const std::uint64_t waited = ticks() - t0;
            if (waited > kStealTicks)
                for (int i = 0; i < count; ++i) {
                    if (claim_[i].load(std::memory_order_relaxed) >= s) continue;
                    const int o = owner_[i].load(std::memory_order_relaxed);
                    // Take a task from a worker that has not started anything yet (late), or any
                    // unclaimed task once the phase has run much longer than a normal one.
                    const bool late = o > 0 && started_[o].n.load(std::memory_order_relaxed) != s;
                    if (!late && waited < kBalanceTicks) continue;
                    if (tryRun(i, s, fn, ctx, 0)) {
                        ++stolen_;
                        if (late && ++strikes_[o - 1] >= 3) ban(o - 1, s, kBanSteal);
                    }
                    if (done_.load(std::memory_order_acquire) >= count) break;
                }
            if (!stallChecked && waited > kStallTicks && s > 20000) {
                stallChecked = true;   // a started task is taking long: its thread was probably preempted
                for (int i = 0; i < count; ++i)
                    if (claim_[i].load(std::memory_order_acquire) == s && fin_[i].load(std::memory_order_acquire) != s) {
                        const int by = by_[i].load(std::memory_order_relaxed);
                        if (by > 0) { ban(by - 1, s, kBanStall); ++stalls_; }
                    }
            }
            pause();
        }
        if ((s & 1023u) == 0) for (auto& k : strikes_) if (k > 0) --k;
    }
    std::uint64_t stolenTasks() const noexcept { return stolen_; }
    std::uint64_t stalls() const noexcept { return stalls_; }
    // Tasks run by slot (0 = audio thread), for statistics.
    std::uint64_t tasksRunBy(int slot) const noexcept { return taskCount_[slot].n.load(std::memory_order_relaxed); }

private:
    static constexpr std::uint64_t kStealTicks = 10000;    // ~2.8 us at 3.6 GHz: a worker that has not started by then is late
    static constexpr std::uint64_t kBalanceTicks = 72000;  // ~20 us: steal anything unclaimed
    static constexpr std::uint64_t kStallTicks = 540000;   // ~150 us
    static constexpr std::uint32_t kBanSteal = 1u << 16, kBanStall = 1u << 17;   // phases (~0.7 s, ~1.4 s)
    void ban(int w, std::uint32_t s, std::uint32_t phases) noexcept { banUntil_[w] = s + phases; strikes_[w] = 0; }
    static constexpr std::int64_t kSpinNs = 4'000'000;     // stay hot 4 ms after the last phase
    static void pause() noexcept {
#if defined(__x86_64__) || defined(__i386__)
        _mm_pause();
#else
        std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
    }
    static std::uint64_t ticks() noexcept {
#if defined(__x86_64__) && defined(__GNUC__)
        return __rdtsc();
#else
        return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count() / 4);
#endif
    }
    static unsigned readCsr() noexcept {
#if defined(__x86_64__) || defined(__i386__)
        return _mm_getcsr();
#else
        return 0;
#endif
    }
    static void writeCsr(unsigned v) noexcept {
#if defined(__x86_64__) || defined(__i386__)
        if (_mm_getcsr() != v) _mm_setcsr(v);
#else
        (void)v;
#endif
    }
    // Claim task i of phase s (stamp s) and run it. A task is claimed at most once per phase; a claim
    // that succeeds proves the phase is still incomplete, so its descriptor cannot have been reused.
    bool tryRun(int i, std::uint32_t s, Fn fn, void* ctx, int slot) noexcept {
        std::uint32_t seen = claim_[i].load(std::memory_order_relaxed);
        if (seen >= s) return false;
        if (!claim_[i].compare_exchange_strong(seen, s, std::memory_order_acq_rel, std::memory_order_relaxed))
            return false;
        by_[i].store(static_cast<std::uint8_t>(slot), std::memory_order_relaxed);
        taskCount_[slot].n.fetch_add(1, std::memory_order_relaxed);
        started_[slot].n.store(s, std::memory_order_relaxed);
        fn(ctx, i);
        fin_[i].store(s, std::memory_order_release);
        done_.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }
    // Workers copy the real-time scheduling class of the audio thread (one lower priority) so that a busy
    // normal-priority process cannot preempt a worker in the middle of a task. Runs on the worker thread,
    // never on the audio thread; silently does nothing if the system refuses.
    void mirrorPriority(std::uintptr_t& mirrored) noexcept {
#if defined(__linux__) || defined(__APPLE__)
        const std::uintptr_t a = audioThread_.load(std::memory_order_relaxed);
        if (a == 0 || a == mirrored) return;
        mirrored = a;
        int policy = 0; sched_param param{};
        if (pthread_getschedparam(static_cast<pthread_t>(a), &policy, &param) != 0) return;
        if (policy != SCHED_FIFO && policy != SCHED_RR) return;
        param.sched_priority = param.sched_priority > 1 ? param.sched_priority - 1 : 1;
        (void)pthread_setschedparam(pthread_self(), policy, &param);
#else
        (void)mirrored;
#endif
    }
    void workerMain(int w) noexcept {
        std::uintptr_t mirrored = 0;
        std::uint32_t last = seq_.load(std::memory_order_acquire);
        auto idleSince = std::chrono::steady_clock::now();
        unsigned spins = 0;
        bool sleeping = false, ready = false;
        const auto setReady = [&](bool r) {
            if (r != ready) { ready = r; state_[w].ready.store(r, std::memory_order_relaxed); }
        };
        while (!quit_.load(std::memory_order_relaxed)) {
            const std::uint32_t s = seq_.load(std::memory_order_acquire);
            if (s != last) {
                last = s; spins = 0; sleeping = false;
                setReady(true);
                const Desc& d = desc_[s & 3u];
                if (d.seq.load(std::memory_order_acquire) == s) {
                    const Fn fn = d.fn.load(std::memory_order_relaxed);
                    void* const ctx = d.ctx.load(std::memory_order_relaxed);
                    const int count = d.count.load(std::memory_order_relaxed);
                    const int act = d.act.load(std::memory_order_relaxed);
                    const unsigned csr = d.csr.load(std::memory_order_relaxed);
                    std::atomic_thread_fence(std::memory_order_acquire);
                    // Seqlock re-check: the copies above are valid only if the descriptor was not rewritten.
                    if (d.seq.load(std::memory_order_relaxed) == s && w < act && count <= kMaxTasks) {
                        writeCsr(csr);   // same FTZ/DAZ state as the audio thread
                        const int slot = w + 1;
                        for (int i = 0; i < count; ++i)
                            if (owner_[i].load(std::memory_order_relaxed) == slot) tryRun(i, s, fn, ctx, slot);
                    }
                }
                idleSince = std::chrono::steady_clock::now();
                continue;
            }
            if (!sleeping) {
                setReady(true);
                if ((spins & 1023u) == 0) mirrorPriority(mirrored);
                pause();
                if ((++spins & 255u) == 0 &&
                    std::chrono::steady_clock::now() - idleSince > std::chrono::nanoseconds(kSpinNs)) {
                    sleeping = true;
                    setReady(false);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    struct Desc {
        std::atomic<std::uint32_t> seq{0};
        std::atomic<Fn> fn{nullptr};
        std::atomic<void*> ctx{nullptr};
        std::atomic<int> count{0}, act{0};
        std::atomic<unsigned> csr{0};
    };
    struct alignas(64) Counter { std::atomic<std::uint64_t> n{0}; };
    Counter taskCount_[kMaxWorkers + 1], started_[kMaxWorkers + 1];
    struct alignas(64) WorkerState { std::atomic<bool> ready{false}; };
    alignas(64) std::atomic<std::uint32_t> seq_{0};
    alignas(64) std::atomic<int> done_{0};
    alignas(64) std::atomic<bool> quit_{false};
    std::atomic<int> active_{0};
    std::atomic<std::uintptr_t> audioThread_{0};
    std::atomic<std::uint32_t> claim_[kMaxTasks]{};
    std::atomic<std::uint8_t> owner_[kMaxTasks]{}, by_[kMaxTasks]{};
    std::atomic<std::uint32_t> fin_[kMaxTasks]{};
    std::uint32_t banUntil_[kMaxWorkers]{};   // audio-thread private
    int strikes_[kMaxWorkers]{};
    std::uint64_t stalls_{0};
    WorkerState state_[kMaxWorkers];
    Desc desc_[4];
    std::uint64_t stolen_{0};
    std::vector<std::thread> threads_;
};

// One helper thread that repeatedly calls `fn(ctx)` (returns true if it did work). Spins while work is
// recent, then sleeps in 100 us steps (a sleeping thread costs the audio thread nothing: it does the
// work itself at the end of the block). The thread mirrors the audio thread's real-time class and its
// FTZ/DAZ state.
class PipelineThread {
public:
    using Fn = bool (*)(void* ctx);
    ~PipelineThread() { stop(); }
    bool start(Fn fn, void* ctx) {
        stop();
        quit_.store(false);
        try { thread_ = std::thread([this, fn, ctx] { main(fn, ctx); }); }
        catch (...) { return false; }
        running_ = true;
        return true;
    }
    void stop() noexcept {
        quit_.store(true);
        if (thread_.joinable()) thread_.join();
        running_ = false;
    }
    bool running() const noexcept { return running_; }
    // Audio thread: tell the helper which thread is the audio thread and its floating-point control word.
    void announce() noexcept {
#if defined(__linux__) || defined(__APPLE__)
        const auto self = static_cast<std::uintptr_t>(pthread_self());
        if (audioThread_.load(std::memory_order_relaxed) != self) audioThread_.store(self, std::memory_order_relaxed);
#endif
#if defined(__x86_64__) || defined(__i386__)
        const unsigned csr = _mm_getcsr();
        if (csr_.load(std::memory_order_relaxed) != csr) csr_.store(csr, std::memory_order_relaxed);
#endif
    }
private:
    void mirror(std::uintptr_t& mirrored, int& policyDone, int& prioDone) noexcept {
#if defined(__linux__) || defined(__APPLE__)
        const std::uintptr_t a = audioThread_.load(std::memory_order_relaxed);
        if (a == 0) return;
        int policy = 0; sched_param param{};
        if (pthread_getschedparam(static_cast<pthread_t>(a), &policy, &param) != 0) return;
        // Re-checked periodically: the audio thread may become real-time after it was first announced.
        if (a == mirrored && policy == policyDone && param.sched_priority == prioDone) return;
        mirrored = a; policyDone = policy; prioDone = param.sched_priority;
        if (policy != SCHED_FIFO && policy != SCHED_RR) return;
        param.sched_priority = param.sched_priority > 1 ? param.sched_priority - 1 : 1;
        (void)pthread_setschedparam(pthread_self(), policy, &param);
#else
        (void)mirrored; (void)policyDone; (void)prioDone;
#endif
    }
    void main(Fn fn, void* ctx) noexcept {
        std::uintptr_t mirrored = 0; int policyDone = -1, prioDone = -1;
        auto idleSince = std::chrono::steady_clock::now();
        unsigned spins = 0, sleepTicks = 0;
        bool sleeping = false;
        while (!quit_.load(std::memory_order_relaxed)) {
#if defined(__x86_64__) || defined(__i386__)
            const unsigned want = csr_.load(std::memory_order_relaxed);
            if (want != 0 && _mm_getcsr() != want) _mm_setcsr(want);
#endif
            if (fn(ctx)) { idleSince = std::chrono::steady_clock::now(); spins = 0; sleeping = false; continue; }
            if (!sleeping) {
                if ((spins & 8191u) == 0) mirror(mirrored, policyDone, prioDone);
#if defined(__x86_64__) || defined(__i386__)
                _mm_pause();
#endif
                if ((++spins & 255u) == 0 &&
                    std::chrono::steady_clock::now() - idleSince > std::chrono::nanoseconds(300'000))
                    sleeping = true;
            } else {
                if ((++sleepTicks & 63u) == 0) mirror(mirrored, policyDone, prioDone);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    std::atomic<bool> quit_{false};
    std::atomic<std::uintptr_t> audioThread_{0};
    std::atomic<unsigned> csr_{0};
    bool running_{false};
    std::thread thread_;
};
}
