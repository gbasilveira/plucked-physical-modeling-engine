// SPDX-License-Identifier: MIT
// Test-local replacement of global operator new/delete that counts allocations (P0.4-R3 (a)).
#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

// A counting replacement of the global allocation functions is global mutable state and uses malloc
// by design.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
namespace {
std::atomic<bool> g_counting{false};
std::atomic<long> g_count{0};

void* allocate(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (size == 0) {
        size = 1;
    }
    void* p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
}  // namespace

namespace pme_test {
void startCountingAllocations() noexcept {
    g_count.store(0);
    g_counting.store(true);
}
long stopCountingAllocations() noexcept {
    g_counting.store(false);
    return g_count.load();
}
}  // namespace pme_test

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
