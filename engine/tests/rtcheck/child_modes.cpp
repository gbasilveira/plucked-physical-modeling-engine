// SPDX-License-Identifier: MIT
#include "child_modes.hpp"

#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

#include "pme/rtcheck/rtcheck.hpp"

namespace pme::rtcheck::test {
namespace {

// The observable store keeps the compiler from removing the allocation.
int* volatile g_sink = nullptr;
void* volatile g_rawSink = nullptr;

void allocate() {
    g_sink = new int(7);
    delete g_sink;
    g_sink = nullptr;
}

}  // namespace

int runChildMode(const char* mode) {
    const auto is = [mode](const char* name) { return std::strcmp(mode, name) == 0; };

    if (is("alloc-in-scope")) {
        const ScopedRealtimeThread scope;
        allocate();
    } else if (is("alloc-outside-scope")) {
        allocate();
    } else if (is("alloc-after-scope")) {
        {
            const ScopedRealtimeThread scope;
        }
        allocate();
    } else if (is("alloc-nested-scope")) {
        const ScopedRealtimeThread outer;
        {
            const ScopedRealtimeThread inner;
        }
        allocate();  // the outer scope is still active
    } else if (is("blocking-call-in-scope")) {
        const ScopedRealtimeThread scope;
        blockingCall("test primitive");
    } else if (is("blocking-call-outside-scope")) {
        blockingCall("test primitive");
#if !defined(_WIN32)
    } else if (is("mutex-lock-in-scope")) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        const ScopedRealtimeThread scope;
        pthread_mutex_lock(&mutex);
    } else if (is("mutex-lock-outside-scope")) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
    } else if (is("usleep-in-scope")) {
        const ScopedRealtimeThread scope;
        usleep(1);
    } else if (is("nanosleep-in-scope")) {
        const ScopedRealtimeThread scope;
        const timespec request{0, 1000};
        nanosleep(&request, nullptr);
    } else if (is("malloc-in-scope")) {
        const ScopedRealtimeThread scope;
        g_rawSink = std::malloc(16);
        std::free(g_rawSink);
#endif
    } else {
        return kChildUnknownMode;
    }
    return kChildSurvived;
}

}  // namespace pme::rtcheck::test
