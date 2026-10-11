# Continuous integration (P0.2)

SPEC sections implemented: none (infrastructure); implements the Real-time rules 1 and 6 of the
Software guidelines (trap only, see [rtcheck.md](rtcheck.md)).

Three workflows in `.github/workflows/`: `pr.yml` (pull request), `nightly.yml` and
`release.yml`. Common rules: `permissions: contents: read` at the top, no secrets, every action
pinned by full commit SHA with the tag in a comment (ADR-0003), explicit runner images
(`ubuntu-24.04`, `macos-15`, `windows-2025`; never `-latest`, so that a runner upgrade is a
visible change), `CPM_SOURCE_CACHE` cached on `cmake/dependencies.cmake`, a `concurrency` group
that cancels superseded runs (not for `release.yml`), and a `timeout-minutes` on every job.

## Pinned tools

| Tool | Pin | Where |
| --- | --- | --- |
| GitHub Actions | full SHA, tag in a comment | all workflows |
| Python packages (pytest, ruff, mypy) | `uv.lock`, `uv sync --locked` | `lint`, `python` |
| clang-format | `18.1.8` (PyPI wheel, `uvx clang-format==18.1.8`) | `lint`, pytest |
| clang-tidy and Clang | Ubuntu 24.04 package major version 18 (`clang-18`, `clang-tidy-18`, `libclang-rt-18-dev`; 18.1.3 when written). The apt minor version follows the pinned runner image. | `lint`, `linux-clang`, nightly Linux Clang |
| gcovr | `8.3` (`uvx --from gcovr==8.3`) | `coverage` |
| actionlint (local check only) | `actionlint-py==1.7.7.23` | developers |

clang-tidy comes from apt and not from PyPI because it must see the same Clang resource headers
as the compiler that wrote the compile database.

## `pr.yml`: pull request

Triggers: `push` (every branch), `pull_request`, `workflow_dispatch`. The job names below are the
required checks of `main` (`tools/ci/required_checks.json`, [CONTRIBUTING.md](../../CONTRIBUTING.md)).

| Job | Runner | What it does |
| --- | --- | --- |
| `lint` | ubuntu-24.04 | one step per check, all run even if one fails: `clang-format` (`tools/ci/check_format.py`), `ruff check`, `ruff format --check`, `mypy` (strict), SPDX (`tools/check_spdx.py`), then clang-tidy over the `linux-debug` and `linux-rt-safety` compile databases (configured with Clang) |
| `build (windows-msvc)` | windows-2025 | `windows-debug`, `windows-rt-safety` |
| `build (windows-clang-cl)` | windows-2025 | the same with `CC=clang-cl CXX=clang-cl` in a Visual Studio developer shell |
| `build (macos-arm64)` | macos-15 | `macos-debug`, `macos-rt-safety` (AppleClang) |
| `build (macos-universal)` | macos-15 | `macos-release` with `-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64`; `lipo -archs` must list both slices for `libpme_simd.a`, `libhwy.a` and `libpme_rtcheck.a`; tests run natively (arm64) |
| `build (linux-gcc)` | ubuntu-24.04 | `linux-debug`, `linux-rt-safety` |
| `build (linux-clang)` | ubuntu-24.04 | the same with Clang 18 and `libclang-rt-18-dev` |
| `python (linux)`, `python (macos)`, `python (windows)` | per system | `uv sync --locked --all-extras`, `pytest --junitxml` |
| `coverage` | ubuntu-24.04 | `linux-coverage` (GCC), gcovr HTML and Cobertura, coverage gate |
| `deps` | ubuntu-24.04 | configure with `PME_FETCH_ALL_DEPS=ON` (every pin resolves) |

Every `build` step per preset is `cmake --preset`, `cmake --build --preset`,
`ctest --preset <p> -LE "perf|fuzz|soak" --output-on-failure --output-junit <file>`. Nothing
hard-codes a test name: tests are selected by CTest label, so tests registered by other phases
run automatically.

