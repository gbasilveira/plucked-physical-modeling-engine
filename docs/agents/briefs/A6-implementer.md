# Brief: A6 Dampers and pedals — implementer

Status: **ready (pending CI on A2/A3/B1)**. A2 and A3 are approved by review and wait only for one fully green pr.yml run; start now from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief. If their CI run forces a change in `engine/src/string/` or `engine/src/nonlinear/`, rebase onto it.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A6-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

damper felt models and the pedal and key logic that drives them.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Real-time rules, Numerics rules, Testing policy); the phase card **A6 Dampers and pedals**; for context the cards **A2**, **A3** (your inputs), **C1** (composes dampers into the note-group loop) and **C4** (turns events into your inputs).
- SPEC and PLAN sections:
  - SPEC-DMP — docs/plan/engine-specification.md, "Dampers and pedals" (all of it, including the SI-0016 sostenuto rule)
  - SPEC-OSC — "The modal oscillator" ("Time-varying parameters", "Bandwidth", "Discrete energy")
  - SPEC-NL — "Nonlinear framework" ("Discrete scheme", "Explicit low-rank solve", "Skipping") for the rank-one damper
  - SPEC-STR — "Forces and observations" (raised-cosine window φ̄)
  - SPEC-EXEC — per-sample step 10 ("Advance damper engagement ramps and refresh coefficients if any ramp is active"); "Parameter changes"
