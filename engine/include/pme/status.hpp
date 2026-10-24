// SPDX-License-Identifier: MIT
#pragma once

/// @file status.hpp
/// Failure channel of every non-real-time function (ADR-0006).

#include <cstdint>

namespace pme {

/// Result of a non-real-time function (ADR-0006). On any value other than `Ok` the function's
/// out-parameter is left unchanged. New codes are added only by ICR.
enum class Status : std::uint8_t {
    Ok,               ///< The operation succeeded; the out-parameter holds the result.
    NotImplemented,   ///< The function is a P0.4 stub; its own phase implements it.
    InvalidArgument,  ///< An argument violates its documented precondition.
    Rejected,         ///< A SPEC rule refused the input (for example SPEC-OSC bandwidth rule).
};

/// Name of a status code, for logs and test output (ADR-0006).
/// Thread: any. Allocates: no. Never throws; returns a pointer to static storage. An
/// out-of-range value yields "Unknown".
[[nodiscard]] const char* toString(Status s) noexcept;

}  // namespace pme
