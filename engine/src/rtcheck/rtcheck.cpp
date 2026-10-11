// SPDX-License-Identifier: MIT
// RT-safety trap implementation (P0.2-R5). SPEC sections implemented: none (infrastructure).
//
// Everything that can trap lives in this one translation unit on purpose: the scope object's
// constructor is the only symbol a user has to reference, so its object file is always pulled out
// of the static library, and with it the replacement global operator new/delete and the libc
// interposers. Split them into other object files and the linker would silently drop them.
#include "pme/rtcheck/rtcheck.hpp"

namespace pme::rtcheck {
namespace {
// Nesting depth of ScopedRealtimeThread on this thread. Constant-initialised, so access needs no
// guard, no allocation and no lock (it is read from inside malloc).
constinit thread_local unsigned t_depth = 0;
}  // namespace

ScopedRealtimeThread::ScopedRealtimeThread() noexcept { ++t_depth; }
ScopedRealtimeThread::~ScopedRealtimeThread() { --t_depth; }
bool isRealtimeThread() noexcept { return t_depth != 0; }

#if !(defined(PME_RTSAFETY) && PME_RTSAFETY)
void blockingCall(const char* /*what*/) noexcept {}  // no-op outside the rt-safety build
#endif

}  // namespace pme::rtcheck

#if defined(PME_RTSAFETY) && PME_RTSAFETY

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <crtdbg.h>
#include <malloc.h>
// clang-format on
#else
#include <unistd.h>

#include <cerrno>
#endif

#if defined(__linux__) && defined(__GLIBC__)
#define PME_RTCHECK_INTERPOSE 1
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

#include <atomic>
#endif

namespace pme::rtcheck {
namespace {

// Fixed-buffer diagnostic. Must not allocate, lock or format through the C++ runtime.
void writeStderr(const char* text, std::size_t length) noexcept {
#if defined(_WIN32)
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(length), &written, nullptr);
#else
    while (length != 0) {
        const ssize_t n = ::write(2, text, length);
        if (n <= 0) {
            break;
        }
        text += n;
        length -= static_cast<std::size_t>(n);
    }
#endif
}

std::size_t cstrLength(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

[[noreturn]] void trap(const char* what) noexcept {
    static constexpr char kPrefix[] = "pme_rtcheck: real-time violation: ";
    static constexpr char kSuffix[] = " called on a real-time thread\n";
    char buffer[256];
    std::size_t used = 0;
    const auto append = [&](const char* s, std::size_t n) {
        for (std::size_t i = 0; i < n && used + 1 < sizeof buffer; ++i) {
            buffer[used++] = s[i];
        }
    };
    append(kPrefix, sizeof kPrefix - 1);
    append(what, cstrLength(what));
    append(kSuffix, sizeof kSuffix - 1);
    writeStderr(buffer, used);
#if defined(_WIN32)
    // Never let CI hang on an abort dialog or an error-reporting pop-up.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_WARN, 0);
#endif
    std::abort();
}

inline void check(const char* what) noexcept {
    if (t_depth != 0) {
        trap(what);
    }
}

// Allocation helpers for the global operator new/delete replacements.
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)
void* allocatePlain(std::size_t size) noexcept {
    check("operator new");
    return std::malloc(size != 0 ? size : 1);
}

void* allocateAligned(std::size_t size, std::size_t alignment) noexcept {
    check("operator new (aligned)");
    if (size == 0) {
        size = 1;
    }
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    void* p = nullptr;
    return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
#endif
}

void releasePlain(void* p) noexcept {
    if (p != nullptr) {
        check("operator delete");
        std::free(p);
    }
}

void releaseAligned(void* p) noexcept {
    if (p != nullptr) {
        check("operator delete (aligned)");
#if defined(_WIN32)
        _aligned_free(p);
#else
        std::free(p);
#endif
    }
}
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,hicpp-no-malloc)

