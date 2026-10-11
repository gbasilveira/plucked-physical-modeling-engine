// SPDX-License-Identifier: MIT
#pragma once

/// @file spsc_queue.hpp
/// Preallocated lock-free single-producer single-consumer queue (Real-time rule 3). The producer
/// is a main thread, the consumer the audio thread. Never allocates after construction.

#include <array>
#include <atomic>
#include <cstddef>

namespace pme::plugin {

template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity: power of two");

public:
    /// Producer thread. Returns false (and drops `value`) when the queue is full.
    bool push(const T& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer thread. Returns false when empty.
    bool pop(T& value) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        value = slots_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    /// Only while neither thread uses the queue (for example while the plug-in is inactive).
    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<T, Capacity> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

}  // namespace pme::plugin
