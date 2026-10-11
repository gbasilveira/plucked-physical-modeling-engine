# ADR-0004: No ThreadSanitizer preset on Windows

- **Status:** Proposed (needs the orchestrator's approval before P0.1 can be accepted with this deferral)
- **Date:** 2026-10-11
- **Deciders:** proposed by the planner (Opus) while briefing P0.1
- **Phase:** P0.1; defers one cell of P0.1-R2 and P0.1-T1
- **References:** phase card P0.1 (P0.1-R2, P0.1-T1); phasing plan, Software guidelines (Testing policy: "TSan on scheduler tests", Nightly); PLAN-STACK

## Decision

The `tsan` preset exists for Linux and macOS only. Windows has no `tsan` preset. P0.1-R2 ("debug, release, rt-safety, asan, tsan and coverage, for each of the three systems") and P0.1-T1 ("every preset configures and builds on all three systems") are met with this one exception: 17 of the 18 system × configuration presets.

## Context

- ThreadSanitizer is not available for Windows targets in MSVC, and clang-cl does not support `-fsanitize=thread` for Windows targets either. A Windows `tsan` preset could therefore only be a mislabelled build without the sanitizer, which would pass P0.1-T1 without testing anything.
- The Testing policy runs TSan on scheduler tests nightly. The scheduler (C5) has platform-specific parts on every system (MMCSS on Windows, audio workgroups on macOS, rtkit on Linux, per PLAN-COMP Threads), but the lock-free queues, barriers and fork-join logic that TSan checks are shared code.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **Omit the Windows `tsan` preset** (proposed) | Honest; every preset that exists does what its name says. | Windows-only threading code (MMCSS registration) is not checked by TSan. |
| A Windows `tsan` preset that builds without the sanitizer | Satisfies the letter of P0.1-T1. | Misleading; a green check with no meaning. |
| Run Windows threading checks under another tool | Covers Windows-specific code. | No equivalent tool fits a per-pull-request or nightly CI job today; can be revisited by C5. |

## Consequences

- P0.1's implementer defines `linux-tsan` and `macos-tsan` and no `windows-tsan`, and reports this ADR against P0.1-R2 and P0.1-T1.
- P0.2's nightly sanitizer job runs TSan on Linux and macOS only.
- C5 keeps Windows-specific threading code thin, and the code reviewer of C5 checks it by hand against the real-time rules.

## Verification

The P0.1 code reviewer confirms that `CMakePresets.json` defines every other system × configuration preset and that the bootstrap CI run builds all 17.

## Revisiting

Reopen if MSVC or clang-cl gains ThreadSanitizer support for Windows targets; the Windows preset is then added in a pull request and this ADR is superseded.
