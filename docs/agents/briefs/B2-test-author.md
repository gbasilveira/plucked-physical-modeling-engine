# Brief: B2 Plate and body FEM compiler — test-author

Status: **part 1 ready (pending CI on A2/A3/B1)** once the B2 implementer's commits are merged (`git log --grep '^B2: '`). **Part 2 (B2-T4 proper) is blocked**: the P0.5 reference plate rejects ribs (reference/README.md "Limits": rectangular, simply supported plates only; no ribs, bridges as beams, thickness grid or rim stiffness). B2-T4 needs a reference follow-up that adds ribs; the orchestrator routes it and tells you when it has merged. Until then report B2-T4 `not run` with that reason.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

computes mass-normalised body modes from geometry and materials, as SPEC-BODY defines.

Your job: write the reference-solver comparison **B2-T4** independently of the B2 implementer, taking expected values from the P0.5 reference solver, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy); the phase cards **B2 Plate and body FEM compiler** and **P0.5 Reference solver**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC sections: SPEC-BODY — docs/plan/engine-specification.md, "Plate model (offline)", "Modal data (offline output)".
- `.claude/agents/test-author.md` — your rules.
- docs/dev/reference-solver.md (R4 plate scheme, P0.5-T4, `plate_scheme_frequency_hz`, accuracy of the reference, Richardson extrapolation with `refine`), reference/README.md (scenario format, limits), tools/tests/test_p0_5_r7_python.py (how Python tests load `pme_reference` through `PME_REFERENCE_PYDIR` and `PME_REFERENCE_REQUIRED`).
- docs/agents/briefs/B2-implementer.md (the public API of `pme_compiler.body`, the boundary option `"simply_supported"`, the blob layout). Use the public API only; do not read the FEM internals to choose expected values.

## Inputs

- Completed dependencies and their outputs: the B2 implementer's commits; **P0.5** (merged at 103badf: the `pme_reference` Python module, built with `-DPME_BUILD_PYTHON_BINDINGS=ON`, and `pme_reference_cli`).
- Frozen interfaces: interfaces-v2 = 300b995.
- CI: the `reference-python` job of `.github/workflows/pr.yml` builds the module and runs `uv run pytest tools/tests/test_p0_5_*.py` with `PME_REFERENCE_REQUIRED=1` (a skip fails the job). The plain `python` job has no reference module, so your tests must skip there (as the P0.5 tests do).
## Concurrent work: A4, A6, B2 and B4 run at the same time

