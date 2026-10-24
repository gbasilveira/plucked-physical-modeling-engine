// SPDX-License-Identifier: MIT
// Scenarios run in the child process of the P0.2-T1 death tests.
#ifndef PME_TESTS_RTCHECK_CHILD_MODES_HPP
#define PME_TESTS_RTCHECK_CHILD_MODES_HPP

namespace pme::rtcheck::test {

/// Exit code of a child scenario that ran to the end without being trapped.
inline constexpr int kChildSurvived = 42;
/// Exit code for an unknown scenario name.
inline constexpr int kChildUnknownMode = 43;

/// Runs the named scenario and returns the process exit code (kChildSurvived unless it aborts).
int runChildMode(const char* mode);

}  // namespace pme::rtcheck::test

#endif  // PME_TESTS_RTCHECK_CHILD_MODES_HPP
