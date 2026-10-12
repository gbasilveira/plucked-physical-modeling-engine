// SPDX-License-Identifier: MIT
// ScopedFlushDenormals (A8-R5): flush-to-zero and denormals-are-zero on the calling thread.
//
// SPEC sections implemented: SPEC-EXEC "Threading contract" (denormals MUST be flushed to zero on
// every audio thread); Real-time rule 5.
//
// x86-64: MXCSR bit 15 (FTZ, results) and bit 6 (DAZ, inputs). arm64: FPCR bit 24 (FZ, both).
// Both are register accesses; there is no system call. No Highway header here.
#include "pme/simd/denormals.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define PME_SIMD_DENORMALS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define PME_SIMD_DENORMALS_ARM64 1
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#endif

namespace pme::simd {

namespace {

#if defined(PME_SIMD_DENORMALS_X86)
constexpr unsigned kFtzDaz = 0x8040U;  // FTZ (bit 15) | DAZ (bit 6)
#elif defined(PME_SIMD_DENORMALS_ARM64)
constexpr unsigned long long kFz = 1ULL << 24U;  // FPCR.FZ

unsigned long long readFpcr() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return static_cast<unsigned long long>(_ReadStatusReg(0x5A20));  // ARM64_FPCR
#else
    unsigned long long v = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v));
    return v;
#endif
}

void writeFpcr(unsigned long long v) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    _WriteStatusReg(0x5A20, static_cast<__int64>(v));  // ARM64_FPCR
#else
    __asm__ volatile("msr fpcr, %0" : : "r"(v));
#endif
}
#endif

}  // namespace

ScopedFlushDenormals::ScopedFlushDenormals() noexcept {
#if defined(PME_SIMD_DENORMALS_X86)
    const unsigned csr = _mm_getcsr();
    saved_ = csr;
    _mm_setcsr(csr | kFtzDaz);
#elif defined(PME_SIMD_DENORMALS_ARM64)
    const unsigned long long fpcr = readFpcr();
    saved_ = fpcr;
    writeFpcr(fpcr | kFz);
#endif
}

ScopedFlushDenormals::~ScopedFlushDenormals() {
#if defined(PME_SIMD_DENORMALS_X86)
    _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(PME_SIMD_DENORMALS_ARM64)
    writeFpcr(saved_);
#endif
}

}  // namespace pme::simd