A4, A6, B2 and B4 (each with its test-author where briefed) run in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. A CI fixer and the D3 lv2lint follow-up may also be committing (`.github/workflows/pr.yml`, `docs/dev/ci.md`, `docs/dev/lv2.md`, `plugin/lv2/**`). Edits must not overlap. This table is identical in every A4, A6, B2 and B4 brief.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/bridge/**` (replaces `bridge_stub.cpp`), `engine/tests/bridge/**`, `docs/dev/bridge.md`; in `engine/tests/interfaces/test_stubs.cpp` the `compileBridgeTerms` SUBCASE only | A4 implementer | read only |
| `engine/tests/oracles/bridge/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-06, ORC-07 only | A4 test-author | read only |
| `engine/src/dampers/**` (replaces `dampers_placeholder.cpp`), `engine/tests/dampers/**`, `docs/dev/dampers.md` | A6 implementer | read only |
| `engine/tests/oracles/dampers/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-13 only | A6 test-author | read only |
| `compiler/pme_compiler/body/**` (including the shared `body/modal_blob.py`), `compiler/pme_compiler/materials/**`, `compiler/tests/test_b2_*.py` except `test_b2_t4_*.py` and `test_b2_r5_reference_*.py`, `compiler/tests/data/b2/**`, `docs/dev/body-compiler.md` | B2 implementer | B4 imports `pme_compiler.body.modal_blob` (read only) |
| `compiler/tests/test_b2_t4_*.py`, `compiler/tests/test_b2_r5_reference_*.py`, `compiler/tests/data/b2_t4/**`; in `.github/workflows/pr.yml` the one `uv run pytest tools/tests/test_p0_5_*.py …` line of the `reference-python` job (append its two test-file globs, nothing else) | B2 test-author | read only |
| `compiler/pme_compiler/importers/**`, `compiler/tests/test_b4_*.py`, `compiler/tests/data/b4/**`, `docs/dev/body-import.md` | B4 implementer | read only |
| `engine/src/string/**`, `engine/src/nonlinear/**`, `engine/src/osc/**`, `compiler/pme_compiler/schema/**`, `engine/src/runtime/loader/**`, `instruments/examples/**`, `reference/**`, `tools/reference-cli/**` and their tests and module notes | A2, A3, A1, B1, P0.5 (complete or pending CI; follow-ups only when the orchestrator routes them) | read only; call their internal headers and Python APIs |
| `engine/include/pme/**`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime`, `exciters`, `barriers`, `sched`, `simd`), `engine/tests/interfaces/**` (except the A4 SUBCASE), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2 = 300b995; changes only by ICR) | read only |
| `.github/workflows/**`, `tools/ci/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases (CI fixer and D3 only), except the B2 test-author's one pr.yml line above | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in this order: A4, A6, B2, B4, then the B2 test-author's pr.yml line.** The second and later merges rebase onto the earlier ones; keep every block self-contained so the order does not change behaviour.

| Shared file | A4 | A6 | B2 | B4 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A4: bridge coupling ----` … `# ---- end A4 ----` appended after the last line | one block `# ---- A6: dampers and pedals ----` … `# ---- end A6 ----` appended after the last line | none | none |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends one line `add_subdirectory(bridge)` | test-author appends one line `add_subdirectory(dampers)` | none | none |
| `docs/dev/interfaces.md`, sentence under "Stubs (P0.4-R3)" | move `bridge` out of the stub list, add "`bridge`: implemented by A4" next to the existing "implemented by" notes | move `dampers` out of the placeholder list, add "`dampers`: implemented by A6" | none | none |
| `pyproject.toml`, `uv.lock` | none | none | none expected (scikit-fem, gmsh and meshio are already in the `fem` extra; CI installs `--all-extras`) | at most one new line in an existing extra, in its own commit, then `uv lock`; if B2 also changed the file, B4 rebases and reruns `uv lock` (never hand-merge `uv.lock`) |
| `cmake/dependencies.cmake` | none | none | none | none |
| `.github/workflows/pr.yml` | none | none | test-author: one line (see table), after the CI fix and D3 have merged | none |

**Rules for every agent in these phases.**

- Test names start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;` (they break ctest filters). Check with `uv run pytest tools/tests/test_p0_3_r1.py`.
- In any GitHub Actions workflow you are allowed to touch, a step `name:` that contains `: ` must be quoted, and you run `actionlint` on the edited workflow before committing. Phases other than the B2 test-author do not edit workflows.
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.

Your rows: `compiler/tests/test_b2_t4_*.py` and `compiler/tests/test_b2_r5_reference_*.py`, `compiler/tests/data/b2_t4/**`, and in `.github/workflows/pr.yml` the single `uv run pytest tools/tests/test_p0_5_*.py …` line of the `reference-python` job: append your test file paths to that pytest command and change nothing else. Make this edit **only after** the CI fix and the D3 lv2lint change have merged (the orchestrator confirms), quote any step name containing `: `, and run `actionlint .github/workflows/pr.yml` before committing.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit `compiler/pme_compiler/` or `reference/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (B2-R1 to B2-R7, see docs/agents/briefs/B2-implementer.md). You verify B2-R3 and B2-R5 through B2-T4.

## Tests that must pass

From the card: **B2-T4** a ribbed rectangle against the P0.5 plate: first 20 modes within 2%.

### How to build them

pytest, names `test_b2_t4_…` (part 2) and `test_b2_r5_reference_…` (part 1); no `,` `*` `?` `\` `"` `;` in parametrised ids. Load `pme_reference` exactly as tools/tests/test_p0_5_r7_python.py does (skip when `PME_REFERENCE_PYDIR` is unset, fail when `PME_REFERENCE_REQUIRED=1` and the module is missing).

**Part 1 (now): unribbed cross-check, `test_b2_r5_reference_…`.** The P0.5-T4 plate (spruce-like, 0.6 m × 0.4 m, h = 3 mm, grain along x), simply supported. Reference: the 20 lowest frequencies from the reference's discrete operator, extrapolated to h → 0 (three grids with a constant ratio, Richardson), or from `plate_scheme_frequency_hz` on a fine grid with its error stated. B2: `pme_compiler.body` with `"simply_supported"`. Pass: the first 20 modes within 2% (the B2-T4 tolerance; B2-T1 already checks 0.5% against the analytic values). This exercises the comparison path that part 2 reuses.

**Part 2 (after the reference supports ribs): `test_b2_t4_…`.** The same plate with ribs (for example three ribs of spruce, 20 mm × 15 mm section, across the grain, parallel to y, at x = 0.15, 0.3, 0.45 m), simply supported edges. The ribs must be expressed identically in both models (same E, section, density, positions); state any modelling difference (beam on the plate's mid-plane versus offset, smeared strip versus beam) in the test docstring. Reference values extrapolated as in part 1. Pass: |f\_B2/f\_ref − 1| ≤ 0.02 for each of the first 20 modes, matched by sorting (state how you pair modes if two are within 2% of each other). Report per-mode errors.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: the tests pass locally with the reference module built (`cmake --preset linux-release -DPME_BUILD_PYTHON_BINDINGS=ON`, then `PME_REFERENCE_PYDIR=… uv run pytest compiler/tests/test_b2_t4_*.py compiler/tests/test_b2_r5_reference_*.py`), skip cleanly without it, are ruff and mypy clean, and follow the naming rule. If a comparison fails, do not weaken it: report the measured values.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `B2: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B2`, `role` `test-author`, `model` `sonnet`; `requirements` an empty object; `tests` B2-T4 (`not run`, with the reason while the reference lacks ribs) and the part 1 test, with per-mode errors in `notes`; `outputs` your files; `pull_requests` empty.