- engine/include/pme/dampers.hpp (frozen `DamperParams`, `DamperMode`), engine/include/pme/instrument.hpp (`SegmentDesc::damper`, `Defaults::halfPedalP0`, `halfPedalP1`), engine/include/pme/events.hpp (`NoteOn`, `NoteOff::releaseVelocity`, `KeyDepth`, `Pedal`), params.json (`pedal.*`, `damper.*`).
- Internal headers you call: `engine/src/osc/include/pme/osc/{oscillator,mode_bank,ramp}.hpp` (A1: `compileOscillatorF64`, `exceedsRampThreshold`, `planRamp`, `applyRampsAt`), `engine/src/string/include/pme/string/string_model.hpp` (A2: `windowedShapes`, `windowFactor`), `engine/src/nonlinear/include/pme/nonlinear/sav.hpp` (A3: `Dashpot`, `savStep`, `SavSegment`); docs/dev/osc.md, docs/dev/string.md, docs/dev/nonlinear.md.
- docs/spec-issues/SI-0016 (sostenuto thresholds). docs/adr/ADR-0006.
- docs/dev/testing.md, docs/dev/signal-analysis.md, docs/dev/rtcheck.md; docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A2** (merged at b80a7d4; approved, pending CI): `pme_string`, `windowedShapes`; **A3** (merged at 352bbfe, test-author adb9479, review tests 7961138; approved, pending CI): `pme_nonlinear`, `savStep`, `Dashpot`, `lowRankSolve`; through them **A1** (`pme_osc`, ramps).
- Frozen interfaces: **interfaces-v2 = 300b995**. If `interfacesVersionMajor` is not 2, stop and report.
- Current state of your module: `engine/src/dampers/dampers_placeholder.cpp` (no stub function).
- Carried follow-ups from the A3 review (owned by A3, not you): a debug assert in `alongGradient`, float-build coverage tests, one sentence on small negative ψ. Do not edit `engine/src/nonlinear/`.
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

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A6: dampers and pedals ----
target_include_directories(pme_dampers PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/dampers/include")
target_link_libraries(pme_dampers PUBLIC pme_string pme_nonlinear pme_osc)
add_subdirectory(engine/tests/dampers)
# ---- end A6 ----
```

## Scope

- May edit (create or change): the paths the ownership table gives the **A6 implementer**, plus your root `CMakeLists.txt` block and your `docs/dev/interfaces.md` words.
- Read only: everything else (in particular `engine/src/nonlinear/`, `engine/src/osc/`, `engine/src/string/`: call them, do not change them).
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A6-R1 Diagonal damper model with ramped coefficient refresh.
2. A6-R2 Rank-one damper as one spring–dashpot pair in the low-rank solve.
3. A6-R3 Lift fraction from key and sustain pedal, half-pedal region, η\_d shaping.
4. A6-R4 τ\_up and τ\_down timing with release velocity.
5. A6-R5 Sostenuto latch and per-key hasDamper flags.

### How to meet each requirement

Internal headers in `engine/src/dampers/include/pme/dampers/*.hpp`, namespace `pme::dmp`; delete `dampers_placeholder.cpp`. Every formula cites `// SPEC-DMP <paragraph>`. Configuration (non-audio, allocates) and the per-sample or per-sub-block path (audio thread: `noexcept`, no allocation, lock, exception or I/O) are separate. Write state-touching code as templates on the state type `S` where it helps tests run in float64.

**Shape.** The pad acts on the **vertical** polarisation through φ̄\_d,n = φ\_n(x\_d) W(k\_n) with x\_d = `xRatio`·L and the raised-cosine window of width `width` (SPEC-DMP "Damper model"; SPEC-STR "Forces and observations"). Compute it with A2's `windowedShapes(modes, xd, WindowKind::RaisedCosine, width, out)` at configuration time; keep a float64 copy for coefficient work (compute it with `windowFactor` and the point shape in float64, not from the float32 output).

**A6-R1 Diagonal (default).** For engagement e, per mode (SPEC-DMP "Diagonal"):

σ\_n,d = σ\_n + e R\_d φ̄²\_d,n / (2ρA), ω²\_n,d = ω²\_n + e K\_d φ̄²\_d,n / ρA

(σ\_n, ω\_n from `StringModes`; the vertical polarisation only; the horizontal polarisation is untouched). Recompute β, κ, γ with `compileOscillatorF64` in float64 and apply them under SPEC-OSC "Time-varying parameters": the state is kept; a change larger than 0.1% per sub-block is ramped linearly over the sub-block with A1's `exceedsRampThreshold` / `planRamp` / `applyRampsAt`. `compileOscillatorF64` does no allocation, so the refresh may run on the audio thread once per sub-block while e moves; document the cost per mode.

The mode count is fixed at compile time, so a damper must not drop modes. **Check overdamping:** report, for the test strings and for every damper of `instruments/examples/generic-grand-88` at e = 1, the largest ratio σ\_n,d/ω\_n,d and whether any mode has σ\_n,d ≥ ω\_n,d or fails `compileOscillatorF64`. If one does, stop that item, do not clamp or skip silently, and report the parameters; the planner files a spec issue (the likely resolution is the exact overdamped continuation of SPEC-OSC's coefficients).

**A6-R2 Rank-one (MAY in SPEC, required by the card).** The spring K\_d acts on the centred average μ\_t· of the pad displacement y = **c**ᵀ**z**, **c** = φ̄\_d over the vertical modal coordinates (0 on lumped coordinates), and the dashpot R\_d on δ\_t· y, both scaled by e (SPEC-DMP "Rank-one"). In SPEC-NL's low-rank solve this is one pair, **h** = γ ⊙ e**c**/m, **w** = **c**(K\_d/2 + R\_d/(2Δt)). Use A3's API without changing it: since e K\_d μ\_t· y = e K\_d Δt δ\_t· y + e K\_d yⁿ⁻¹, the pair equals

- one `pme::nl::Dashpot` along **c** with R = e (R\_d + K\_d Δt) (its pair is h = Rγ⊙**c**/m, w = **c**/(2Δt), so hwᵀ carries e(K\_d/2 + R\_d/(2Δt)) as SPEC writes), plus
- an explicit force −e K\_d **c** (**c**ᵀ**z**ⁿ⁻¹) added to `explicitForce` before `savStep`.

Derive this in docs/dev/dampers.md and test it against a direct implementation of SPEC's pair through `lowRankSolve` (A6-R2 test below). The damper's energy (for A6-T1, constant e) is E\_d^(n+½) = (e K\_d/4)[(**c**ᵀ**z**ⁿ⁺¹)² + (**c**ᵀ**z**ⁿ)²]; total energy = Σ ρA H\_n + E\_d (+ ½ψ² − ε₀ when a potential is active). The damper counts towards SPEC-NL's limit of 3 dashpot directions per segment.

**A6-R3 Lift fraction** (SPEC-DMP "Lift fraction"): l = max(l\_key, l\_ped), l\_ped = clamp((P − P₀)/(P₁ − P₀), 0, 1), e\_target = (1 − l)^η\_d, with P₀, P₁ from `Defaults::halfPedalP0/P1` (registry `damper.halfPedalStart/End`), η\_d = `DamperParams::etaD`. l\_key = 1 while the key is down, or the last `KeyDepth` depth when the controller sends one; 0 after `NoteOff` unless latched. Validate P₀ < P₁ at configuration (`InvalidArgument`).

**A6-R4 Timing** (SPEC-DMP "Timing"): e follows e\_target with a one-pole time constant, τ\_up = `tauUp` while lifting (e\_target < e) and τ\_down = τ₀/(1 + k\_r ν\_rel) while landing (e\_target > e), ν\_rel from the last `NoteOff`. Discretise exactly, eⁿ⁺¹ = e\_target + (eⁿ − e\_target) exp(−Δt/τ), per sample (SPEC-EXEC step 10), and refresh coefficients per sub-block from e at the sub-block end with the A6-R1 ramp. Document the per-sample versus per-sub-block split.

**A6-R5 Sostenuto and hasDamper** (SPEC-DMP "Sostenuto", "Undamped notes", SI-0016): the pedal is down while its depth ≥ 0.5; at the sample it goes down, every key with l\_key ≥ 0.5 is latched with l\_key = 1 until the pedal falls below 0.5, whatever its key events; keys pressed later behave normally. A segment whose `DamperParams::hasDamper` is false (or that has no damper) always has l = 1 (e = 0). Keep per-key state for keys 0 … 127 in preallocated arrays.

Provide a small controller (for example `PedalState` plus a per-key `KeyDamperState`) that takes NoteOn, NoteOff (with release velocity), KeyDepth and Pedal (Sustain, Sostenuto) inputs at sample offsets and yields eⁿ per key; C4 will route real events into it. The Soft pedal is not yours (A5).

## Tests that must pass

- **ORC-13** (Damper (diagonal)): written by the A6 test-author (docs/agents/briefs/A6-test-author.md), not by you. Make it possible: the diagonal damper's coefficient computation and its use with a `ModeBank` must be reachable through your internal headers. Do not write tests named `ORC-13`.
- **A6-T1** the rank-one damper makes energy decrease monotonically;
- **A6-T2** table-driven tests of key, sustain and sostenuto sequences against expected lift states;
- **A6-T3** damping increases monotonically as pedal depth decreases through the half-pedal region;
- **A6-T4** engagement ramps produce no output step larger than the largest sample-to-sample change of the undamped signal.

### How each test is built

**A6-T1** (`engine/tests/dampers/`, label `unit`): a lossless string (b = 0, 40–80 modes, 48 kHz) started from a pluck-like displacement, rank-one damper at constant e = 1 (and e = 0.3), float64 states through `savStep` with no potential. Pass: Eⁿ⁺½ ≤ Eⁿ⁻½ (1 + 10⁻¹³) at every step over 2 s, and E decreases by more than 10× in that time for the chosen K\_d, R\_d. Report the decay.

**A6-T2**: a table of at least 15 sequences, each a list of timed inputs (NoteOn, NoteOff with ν\_rel, KeyDepth, Sustain depth, Sostenuto depth) and expected (l\_key, l\_ped, l, latched) per key after each input: key alone; sustain alone; half pedal at P = 0.35, 0.5, 0.65; sostenuto pressed with keys held then released; key pressed after sostenuto (not latched); NoteOff of a latched key; sostenuto released with the key still held; a key with hasDamper = false; KeyDepth half-key control; sostenuto at depth 0.49 and 0.5 (SI-0016 thresholds). Expected values are written by hand from SPEC-DMP, not computed by the code under test.

**A6-T3**: for P from 1 down to 0 in steps of 0.01 with no key held, the steady-state e and the resulting σ\_n,d of the first five modes (diagonal model) are non-decreasing as P decreases, constant outside [P₀, P₁], and strictly increasing inside it.

**A6-T4**: a sounding string (float32 engine path) with the diagonal damper, and separately the rank-one damper, landing (e: 0 → 1 with τ\_down at ν\_rel = 0 and 1) and lifting (1 → 0). Output = displacement observed at a registered point. Let D = max\_n |yⁿ⁺¹ − yⁿ| of the same string without a damper over the same window. Pass: max\_n |yⁿ⁺¹ − yⁿ| with the damper engaging ≤ D (state any margin you need, with the reason, and do not exceed 1% without reporting it).

### Other tests (unit label)

- `A6-R1 …`: σ\_n,d and ω²\_n,d against the formulas (10⁻¹² relative); only the vertical polarisation changes; a sub-0.1% change is applied without a ramp and a larger one with a linear ramp over the sub-block (coefficients at each sample against the expected interpolation).
- `A6-R2 …`: the Dashpot-plus-explicit-force form equals SPEC's pair solved directly with `lowRankSolve` (10⁻¹² relative over 1000 steps, float64).
- `A6-R3 …`, `A6-R4 …`: e\_target formula; the one-pole response reaches 1 − e⁻¹ of a step after τ (to 10⁻¹² in float64); τ\_down shortens with ν\_rel as τ₀/(1 + k\_r ν\_rel).
- `A6-R5 …`: hasDamper = false gives e = 0 whatever the inputs.
- RT-safety: the controller and the damper per-sample and per-sub-block paths run under `pme_rtcheck` without allocation or blocking.
- Benchmark `engine/tests/dampers/dampers_bench.cpp` (`pme_add_benchmark`, label `perf`): coefficient refresh of 80 modes per sub-block, and one rank-one damper step on 80 modes. No baseline file (D-4, V2); report the numbers.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A6:

- Module note: docs/dev/dampers.md — SPEC-DMP sections implemented (and the SPEC-OSC, SPEC-NL parts used); the internal API for C1 and C4; the rank-one derivation; the per-sample/per-sub-block split; the overdamping check results; SI-0016.
- In docs/dev/interfaces.md move `dampers` out of the placeholder list as the ownership table says.
- Coverage: `engine/src/dampers` is part of the 90% engine/src line-coverage gate.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A6: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` with GCC and with Clang; the rt-safety preset's tests; `uv run pytest tools/tests/test_p0_3_r1.py`.
- If something cannot be done as written (a tolerance not reached, an overdamped mode, SPEC unclear), stop that item, do not loosen a tolerance, and describe it in the report with the measured values; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, presets, A6-T1 decay and the worst step-to-step energy ratio, A6-T4 measured step versus D, the overdamping ratios, benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A6`, `role` `implementer`, `model` `sonnet`; `requirements` A6-R1 to A6-R5; `tests` A6-T1 to A6-T4 (ORC-13 is the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0016 plus any new defect; `open_risks` as found.
