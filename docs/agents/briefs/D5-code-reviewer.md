# Brief: D5 Instrument and preset management — code-reviewer

Model: sonnet
Branch: review the D5 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

where instruments, presets and compiled body data live on each system, and how they are imported, migrated and cached.

Your job: decide whether the D5 implementer's work meets the D5 card, the Software guidelines and docs/agents/briefs/D5-implementer.md, and return a verdict. Check yourself that the cache key covers exactly the inputs of `compileBody` and `buildLocalBridge` and nothing sample-rate dependent, that no corrupted blob or cache entry can ever be returned, and that no test touches the real user folders.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **D5 Instrument and preset management**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-INST — docs/plan/engine-specification.md, "Instrument definitions", paragraph "Instrument file".
- SPEC-API "Descriptions", "Parameter classes", "Compile functions"; SPEC-BODY "Local bridge model".
- docs/agents/briefs/D5-implementer.md (scope, shared files, the location table, the preset format and version 0, the cache key and dependency table, the test setups).
- docs/dev/instrument-format.md (B1) and `engine/src/runtime/loader/include/pme/loader/*.hpp`; docs/dev/clap-shell.md ("State (D1-R4)", "Provisional identifiers (D-3)"); `engine/include/pme/body.hpp`, `instrument.hpp`, `params.hpp`.
- docs/adr/ADR-0003, ADR-0005.
- `.claude/agents/code-reviewer.md` — your checklist.
- The implementer's handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `D5: ` (`git log --grep '^D5: ' --name-only`).
- CI evidence: given by the orchestrator after merging (the `plugin (linux|macos|windows)` jobs with the `^D[15]-` filter). Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 (300b995; ICR-0001). D-3 open: the data directory name, preset extension and preset `format` value are provisional and must live only in `ids.hpp`.

## Scope

- May edit: test sources under `plugin/common/library/tests/` — only to add missing or independent tests for D5-T1 to D5-T4 or D5-R1 to D5-R5. Commit with messages starting `D5: `. Do not push.
- Read only: everything else. You MUST NOT change implementation, workflows, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. D5-R1 Install locations for bundled instruments and user data on each system.
2. D5-R2 Preset format: instrument reference plus parameter overrides, with schema version.
3. D5-R3 Import and export of instruments and presets; migration through B1.
4. D5-R4 Hash-keyed cache of compiled body data in the user cache directory.
5. D5-R5 Blob integrity checks by hash on load.

## Tests that must pass

- **D5-T1** presets round-trip;
- **D5-T2** an old preset migrates;
- **D5-T3** a structural edit invalidates exactly the affected cache entries;
- **D5-T4** a corrupted blob is detected and reported.

## Review checklist for D5

**Scope and process**

1. D5 commits touch only `plugin/common/library/**`, docs/dev/library.md, docs/user/files.md, and the shared-file edits of the brief: one line at the end of `plugin/common/CMakeLists.txt`, one appended block in `ids.hpp` (nothing existing changed), the ctest filter `^D[15]-` in D1's `plugin` job (nothing else in pr.yml, no new job), one sentence in docs/dev/ci.md. Nothing under `engine/`, `compiler/`, `instruments/`, `plugin/clap/`, `plugin/lv2/`, `cmake/dependencies.cmake`, `pyproject.toml`. No new third-party dependency. `uv run pytest tools/tests/test_p0_2_r7.py` passes.
2. Every D1 test passes unchanged; the default build (`PME_BUILD_PLUGINS=OFF`) configures, builds and passes ctest.

**Correctness (blocking if violated)**

3. D5-R4 key: read the key encoding field by field against `compileBody(BodyParams, bridgePoints, channels)` and `buildLocalBridge(body, point, maxModes, fSplit)`. Every input is in the key (every `BodyParams` and `PlateParams` member, ribs, outline, thickness grid, bridges, `modalTableHash`; bridge-point and channel positions in order; point index, effective `maxModes` and `fSplit`); nothing else is (no sample rate, no string, exciter, damper, tuning or `strings.fMax`, no continuous parameter). Doubles by bit pattern, explicit little-endian; a guard catches a new struct member. Write a scratch check: change each `PlateParams` member in turn and confirm the body key changes.
4. Nothing sample-rate dependent is stored (SPEC-INST): the serialised payloads hold only `BodyModes` and `LocalBridge` members.
5. D5-R5: `loadLibraryInstrument` always verifies blobs (B1 `verifyBlobs = true`) and returns nothing on failure; cache reads check magic, versions, key, length, payload SHA-256 and internal length consistency, and a bad entry is deleted and recomputed, never returned. Independent check in scratch: corrupt 100 random bytes or truncation points of a cache entry; each read must report and recompute.
6. Atomic writes everywhere (temp file or directory + rename); a failed import leaves no partial directory; concurrent `getOrCompute` on one key leaves one valid entry. `pruneCache` deletes only under `cache/body/`.
7. Paths: library ids cannot escape their root (`..`, absolute paths, drive letters, backslashes); sanitised directory names; Windows known folders via `SHGetKnownFolderPath`, XDG defaults on Linux, the macOS folders of the brief; a missing `HOME` is an error, never a guess. No test reads or writes the real user folders (grep the tests for `defaultLocations()` used without overridden environment).
8. D5-R2: preset keys are registry ids; unknown and retired ids ignored with a diagnostic, values clamped, non-finite rejected; newer `presetSchemaVersion` rejected without loading anything; canonical output byte-stable; `instrument.hash` mismatch is a warning, not a failure. Version 0 → 1 migration as specified (dotted names to ids, bare instrument string to object). D1's `state.hpp` and state format unchanged.
9. D5-R3: import uses B1's loader and `writeInstrument` (no second parser, no second SHA-256); a version-0 instrument is stored as canonical version 1; import is idempotent; export re-imports to the same hash.
10. Public API never throws (filesystem and JSON exceptions caught); every failing path returns `Status` plus errors with a path and a documented code.

**Tests and CI**

11. D5-T1 to D5-T4 exist under those names and match the brief's setups, including every row of the D5-T3 dependency table as its own subcase and the producer-version row; D5-T4 covers instrument blob corruption, blob deletion and the three cache-entry corruptions. Run them yourself in `linux-debug` and `linux-rt-safety` with `-DPME_BUILD_PLUGINS=ON`, with GCC and Clang, and once under an address-sanitizer preset.
12. Provisional identifiers (D-3): `git grep` the data directory name, the preset extension and the `format` string; they appear only in `ids.hpp` (tests may use the constants).

**Quality**

13. clang-format and clang-tidy clean on `plugin/common/library/`; docs/dev/library.md covers every item the implementer brief lists (SPEC sections, locations, ids, preset format and version table, import/export, key encoding and dependency table, container format, pruning, integrity, error codes, concurrency, D-3, D6 hand-over); docs/user/files.md is written for players and matches the code.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For D5: no audio-thread code and nothing under engine/src (RT tests, coverage gate and baselines do not apply). There is no pull request; the orchestrator records your verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `D5`, `role` `code-reviewer`, `model` `sonnet`; `requirements` D5-R1 to D5-R5 as verified; `tests` D5-T1 to D5-T4 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
