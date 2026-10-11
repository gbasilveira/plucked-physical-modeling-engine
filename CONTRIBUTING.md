# Contributing

## Workflow

- Trunk-based development. Every change reaches `main` through a pull request.
- Commit messages and pull-request titles start with the phase ID, for example
  `A2: compile wound-string mass and stiffness`, or with `PLAN:` for planning-only changes.
- Phase branches are named `phase/<ID>-<slug>`.
- Requirement and test IDs appear in test names, for example `TEST_CASE("A2-T1 wound string mass")`.
- Every source file carries an SPDX header (`SPDX-License-Identifier: MIT`); `tools/check_spdx.py`
  enforces it.
- Real-time rules, numerics rules and code style are in the Software guidelines of
  [docs/plan/phasing-plan.md](docs/plan/phasing-plan.md).

## Model policy

Planning documentation is written by Opus agents and code by Sonnet agents; see
[CLAUDE.md](CLAUDE.md). Planning documentation is everything under `docs/plan/`, `docs/adr/`,
`docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/` and `docs/agents/`, plus the
narrative and conclusions of the decision reports named in phase cards. Code, tests, CI, build
scripts, tools, module notes and user documentation are written by Sonnet agents.

## Problems with interfaces or the specification

- Interface changes: [docs/icr/README.md](docs/icr/README.md).
- Specification defects: [docs/spec-issues/README.md](docs/spec-issues/README.md).

## Build and test

See [docs/dev/build.md](docs/dev/build.md).

## Required checks

A change merges into `main` only when **all** of the following checks of the pull-request workflow
(`.github/workflows/pr.yml`) are green **and** a reviewer has approved. The list is
`tools/ci/required_checks.json`; a test keeps it equal to the job names of `pr.yml`.

| Check | What it covers |
| --- | --- |
| `lint` | clang-format, ruff check, ruff format, mypy (strict), SPDX headers, clang-tidy |
| `build (windows-msvc)` | MSVC: `windows-debug`, `windows-rt-safety` |
| `build (windows-clang-cl)` | clang-cl: `windows-debug`, `windows-rt-safety` |
| `build (macos-arm64)` | AppleClang arm64: `macos-debug`, `macos-rt-safety` |
| `build (macos-universal)` | `macos-release` with `arm64;x86_64`, both slices verified |
| `build (linux-gcc)` | GCC: `linux-debug`, `linux-rt-safety` |
| `build (linux-clang)` | Clang: `linux-debug`, `linux-rt-safety` |
| `python (linux)`, `python (macos)`, `python (windows)` | `uv sync --locked`, pytest |
| `coverage` | `linux-coverage` (GCC); line coverage of `engine/src` at least 90 % |
| `deps` | configure with `PME_FETCH_ALL_DEPS=ON` (every pin resolves) |
| `traceability` | traceability matrix over every `junit-*` artifact of the run; fails when a phase marked `complete` has a gap not on `tools/trace/known-gaps.json` ([docs/dev/traceability.md](docs/dev/traceability.md)) |

Each `build` check runs every unit, oracle, integration, golden and RT-safety test
(`ctest -LE "perf|fuzz|soak"`). Sanitizers, performance, fuzz and soak run in the nightly workflow
and are not required checks. Branch protection is applied from the list with
`tools/ci/apply_branch_protection.py` (dry run by default; applying needs repository admin
rights). Workflows, labels, retention and how to read the reports: [docs/dev/ci.md](docs/dev/ci.md).