void* allocateOrThrow(std::size_t size) {
    for (;;) {
        if (void* p = allocatePlain(size)) {
            return p;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

void* allocateAlignedOrThrow(std::size_t size, std::align_val_t alignment) {
    for (;;) {
        if (void* p = allocateAligned(size, static_cast<std::size_t>(alignment))) {
            return p;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

}  // namespace

void blockingCall(const char* what) noexcept { check(what); }

}  // namespace pme::rtcheck

// Replacement allocation functions, every overload ([new.delete]). The signatures must match the
// declarations in <new> exactly.
// NOLINTBEGIN(misc-new-delete-overloads,cert-dcl54-cpp,hicpp-new-delete-operators)
void* operator new(std::size_t size) { return pme::rtcheck::allocateOrThrow(size); }
void* operator new[](std::size_t size) { return pme::rtcheck::allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return pme::rtcheck::allocatePlain(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return pme::rtcheck::allocatePlain(size);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return pme::rtcheck::allocateAlignedOrThrow(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return pme::rtcheck::allocateAlignedOrThrow(size, alignment);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return pme::rtcheck::allocateAligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return pme::rtcheck::allocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* p) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete[](void* p) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete(void* p, std::size_t) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete[](void* p, std::size_t) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { pme::rtcheck::releasePlain(p); }
void operator delete(void* p, std::align_val_t) noexcept { pme::rtcheck::releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { pme::rtcheck::releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    pme::rtcheck::releaseAligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    pme::rtcheck::releaseAligned(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    pme::rtcheck::releaseAligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    pme::rtcheck::releaseAligned(p);
}
// NOLINTEND(misc-new-delete-overloads,cert-dcl54-cpp,hicpp-new-delete-operators)

#if defined(PME_RTCHECK_INTERPOSE)
// Linux (glibc) symbol interposition. These definitions live in the executable, which the dynamic
// linker searches before libc, so calls from the program and from shared libraries bind to them.
// The real allocator is reached through glibc's exported __libc_* entry points (dlsym would itself
// allocate); the blocking calls are forwarded to the next definition found with dlsym.
extern "C" {
void* __libc_malloc(std::size_t size);
void* __libc_calloc(std::size_t count, std::size_t size);
void* __libc_realloc(void* p, std::size_t size);
void __libc_free(void* p);
void* __libc_memalign(std::size_t alignment, std::size_t size);
}

namespace {
template <class Fn>
Fn resolveNext(std::atomic<Fn>& slot, const char* name) noexcept {
    Fn fn = slot.load(std::memory_order_relaxed);
    if (fn == nullptr) {
        // dlsym is idempotent; a racing thread stores the same pointer. A function-local static
        // with a guard is avoided on purpose: the guard takes a pthread mutex, interposed below.
        fn = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name));
        slot.store(fn, std::memory_order_relaxed);
    }
    return fn;
}
// One slot per forwarded function; constant-initialised, so no guard.
std::atomic<int (*)(pthread_mutex_t*)> g_mutexLock{nullptr};
std::atomic<int (*)(pthread_cond_t*, pthread_mutex_t*)> g_condWait{nullptr};
std::atomic<int (*)(pthread_cond_t*, pthread_mutex_t*, const struct timespec*)> g_condTimedWait{
    nullptr};
std::atomic<int (*)(pthread_t, void**)> g_join{nullptr};
std::atomic<int (*)(const struct timespec*, struct timespec*)> g_nanosleep{nullptr};
std::atomic<int (*)(clockid_t, int, const struct timespec*, struct timespec*)> g_clockNanosleep{
    nullptr};
std::atomic<int (*)(useconds_t)> g_usleep{nullptr};
std::atomic<unsigned int (*)(unsigned int)> g_sleep{nullptr};
}  // namespace

// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" {

void* malloc(std::size_t size) noexcept {
    pme::rtcheck::check("malloc");
    return __libc_malloc(size);
}
void* calloc(std::size_t count, std::size_t size) noexcept {
    pme::rtcheck::check("calloc");
    return __libc_calloc(count, size);
}
void* realloc(void* p, std::size_t size) noexcept {
    pme::rtcheck::check("realloc");
    return __libc_realloc(p, size);
}
void free(void* p) noexcept {
    if (p != nullptr) {
        pme::rtcheck::check("free");
        __libc_free(p);
    }
}
void* aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
    pme::rtcheck::check("aligned_alloc");
    return __libc_memalign(alignment, size);
}
int posix_memalign(void** out, std::size_t alignment, std::size_t size) noexcept {
    pme::rtcheck::check("posix_memalign");
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    void* p = __libc_memalign(alignment, size);
    if (p == nullptr) {
        return ENOMEM;
    }
    *out = p;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) noexcept {
    pme::rtcheck::check("pthread_mutex_lock");
    return resolveNext(g_mutexLock, "pthread_mutex_lock")(mutex);
}
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    pme::rtcheck::check("pthread_cond_wait");
    return resolveNext(g_condWait, "pthread_cond_wait")(cond, mutex);
}
int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* deadline) {
    pme::rtcheck::check("pthread_cond_timedwait");
    return resolveNext(g_condTimedWait, "pthread_cond_timedwait")(cond, mutex, deadline);
}
int pthread_join(pthread_t thread, void** result) {
    pme::rtcheck::check("pthread_join");
    return resolveNext(g_join, "pthread_join")(thread, result);
}
int nanosleep(const struct timespec* request, struct timespec* remaining) {
    pme::rtcheck::check("nanosleep");
    return resolveNext(g_nanosleep, "nanosleep")(request, remaining);
}
int clock_nanosleep(clockid_t clock, int flags, const struct timespec* request,
                    struct timespec* remaining) {
    pme::rtcheck::check("clock_nanosleep");
    return resolveNext(g_clockNanosleep, "clock_nanosleep")(clock, flags, request, remaining);
}
int usleep(useconds_t microseconds) {
    pme::rtcheck::check("usleep");
    return resolveNext(g_usleep, "usleep")(microseconds);
}
unsigned int sleep(unsigned int seconds) {
    pme::rtcheck::check("sleep");
    return resolveNext(g_sleep, "sleep")(seconds);
}

}  // extern "C"
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#endif  // PME_RTCHECK_INTERPOSE

#endif  // PME_RTSAFETY