### CTest labels

Every registered CTest test carries at least one of `unit`, `oracle`, `integration`, `golden`,
`rt`, `perf`, `fuzz`, `soak`. `rt` marks tests that must also pass under the `rt-safety` preset.
The pull-request workflow runs everything except `perf|fuzz|soak`; the nightly workflow runs
those three in their own jobs.

### Coverage gate

Line coverage of `engine/src/` must be at least 90 % (Testing policy), measured with GCC and
gcovr, **excluding**: `engine/src/rtcheck/` (test infrastructure), `engine/src/sched/platform/`
(platform glue; the directory is the convention for it, none yet) and files named
`*_placeholder.cpp`. `tools/ci/coverage_gate.py` reads the Cobertura XML and applies the same
exclusions; when the measured set is empty it prints "no engine sources measured" and passes.
macOS and Windows build their coverage presets in the nightly workflow, build only.

### Reports and artifacts

Every `ctest` call writes JUnit XML (`--output-junit`), every `pytest` call writes
`--junitxml`; the files are uploaded with `actions/upload-artifact` (`if: always()`) and
summarised in the job summary by `tools/ci/junit_summary.py` (standard library, no third-party
action, no write permission). The coverage job uploads `coverage/` (HTML and `coverage.xml`).

Retention: pull-request artifacts 14 days, nightly 30 days, release 90 days. "Coverage upload"
means a workflow artifact; no external coverage service is used, as that would need a token.

### P0.2-T2: showing the real `lint` job fail

The unit-level proof is `tools/tests/test_p0_2_t2.py` (a temporary repository with one
mis-formatted file; the format script exits 1 and names it). To see the workflow fail:

- manual: Actions, "pr", Run workflow, tick `p0_2_t2_inject_misformat`; or
  `gh workflow run pr.yml --ref <branch> -f p0_2_t2_inject_misformat=true`
  (the workflow file must exist on the default branch for dispatch to work);
- from any branch, with no dispatch: push a commit whose message contains `[p0.2-t2-misformat]`.

Either way the `lint` job writes `engine/tests/p0_2_t2_injected_misformat.cpp` into the checkout,
stages it, and the `clang-format` step fails naming that file. Nothing is committed.

### Required checks and branch protection

`tools/ci/required_checks.json` lists the check names; a pytest test
(`test_p0_2_r7_required_checks_match_workflow`) fails when a name is not a job (or
matrix-expanded job) of `pr.yml`, and another when a job is not listed.
`python3 tools/ci/apply_branch_protection.py --repo OWNER/REPO` prints the REST request (dry run,
the default); `--apply` with `GITHUB_TOKEN` of an administrator sends it: required checks, strict
(up to date), one approving review, stale reviews dismissed, no force pushes or deletions.
Applying is an owner/orchestrator step.

## `nightly.yml`

Triggers: `schedule` (03:17 UTC daily), `workflow_dispatch`, and `push` when
`.github/workflows/nightly.yml` or `tools/ci/**` changes (the self-push trigger: dispatch only
works for workflow files on the default branch, so a branch could not otherwise exercise it).
Jobs:

- `sanitizers (<entry>)`: `linux-asan` and `linux-tsan` with GCC **and** Clang, `macos-asan`,
  `macos-tsan`, `windows-asan` (MSVC). No Windows TSan (ADR-0004, accepted). Each runs
  `ctest -LE "perf|fuzz|soak"`: ASan and UBSan over unit and integration tests; TSan over
  whatever tests exist until C5 adds the scheduler tests.
- `coverage-build (macos|windows)`: configure and build the coverage presets, nothing else.
- `performance`, `fuzz`, `soak`: `ctest -L perf|fuzz|soak` on Linux (`linux-release`). With no
  such tests they print "no <label> tests yet" and succeed. `--no-tests=ignore` is used there
  and nowhere else, with a comment in the workflow.

