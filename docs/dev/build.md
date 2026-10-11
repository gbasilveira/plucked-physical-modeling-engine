# Build system (P0.1)

SPEC sections implemented: none (build infrastructure).

This note covers the CMake presets, compiler and floating-point flags, how to build and test, the
dependency and licence table, and the Git LFS rules. CI for the three systems is described in
[ci.md](ci.md); the RT-safety trap library is described in [rtcheck.md](rtcheck.md).

## Requirements

- CMake 3.25 or later, Ninja, a C++20 compiler (GCC, Clang/AppleClang, or MSVC / clang-cl), and a C
  compiler (Highway's CMake project enables C).
- Windows: run CMake from a Visual Studio developer shell (x64 Native Tools prompt) so that `cl`
  and Ninja are on `PATH`.
- Python 3.12 or later and [uv](https://docs.astral.sh/uv/) for the design compiler and tools.
- Git LFS for `data/` and audio files.

## Presets

`CMakePresets.json` (schema version 6) defines configure, build and test presets named
`<system>-<config>`; the binary directory is `build/<preset>`. Presets are only available on
their own host system (`linux`: Linux, `macos`: Darwin, `windows`: Windows).

| Config | Build type | Extra |
| --- | --- | --- |
| debug | Debug | |
| release | Release | |
| rt-safety | RelWithDebInfo | `PME_RTSAFETY=ON`, which defines `PME_RTSAFETY=1` on project targets and activates the trap library `pme_rtcheck` ([rtcheck.md](rtcheck.md)). Setting it together with a sanitizer is a CMake error. |
| asan | Debug | GCC/Clang: `-fsanitize=address,undefined -fno-omit-frame-pointer`. MSVC: `/fsanitize=address`, `/RTC1` removed, `/Zi` kept, `/INCREMENTAL:NO`. |
| tsan | Debug | `-fsanitize=thread -fno-omit-frame-pointer` (Linux and macOS only). |
| coverage | Debug | GCC: `--coverage`. Clang/AppleClang: `-fprofile-instr-generate -fcoverage-mapping`. Windows (MSVC): Debug with full PDBs (`/Zi`, `/DEBUG:FULL`, `/INCREMENTAL:NO`) for an external coverage tool in P0.2. clang-cl source-based coverage is not used in P0.1. |

There are 17 presets: `linux-*` and `macos-*` for all six configs, `windows-*` for five. **There is
no `windows-tsan`** (ADR-0004, Accepted): ThreadSanitizer does not exist for Windows targets, and a
preset that built without it would only be a mislabelled build. If ADR-0004 is ever superseded,
the entry is easy to add: add a `windows-tsan` configure preset inheriting `base`, `os-windows`,
`cfg-tsan` (with matching build and test presets), replace the `message(FATAL_ERROR ...)` for MSVC
in `cmake/sanitizers.cmake`, and add the entry to the sanitizer matrix of
`.github/workflows/nightly.yml`.

Compilers are not hard-coded. Choose with `CC`/`CXX`, for example
`CC=clang CXX=clang++ cmake --preset linux-debug -B build/clang-debug`. macOS presets build the
native architecture; the universal build is CI's `build (macos-universal)` entry, which passes
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` on the command line (`cmake --preset macos-release
-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`) and checks both slices with `lipo -archs`. clang-cl:
`CC=clang-cl CXX=clang-cl cmake --preset windows-debug` from a Visual Studio developer shell.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug          # no tests yet; exits 0
```

Options: `PME_RTSAFETY` (default OFF), `PME_FETCH_ALL_DEPS` (default OFF, also fetches nanobind,
CLAP, clap-wrapper, the VST 3 SDK and LV2 to prove every pin resolves), `PME_SANITIZER` (set by the
presets). `CPM_SOURCE_CACHE` is honoured; set it to share downloads between build directories.

## Targets

One static library per engine module, built from `engine/src/<module>/`, with a `pme::<module>`
alias: `pme_osc`, `pme_string`, `pme_bridge`, `pme_body`, `pme_nonlinear`, `pme_exciters`,
`pme_dampers`, `pme_barriers`, `pme_runtime`, `pme_sched`, `pme_simd`. `pme_engine` (alias
`pme::engine`) is an interface target that links all eleven. Each module currently holds one
placeholder source (replaced in P0.4 and by the module's own phase). `pme_simd` links Highway
privately (ADR-0001); no other module does.

## Compiler and floating-point flags

Applied to project targets only, through the interface target `pme_build_options`
(`cmake/compiler_options.cmake`); third-party targets never receive them.

- C++20, extensions off.
- GCC/Clang/AppleClang: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- MSVC: `/W4 /permissive- /Zc:__cplusplus /utf-8`.
- Warnings are errors through the target property `COMPILE_WARNING_AS_ERROR ON` on project
  targets; `CMAKE_COMPILE_WARNING_AS_ERROR` is not set. Third-party packages are added as `SYSTEM`.
- Floating point (Numerics rule 2): never `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations` or
  `/fp:fast`. FMA contraction is fixed per build: `-ffp-contract=off` on GCC/Clang/AppleClang,
  `/fp:precise` on MSVC (clang-cl also gets `/clang:-ffp-contract=off`).
- Sanitizer and coverage flags are global (third-party code included) so that the runtime matches.

## Formatting and static analysis

Pinned versions (the same ones CI uses, [ci.md](ci.md)): **clang-format 18.1.8** (PyPI wheel, run
through `uvx`) and **clang-tidy 18** (Ubuntu package `clang-tidy-18`; 18.1.3 when written). The
scripts cover every tracked `*.cpp`, `*.cc`, `*.hpp`, `*.h` under `engine/`, `tools/`,
`reference/`, `plugin/`, `bindings/` and `compiler/`, never a list of files.

- `.clang-format`: Google base style, 4-space indent, 100 columns, left pointer alignment.
  Check, the exact command the `lint` job runs:
  `python3 tools/ci/check_format.py --clang-format "uvx clang-format==18.1.8"`.
  Fix in place: `uvx clang-format==18.1.8 -i <files>`.
- `.clang-tidy`: `bugprone-*`, `performance-*`, `modernize-*`, `cppcoreguidelines-*`, warnings as
  errors, headers from every project directory; each disabled check has a reason in the file.
  `engine/src/rtcheck/` and `engine/tests/rtcheck/` carry a nested `.clang-tidy` that relaxes a
  few checks with reasons (low-level infrastructure). Run with Clang compile databases for the
  debug and rt-safety builds, as the `lint` job does:

  ```bash
  CC=clang-18 CXX=clang++-18 cmake --preset linux-debug -B build/lint-debug
  CC=clang-18 CXX=clang++-18 cmake --preset linux-rt-safety -B build/lint-rt
  python3 tools/ci/run_clang_tidy.py -p build/lint-debug -p build/lint-rt --clang-tidy clang-tidy-18
  ```


## Python

`pyproject.toml` defines the package `pme-compiler` (import `pme_compiler`, in
`compiler/pme_compiler/`), Python 3.12 or later. `uv.lock` is the committed universal lock for
Windows, macOS and Linux.

```bash
uv sync --locked --all-extras      # installs runtime, fem and fit extras, and the dev group
uv run pytest                      # compiler/tests and tools/tests
uv run ruff check && uv run ruff format --check
uv run mypy                        # strict, over compiler/ and tools/
uv run python tools/check_spdx.py  # SPDX header check
```

Extras: `fem` = scikit-fem, gmsh, meshio (ADR-0002); `fit` = jax. After changing dependencies, run
`uv lock` and commit `uv.lock`.

## SPDX header check

`tools/check_spdx.py` lists tracked files (`git ls-files`) and requires
the line `SPDX-License-Identifier: MIT` (exactly: `MIT-0` or `MIT OR Apache-2.0` fail; trailing whitespace is allowed) in the first five lines of C++, CMake, Python, shell, YAML, TOML and
comment-capable dotfiles. Exempt types and files, each with a reason, are listed in the script
(Markdown, JSON, `LICENSE`, `uv.lock`, `.python-version`, empty files, binary files, `data/`).
`cmake/CPM.cmake` is vendored and checked for its own MIT licence text instead. An unknown file
type fails the check, so adding a new type is a deliberate edit to the script.

## Dependencies and licences (ADR-0003)

All C++ dependencies are fetched by CPM.cmake in `cmake/dependencies.cmake`, pinned to the full
commit SHA. For annotated tags the SHA is the peeled commit (`git ls-remote <url> 'refs/tags/<tag>^{}'`).
Pins were resolved on 2026-10-11 with `git ls-remote`.

| Name | Upstream | Tag | Commit | Licence (SPDX) | Fetched |
| --- | --- | --- | --- | --- | --- |
| CPM.cmake (vendored) | https://github.com/cpm-cmake/CPM.cmake | v0.43.2 | 01678cfe175eab53143640429fdaa57c3c621cc6 | MIT | vendored |
| Highway | https://github.com/google/highway | 1.4.0 | 2607d3b5b0113992fe84d3848859eae13b3b52c1 | Apache-2.0 OR BSD-3-Clause | every preset |
| doctest | https://github.com/doctest/doctest | v2.5.3 | 2d0a9359a60c51affe2a9bebb1be1dca47868151 | MIT | every preset |
| nanobind | https://github.com/wjakob/nanobind | v3.1.0 | 68480a9e6883bb1bf65e1925423321e2a89d07f9 | BSD-3-Clause | `PME_FETCH_ALL_DEPS` |
| CLAP | https://github.com/free-audio/clap | 1.2.10 | 195b42a004144fab0b3cf95e9c067187d15365b7 | MIT | `PME_FETCH_ALL_DEPS` |
| clap-wrapper | https://github.com/free-audio/clap-wrapper | v0.16.0 | 1cca996e96f29ab2be7ae9f8cfe532bbc92e1dd6 | MIT | `PME_FETCH_ALL_DEPS` |
| VST 3 SDK | https://github.com/steinbergmedia/vst3sdk | v3.8.1_build_84 | 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96 | MIT | `PME_FETCH_ALL_DEPS` |
| LV2 (GitHub mirror of gitlab.com/lv2/lv2) | https://github.com/lv2/lv2 | v1.18.10 | 0bcde338db1c63bbc503b4d1f6d7b55ed43154af | ISC (mixed; see `LICENSES/` upstream) | `PME_FETCH_ALL_DEPS` |

Vendored `cmake/CPM.cmake`: release asset of v0.43.2
(`https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.2/CPM.cmake`), SHA-256
`49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa`, verified at configure time. It
carries the MIT licence text in its header but no SPDX identifier line; it is left unmodified so the
hash stays valid.

Transitive fetches: nothing in P0.1 builds clap-wrapper, so its own downloads do not run. D1 and D2
pass our pinned CLAP and VST 3 SDK source directories to clap-wrapper; D2 adds the AudioUnit SDK
pin.

Python dependencies are locked in `uv.lock`; GitHub Actions in `.github/workflows/` are referenced
by full commit SHA with the tag in a comment.

## Git LFS

`.gitattributes` routes everything under `data/` and the audio extensions `*.wav`, `*.flac`,
`*.aif`, `*.aiff`, `*.ogg`, `*.mp3` anywhere to Git LFS (`filter=lfs diff=lfs merge=lfs -text`).
`data/.gitkeep` matches `data/**`, so a later rule (`data/.gitkeep text eol=lf -filter -diff
-merge`) takes it out of LFS; `git check-attr -a data/.gitkeep` shows `filter: unset`, and
`git lfs ls-files` lists no file after staging. `* text=auto eol=lf` normalises text files; images
are marked `binary`.
