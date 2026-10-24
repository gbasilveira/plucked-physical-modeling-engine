// SPDX-License-Identifier: MIT
#pragma once

// Allocation counter backed by a test-local replacement of global operator new/delete
// (alloc_counter.cpp). Compiled only when PME_RTSAFETY is OFF, so that it never collides with the
// replacement installed by pme_rtcheck.

namespace pme_test {

/// Start counting allocations on all threads; resets the count to zero.
void startCountingAllocations() noexcept;
/// Stop counting and return the number of allocations since startCountingAllocations().
long stopCountingAllocations() noexcept;

}  // namespace pme_test
