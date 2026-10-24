// SPDX-License-Identifier: MIT
#include "process.hpp"

#include <array>
#include <csignal>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;  // NOLINT(readability-redundant-declaration)
#endif

namespace pme::rtcheck::test {

#if defined(_WIN32)

namespace {
constexpr DWORD kStatusFatalAppExit = 0xC0000409UL;  // fast-fail, how some CRTs report abort()
}

ChildResult runSelfAsChild(const std::string& mode) {
    ChildResult result;

    std::wstring exe(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (length == 0 || length >= exe.size()) {
        return result;
    }
    exe.resize(length);
    std::wstring commandLine = L"\"" + exe + L"\" --pme-rtcheck-child=";
    for (const char c : mode) {  // scenario names are ASCII
        commandLine.push_back(static_cast<wchar_t>(c));
    }

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof inherit;
    inherit.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    CloseHandle(writeEnd);
    if (!created) {
        CloseHandle(readEnd);
        return result;
    }

    std::array<char, 1024> buffer{};
    DWORD got = 0;
    while (ReadFile(readEnd, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) &&
           got != 0) {
        result.standardError.append(buffer.data(), got);
    }
    CloseHandle(readEnd);

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    result.launched = true;
    result.exitCode = static_cast<long>(code);
    return result;
}

bool endedByAbort(const ChildResult& result) {
    return result.launched &&
           (result.exitCode == 3 || static_cast<DWORD>(result.exitCode) == kStatusFatalAppExit);
}

#else  // POSIX

namespace {
std::string executablePath() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::string(buffer.data());
#else
    std::vector<char> buffer(4096, '\0');
    const ssize_t n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    return n > 0 ? std::string(buffer.data(), static_cast<std::size_t>(n)) : std::string();
#endif
}
}  // namespace

ChildResult runSelfAsChild(const std::string& mode) {
    ChildResult result;
    const std::string exe = executablePath();
    if (exe.empty()) {
        return result;
    }

    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    const std::string flag = "--pme-rtcheck-child=" + mode;
    std::vector<char*> argv = {const_cast<char*>(exe.c_str()), const_cast<char*>(flag.c_str()),
                               nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return result;
    }

    std::array<char, 1024> buffer{};
    for (;;) {
        const ssize_t n = read(fds[0], buffer.data(), buffer.size());
        if (n > 0) {
            result.standardError.append(buffer.data(), static_cast<std::size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.launched = true;
    if (WIFSIGNALED(status)) {
        result.signaled = true;
        result.signalNumber = WTERMSIG(status);
    } else if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    }
    return result;
}

bool endedByAbort(const ChildResult& result) {
    return result.launched && result.signaled && result.signalNumber == SIGABRT;
}

#endif

}  // namespace pme::rtcheck::test