Linux Clang entries install `libclang-rt-18-dev` (the sanitizer runtimes). **TSan on Ubuntu
runners:** if TSan binaries die with "FATAL: ThreadSanitizer: unexpected memory mapping", the
kernel's ASLR entropy is too high for the runtime; every Linux sanitizer job therefore runs
`sudo sysctl vm.mmap_rnd_bits=28` before the tests.

## `release.yml`

Triggers: push of a tag matching `v[0-9]+.[0-9]+.[0-9]+*`, `workflow_dispatch`, and branch pushes
(see below). It builds the `release` preset on Windows (MSVC), macOS (universal) and Linux
(GCC), runs `ctest -LE "perf|fuzz|soak"`, and uploads one archive per system as a workflow
artifact (90 days). **It publishes nothing**: no GitHub release, no registry, no
`contents: write`, no secrets. Signing and publishing arrive in H1/H2.

The `plan` job fails the run when the tag's version differs from `project(pme VERSION x.y.z)` in
the root `CMakeLists.txt` (`tools/ci/release_version.py`).

**The `tags` plus `paths` rule.** A `paths` filter is not a dependable partner of `tags` (a tag
push has no diff of its own to match against, so the combination can silently never fire), and a
push event with only `tags` never runs for a branch. A single `push` trigger with `tags` and
`paths` therefore cannot be relied on to serve both the tag and the branch dry run. The workflow instead listens to tags **and** all branches without a `paths`
filter, and its first job `plan` decides, from the push's own diff (`before..after`; a new
branch is judged by its tip commit): a tag push is a real run; a branch push is a dry run when it
changed `.github/workflows/release.yml` or anything under `tools/ci/`; any other branch push ends
after `plan` (one small job, skipped release jobs). A dry run computes the version
`0.0.0-dryrun+<short sha>` and otherwise behaves exactly like a tag run.

Archive layout (`tools/ci/stage_release.py`; the project has no `install()` rules yet, so the
build tree's outputs are staged): `pme-<version>-<system>/{VERSION,LICENSE,include/pme/,lib/}`
with every `libpme_*.a` / `pme_*.lib` and Highway's static library; `.zip` on Windows,
`.tar.gz` elsewhere.

## P0.2-T3: all workflows green on an empty change

An empty commit triggers `pr.yml` only. `nightly.yml` and `release.yml` are exercised by their
self-push triggers, which need a change to the workflow file or `tools/ci/` (for example a
comment edit), or a manual dispatch once the files are on `main`. The nightly and release jobs
must be green on that run.

## Running the checks locally

See [build.md](build.md) for the exact commands (format, tidy, Python, SPDX). Validate workflow
files with `uvx --from actionlint-py==1.7.7.23 actionlint .github/workflows/*.yml`.

<!-- ---- D1: plugin job ---- -->
## D1: the `plugin` job

`plugin (linux)`, `plugin (macos)` and `plugin (windows)` are the last job of `pr.yml`. On each
system the job configures `<system>-rt-safety` with `-DPME_BUILD_PLUGINS=ON`, builds, and runs
`ctest -R "^D1-"`: the controller-layer and CLAP-adapter tests (`unit` and `rt` labels, under the
RT-safety trap) and `D1-T1`, which runs clap-validator on the built `pme.clap`. The job then runs
clap-validator once more with its full output visible, and uploads JUnit as `junit-plugin-<system>`.
On Linux the job builds with Clang and runs `tools/ci/run_clang_tidy.py` over the compile database
of that build, because the `lint` job does not build plug-ins.

clap-validator is pinned to release tag `0.3.2` (the newest release with binaries) and each archive
is verified against a SHA-256 before it is unpacked. The release page lists no checksums; the
values in `pr.yml` were computed from the downloaded assets (D1 report). To change the pin, update
the tag, the three asset names and the three checksums together. `PME_CLAP_VALIDATOR` (environment
or CMake cache variable) points `D1-T1` at a local validator; without it the test is skipped.
<!-- ---- end D1 ---- -->
