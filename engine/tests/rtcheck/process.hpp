// SPDX-License-Identifier: MIT
// Minimal child-process helper for the death tests: no shell, stderr captured.
#ifndef PME_TESTS_RTCHECK_PROCESS_HPP
#define PME_TESTS_RTCHECK_PROCESS_HPP

#include <string>

namespace pme::rtcheck::test {

struct ChildResult {
    bool launched = false;   // the child process was started and waited for
    bool signaled = false;   // POSIX: terminated by a signal
    int signalNumber = 0;    // POSIX: that signal
    long exitCode = 0;       // exit status (POSIX: when not signaled; Windows: process exit code)
    std::string standardError;
};

/// Re-launches this executable with --pme-rtcheck-child=<mode> and waits for it.
ChildResult runSelfAsChild(const std::string& mode);

/// True when the child ended abnormally the way std::abort() ends a process on this system:
/// SIGABRT on POSIX; exit code 3 (or the fast-fail status) on Windows.
bool endedByAbort(const ChildResult& result);

}  // namespace pme::rtcheck::test

#endif  // PME_TESTS_RTCHECK_PROCESS_HPP
