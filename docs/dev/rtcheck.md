# RT-safety trap, `pme_rtcheck` (P0.2-R5)

SPEC sections implemented: none (infrastructure); implements the Real-time rules 1 and 6 of the
Software guidelines (trap only).

`pme_rtcheck` (alias `pme::rtcheck`) is a static library in `engine/src/rtcheck/`. It is test
infrastructure, not audio-thread engine code: it is **not** part of `pme_engine`, has no
performance baseline, and is excluded from the coverage gate. Tests and (later) the plug-in link
it.

```cpp
#include "pme/rtcheck/rtcheck.hpp"

pme::rtcheck::ScopedRealtimeThread scope;   // this thread is now "real-time" (nestable)
// ... allocation or a blocking call here aborts in the rt-safety build ...
```

API (`pme::rtcheck`): `ScopedRealtimeThread` (non-copyable, non-movable), `isRealtimeThread()`,
`enabled()` (constexpr, true when `PME_RTSAFETY=1`), `blockingCall(const char* what)` for the
project's own blocking primitives (queues, barriers, pools; later phases call it before they
block). The scope keeps a `thread_local` nesting counter in every build; the trap itself exists
only in the rt-safety build.

## When it is active

Configure with the `*-rt-safety` presets (`PME_RTSAFETY=ON`, RelWithDebInfo). `PME_RTSAFETY=1` is
defined on all project targets (through `pme_build_options`) and publicly on `pme_rtcheck`, so
that consumers and the library agree on `enabled()`. In every other preset the library is a no-op
and replaces **no** global symbol. Setting `PME_RTSAFETY` together with `PME_SANITIZER` is a CMake
`FATAL_ERROR`: the sanitizer runtimes own `malloc` and `operator new`, and the presets never
combine them.

On a violation the trap writes `pme_rtcheck: real-time violation: <what> called on a real-time
thread` to standard error with `write(2)` (POSIX) or `WriteFile` (Windows) from a fixed stack
buffer, then calls `std::abort()`. It does not allocate, lock or format. On Windows it also
disables the abort message box and Windows Error Reporting (`_set_abort_behavior`,
`SetErrorMode`, the debug-CRT report modes) so that CI never hangs on a dialog; the process then
ends with exit code 3.

## What is intercepted

| Call | Linux (glibc) | macOS | Windows |
| --- | --- | --- | --- |
| every global `operator new` / `delete` overload (plain, `[]`, sized, aligned, `nothrow`) | yes | yes | yes |
| `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`, `posix_memalign` | yes, by symbol interposition | **no** | **no** |
| `pthread_mutex_lock`, `pthread_cond_wait`, `pthread_cond_timedwait`, `pthread_join` | yes | **no** | n/a |
| `nanosleep`, `clock_nanosleep`, `usleep`, `sleep` | yes | **no** | **no** |
| `blockingCall()` | yes | yes | yes |

`free(nullptr)` and `delete nullptr` are allowed on a marked thread (they do nothing).

### Linux

The executable defines the libc symbols itself. The dynamic linker searches the executable first,
so calls from the program and from shared libraries (libstdc++ included) bind to the definitions
in `rtcheck.cpp`. The allocation functions reach the real allocator through glibc's exported
`__libc_malloc`, `__libc_calloc`, `__libc_realloc`, `__libc_free` and `__libc_memalign`; `dlsym`
is deliberately not used for them, because it allocates. The blocking functions are forwarded to
the next definition found with `dlsym(RTLD_NEXT, ...)`, cached in constant-initialised atomics
(a function-local static would take its guard through `pthread_mutex_lock`, which is interposed).
The interposition is compiled only where `__GLIBC__` is defined; on another libc (musl) only the
`operator new` family and `blockingCall()` remain.

### macOS: libc is not interposed

`DYLD_INTERPOSE` is the documented mechanism, but dyld does not apply an image's interpose table
to that image's own call sites. The traps would therefore be bypassed exactly where the test
(and later the engine) calls them, unless the interposer lives in a separate dylib loaded with
`DYLD_INSERT_LIBRARIES`, which a static library cannot provide. This could not be verified and
is left out, as P0.2-R5 allows: on macOS the trap covers the `operator new` family and
`blockingCall()` only. The corresponding controls (mutex, sleep, `malloc`) are not compiled
there. Recorded as an open risk in the P0.2 handoff report. A hardened-runtime plug-in host
would not honour `DYLD_INSERT_LIBRARIES` anyway.

### Windows: operator new only

The Windows CRT allocator and the Win32 wait functions cannot be replaced without a hooking
library (Detours, MinHook), which the project does not take as a dependency. The trap covers the
`operator new` family and `blockingCall()`. Raw `malloc`/`HeapAlloc` and `WaitForSingleObject`
are caught by review and, for the project's own primitives, by `blockingCall()`.

## Why the replacements live in the same object file as the scope

A static library's object file is only linked into an executable when something in it is
referenced. `ScopedRealtimeThread`'s constructor is the one symbol every user must reference, so
everything that can trap (`operator new` and `delete`, the libc interposers) is defined in
`rtcheck.cpp` next to it. Defining the replacements in another file would let the linker drop
them silently and the trap would never fire.

## Tests

`engine/tests/rtcheck/` (label `unit;rt`, registered only when `PME_RTSAFETY` is ON):

- `P0.2-T1 allocation inside ScopedRealtimeThread aborts` and the `P0.2-R5` controls. doctest has
  no death tests, so each aborting scenario runs in a child process: the test executable
  re-launches itself with `--pme-rtcheck-child=<scenario>` (`posix_spawn` and `waitpid` on POSIX,
  `CreateProcessW` on Windows; no shell), captures the child's standard error, and asserts the
  child ended by `SIGABRT` (POSIX) or exit code 3 (Windows) **and** that standard error contains
  the diagnostic naming the call.
- Controls: allocation outside a scope, and after the scope ends, does not abort; a nested scope
  still traps after the inner scope ends; `blockingCall()` aborts inside a scope only; on Linux,
  `pthread_mutex_lock`, `usleep`/`nanosleep` and `malloc` abort inside a scope.
- In builds without `PME_RTSAFETY`, `pme_rtcheck_noop_tests` (label `unit`) checks that the scope
  only counts and nothing aborts.

Inside a scope, in-process test code must not use doctest assertions (they allocate); the cases
that need a scope run it in the child process or record results in plain variables and assert
after the scope ends.

## Using it from tests of other phases

Link `pme::rtcheck`, wrap the code under test in a `ScopedRealtimeThread`, and register the test
with the label `rt` so that it also runs under the `*-rt-safety` presets. Run it directly with
`cmake --preset linux-rt-safety && cmake --build --preset linux-rt-safety && ctest --preset
linux-rt-safety -L rt`.
