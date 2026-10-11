// SPDX-License-Identifier: MIT
// RT-safety trap (P0.2-R5; Software guidelines, Real-time rules 1 and 6).
//
// SPEC sections implemented: none (infrastructure).
#ifndef PME_RTCHECK_RTCHECK_HPP
#define PME_RTCHECK_RTCHECK_HPP

namespace pme::rtcheck {

/// Marks the current thread real-time for the object's lifetime. Nestable.
///
/// In the rt-safety build (PME_RTSAFETY=1) any allocation or blocking call on a marked thread
/// prints a diagnostic to stderr and calls std::abort(). Otherwise it only keeps the nesting
/// count that isRealtimeThread() reads.
class ScopedRealtimeThread {
public:
    ScopedRealtimeThread() noexcept;
    ~ScopedRealtimeThread();
    ScopedRealtimeThread(const ScopedRealtimeThread&) = delete;
    ScopedRealtimeThread& operator=(const ScopedRealtimeThread&) = delete;
    ScopedRealtimeThread(ScopedRealtimeThread&&) = delete;
    ScopedRealtimeThread& operator=(ScopedRealtimeThread&&) = delete;
};

/// True inside a ScopedRealtimeThread scope on the calling thread.
[[nodiscard]] bool isRealtimeThread() noexcept;

/// True in the rt-safety build (PME_RTSAFETY=1), where the trap is active.
[[nodiscard]] constexpr bool enabled() noexcept {
#if defined(PME_RTSAFETY) && PME_RTSAFETY
    return true;
#else
    return false;
#endif
}

/// Called by the project's own blocking primitives (later phases) before they block. `what`
/// names the primitive in the diagnostic. Aborts on a marked thread in the rt-safety build.
void blockingCall(const char* what) noexcept;

}  // namespace pme::rtcheck

#endif  // PME_RTCHECK_RTCHECK_HPP
