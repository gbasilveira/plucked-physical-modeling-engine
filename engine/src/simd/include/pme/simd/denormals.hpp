// SPDX-License-Identifier: MIT
// Flush-to-zero guard for the audio thread (A8-R5; Real-time rule 5, SPEC-EXEC "Threading
// contract": denormals MUST be flushed to zero on every audio thread).
//
// SPEC sections implemented: SPEC-EXEC "Threading contract" (denormals).
#pragma once

namespace pme::simd {

/// RAII guard: while it lives, denormal floating-point inputs and results are flushed to zero on
/// the constructing thread. x86-64: MXCSR FTZ and DAZ bits. arm64: FPCR FZ bit. Other
/// architectures: no effect. The previous control state is restored on destruction. The guard
/// is a register read and write only: noexcept, no system call, no allocation. C1 constructs one
/// at `process` entry and on every worker thread. Must be destroyed on the thread that made it.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept;
    ~ScopedFlushDenormals();
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals(ScopedFlushDenormals&&) = delete;
    ScopedFlushDenormals& operator=(ScopedFlushDenormals&&) = delete;

private:
    unsigned long long saved_{0};
};

}  // namespace pme::simd
