# ADR-0003: Dependency pinning — CPM.cmake at exact commits, uv lock file for Python

- **Status:** Accepted
- **Date:** 2026-10-11
- **Deciders:** the pinning policy is fixed by the Software guidelines of the phasing plan ("C++ dependencies come through CPM.cmake pinned to exact commits; Python dependencies through a lock file. No third-party code enters the repository without a licence record."). The tool choices below (vendored CPM.cmake, uv, SHA-pinned GitHub Actions) were made by the planner (Opus) in P0.1; the P0.1 pull request review is where they can be challenged.
- **Phase:** P0.1 (requirements P0.1-R3, P0.1-R4, P0.1-R6)
- **References:** PLAN-STACK (Build and CI row: "CMake presets, CPM.cmake or vcpkg"); phasing plan, Software guidelines (Languages and dependencies, Numerics rule 3, Licensing, CI); phase cards P0.1, P0.2, H1

## Decision

1. **C++.** Every third-party C++ dependency is added with CPM.cmake in one file, `cmake/dependencies.cmake`, and pinned to a full 40-hex-digit Git commit SHA. Tags and branch names are never the pin. CPM.cmake itself is vendored at a released version in `cmake/CPM.cmake`, with its version and SHA-256 recorded next to the pins.
2. **Python.** `pyproject.toml` declares dependencies with lower bounds; `uv.lock` (from uv) is the lock file, is committed, and CI installs with `uv sync --locked`, which fails when the lock is out of date. Development tools (pytest, ruff, mypy) are locked in the same file.
3. **CI actions.** GitHub Actions in `.github/workflows/` are referenced by full commit SHA, with the release tag in a comment.
4. **Licence record.** Each pinned dependency has a licence record (SPDX identifier, upstream URL, release tag, commit) in `cmake/dependencies.cmake` and in the dependency table of docs/dev/build.md. H1 builds the software bill of materials from these records.

## Context

- Numerics rule 3 requires bit-identical output for any thread count on one build. A build is only reproducible if the same sources go into it every time; a moving dependency (a branch, a re-pointed tag, an unlocked Python range) breaks that silently.
- Git tags can be moved or deleted upstream; a commit SHA cannot change what it names.
- The C++ dependencies P0.1-R4 names are Highway, doctest, nanobind, CLAP, clap-wrapper, the VST 3 SDK (version 3.8 or later, MIT) and LV2. Some of them fetch further dependencies of their own (clap-wrapper can download the CLAP and VST 3 SDKs itself); those transitive fetches must be pinned too.
- The design compiler needs NumPy, SciPy, Gmsh, scikit-fem (ADR-0002), JAX and pytest on Python 3.12 or later, on Windows, macOS and Linux. A lock file must therefore cover all three systems at once.
- PLAN-STACK allows CPM.cmake or vcpkg; the Software guidelines chose CPM.cmake.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **CPM.cmake, commit-SHA pins** (chosen, C++) | Plain CMake, no extra tool; source cache (`CPM_SOURCE_CACHE`) avoids repeat downloads; deduplicates packages by name, so our pin wins when a dependency asks for the same package. | Fetches from source at configure time, so a first configure needs network access; SHA pins cannot use shallow clones, so large repositories (VST 3 SDK with its submodules) clone in full once per cache. |
| vcpkg manifest with a baseline | Binary caching; widely used. | A second versioning layer (ports and baselines) between us and upstream commits; a separate tool to install on every runner. |
| Git submodules | Pins commits natively. | Every clone must recurse; no CMake-level deduplication or cache; easy to commit a submodule move by accident. |
| CPM.cmake fetched at configure time (`get_cpm.cmake`) instead of vendored | Smaller repository. | The build tool itself would come from the network; vendoring one MIT file with a recorded hash is simpler and auditable. |
| **uv with `uv.lock`** (chosen, Python) | One universal lock for all systems and Python versions; standard `pyproject.toml` (PEP 621); fast; `--locked` mode turns lock drift into a CI failure. | Another tool for contributors to install (single binary). |
| pip-tools (`requirements.txt` with hashes) | Only pip needed. | Resolves for the platform it runs on; three systems need three lock files. |
| Poetry | Cross-platform lock. | Slower resolution; its own configuration conventions beside PEP 621. |
| conda / pixi | Needed for FEniCSx-style native stacks. | Not needed with scikit-fem (ADR-0002); heavier for contributors. |

## Consequences

- **P0.1** vendors CPM.cmake, writes `cmake/dependencies.cmake` with SHA pins for Highway, doctest, nanobind, CLAP, clap-wrapper, the VST 3 SDK (3.8 or later) and LV2, writes `pyproject.toml` and `uv.lock`, and records licences in docs/dev/build.md. Dependencies a preset does not build are still fetched by a configure option, so every pin is proven to resolve.
- **Transitive fetches.** Where a dependency would fetch its own dependencies (for example clap-wrapper fetching the CLAP or VST 3 SDK), the build passes our pinned source directories to it instead, so no download in the build is unpinned. `CPM_USE_LOCAL_PACKAGES` stays off in CI.
- **Changing a pin** is a code change in a pull request whose description gives the old and new commit, the upstream release notes or compare link, and the licence of the new version. The full pull-request workflow, including golden-feature and performance checks, must pass. A licence change goes to H1 (and the human owner, per the escalation rules) before merge.
- **Offline and restricted networks.** A configure with a populated `CPM_SOURCE_CACHE` needs no network. If a dependency cannot be reached when a pin is chosen, the pin is left missing and the gap is reported; a pin is never guessed or copied from memory without checking it against the upstream repository.
- **P0.2** pins every action it uses by SHA, caches `CPM_SOURCE_CACHE` and the uv cache between runs, and runs `uv sync --locked`.
- **H1** reads the licence records to build the software bill of materials and checks them against upstream licence files.

## Verification

- P0.1-T1: every preset configures and builds from the pinned sources.
- P0.1-T2: pytest runs in an environment installed from `uv.lock`.
- Code review (P0.1 and every later pull request that touches `cmake/dependencies.cmake`, `pyproject.toml`, `uv.lock` or `.github/workflows/`): every `GIT_TAG` is a 40-hex SHA, every action reference is a SHA, every new dependency has a licence record.

## Revisiting

Reopen this decision if configure-time fetching makes CI unreliable or too slow even with caching (P0.2 or V2 would show it in CI timings), in which case vcpkg binary caching is the first alternative to evaluate.
