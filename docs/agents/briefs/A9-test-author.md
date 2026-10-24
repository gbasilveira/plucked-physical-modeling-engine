# Brief: A9 Implicit bridge step and bridge numerics options — test-author

Status: **ready** once the A9 implementer's first commits with the implicit `BridgePoint` mode are merged (`git log --grep '^A9: '`).

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the second bridge scheme required by decision D-13 (owner answers on SI-0018 and SI-0021): an implicit bridge step with matched coupling, selectable against A4's explicit order; the explicit-mode stability check with its policy; and overdamped lumped bridge modes (SI-0020), each behind an engine setting.

Your job: write **ORC-06 in the implicit scheme** and the reference comparison **A9-T5**, independently of the A9 implementer. Derive every expected value from SPEC formulas, the exact one-step map you build yourself, or P0.5.

## Read first

- docs/plan/phasing-plan.md: phase cards **A9**, **A4**, **P0.5**; Software guidelines (Testing policy); **Roles** (Oracle author).
- SPEC: SPEC-BRG "Bridge scheme", "Small-admittance results", "Stability of the explicit order"; SPEC-ORC row "Bridge damping" (both criteria, verbatim below); SPEC-CONV (e^(+iωt), SI-0012).
- docs/spec-issues/SI-0021 (the planner's numbers per scheme), SI-0018, SI-0019.
- `.claude/agents/test-author.md`; engine/tests/oracles.md; docs/dev/testing.md; docs/dev/signal-analysis.md.
- The A4 oracle files in engine/tests/oracles/bridge/ (`test_orc06_orc07.cpp`, `bridge_rig*.hpp`, `test_a4t3_reference.cpp`). **Read only:** you add new files `test_a9_*.cpp` and may include A4's helpers, but you do not edit them.
- A9's headers only (engine/src/bridge/include/pme/bridge/); never the `.cpp` files. docs/dev/reference-solver.md; reference/README.md "Limits".

## Inputs

- A4 complete (its explicit ORC-06, ORC-07 and A4-T3 pass); the A9 implementer's commits; P0.5 (103badf).
- Frozen interfaces: interfaces-v2 = 300b995.
- **Reference scope:** b₂ = 0; no `detuneH`, no `At0`; lumped bridge; θ\_b with the SI-0019 rotation. The reference is itself an implicit θ-scheme of second order: use fine grids or `pme_reference_cli refine` extrapolated values, as A4-T3 does.

## Concurrent work: A5, A7, A8 and A9 run at the same time

A5, A7 and A8 (each with its test-author where briefed) run in separate git worktrees, and A9 (implicit bridge step, D-13), each with its test-author where briefed. The orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. B2 and D5 follow-ups (in review) and an interface agent for ICR-0002 (once approved) may also be committing. Edits must not overlap. **This table is identical in every A5, A7, A8 and A9 brief.**

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/exciters/**` (replaces `exciters_placeholder.cpp`; internal headers in `engine/src/exciters/include/pme/exciters/`), `engine/tests/exciters/**`, `docs/dev/hammer.md` | A5 implementer | read only |
| `engine/tests/oracles/exciters/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-09 and ORC-19, and an appended note in the test-location cell of ORC-08 | A5 test-author | read only |
| New files only: `engine/src/string/longitudinal*` and `engine/src/string/include/pme/string/longitudinal*`, `engine/src/nonlinear/tension*` and `engine/src/nonlinear/include/pme/nonlinear/tension*`; `engine/tests/geometric/**`; `docs/dev/geometric.md` | A7 implementer | read only |
| `engine/tests/oracles/geometric/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-11 and ORC-12 | A7 test-author | read only |
| `engine/src/simd/**` (replaces `simd_placeholder.cpp`; internal headers in `engine/src/simd/include/pme/simd/`), `engine/tests/simd/**`, `docs/dev/simd.md`, one new file `tools/bench/baselines/<machine-id>-a8.json` | A8 implementer | read only. **No Highway include outside `engine/src/simd/`** (ADR-0001). A8 adds kernels only and wires nothing into osc, string, bridge or nonlinear (C1 composes them). Its kernels are generic primitives (dot products, mode update, rank-one update) that serve **both** bridge schemes; A8 MUST NOT encode the explicit coupling order (for example a fused "bridge force, then advance with δ\_tt w" kernel) in a way that the implicit scheme cannot reuse. |
| `engine/src/bridge/**`, `engine/tests/bridge/**`, `engine/tests/oracles/bridge/**`, `docs/dev/bridge.md` | A9 implementer (and A9 test-author for `engine/tests/oracles/bridge/test_a9_*` and new files there; the existing A4 oracle files stay as they are) | read only: call the A4 internal headers, never edit them. **Only A9 edits engine/src/bridge while these phases run.** A5 and A7 use `BridgePoint`, `ModalSegment`, `bridgeForce` and the hooks as they are. |
| Existing files of `engine/src/string/**` (`string_model.*`), `engine/src/nonlinear/**` (`sav.*`), `engine/src/osc/**`, `engine/src/dampers/**`, `reference/**`, `tools/reference-cli/**`, `engine/tests/{osc,string,nonlinear,dampers,oracles/osc,oracles/string,oracles/nonlinear,oracles/dampers}/**` and their module notes | A1, A2, A3, A6, P0.5 (complete; follow-ups only when the orchestrator routes them) | read only; call their internal headers |
| `engine/include/pme/**`, `engine/tests/interfaces/**`, `tools/params/**`, `params.json`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime` except the B1 loader, `barriers`, `sched`) | nobody (frozen at interfaces-v2 = 300b995; ICR-0002 is proposed and will be applied by an interface agent) | read only |
| `compiler/**`, `plugin/**`, `engine/src/runtime/loader/**` | B2, D5 follow-ups, B1 (complete) | read only |
| `.github/workflows/**`, `tools/ci/**`, `tools/trace/**`, `tools/tests/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in the order A5, A7, A8, A9.** Later merges rebase onto earlier ones. Keep every block self-contained, so the order does not change behaviour.

| Shared file | A5 | A7 | A8 | A9 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A5: hammer and felt ----` … `# ---- end A5 ----` appended after the last line | one block `# ---- A7: geometric nonlinearity ----` … `# ---- end A7 ----` appended | one block `# ---- A8: SIMD kernels ----` … `# ---- end A8 ----` appended | none (tests go into engine/tests/bridge/, already built by A4's block) |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends `add_subdirectory(exciters)` | test-author appends `add_subdirectory(geometric)` | none | none (the A9 test-author appends its `test_a9_*` registrations to `engine/tests/oracles/bridge/CMakeLists.txt`) |
| `docs/dev/interfaces.md`, the sentence under "Stubs (P0.4-R3)" | move `exciters` out of the placeholder list, add "`exciters`: hammer by A5 (plucked exciters P1)" | none | move `simd` out of the placeholder list, add "`simd`: implemented by A8" | none |
| `engine/tests/oracles.md` | rows ORC-08 (location note), ORC-09, ORC-19 only | rows ORC-11, ORC-12 only | none | ORC-06: an appended note in the test-location cell (A9 test-author) |

**Rules for every agent in these phases.**

- **Test names** start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;`. Check with `uv run pytest tools/tests/test_p0_3_r1.py`. **Every requirement ID and every test ID of your card needs at least one test whose name starts with that ID** (the traceability job, docs/dev/traceability.md, fails a complete phase that lacks one).
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.
- **Disk space is limited.** Build one preset at a time. Before configuring another preset, delete the previous build directory (`rm -rf build/<preset>`). Never keep more than two build trees. Delete scratch programs and their builds from your scratchpad when you are done. The reference-comparison tests need an optimised preset (`linux-release` or `linux-rt-safety`), so build it last, by itself.
- **Engine settings (D-13).** When SPEC offers alternatives, implement each behind an option (SPEC-CONV "Quality and numerical options"). Until ICR-0002 is applied, options are internal enums whose names and values match ICR-0002. Never pick one alternative silently.
- If something cannot be done as written (a tolerance not reached, an instability, SPEC unclear), stop that item. Do not loosen a tolerance or change the scheme. Describe it in the report with measured values; the planner files a spec issue.

Your rows:

- new files `engine/tests/oracles/bridge/test_a9_*.cpp` and their registration in `engine/tests/oracles/bridge/CMakeLists.txt` (append-only lines; that file is otherwise A4's);
- in `engine/tests/oracles.md` an appended note in the test-location cell of row ORC-06 ("; implicit scheme (A9): …").

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit engine/src/ or reference/.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A9-R1 to A9-R6, docs/agents/briefs/A9-implementer.md). You verify them through ORC-06 (implicit) and A9-T5.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-06 | Bridge damping ✓ | Flexible string on a pure conductance G, every kept mode | σ = −f₀ ln\[(1 − Z₀G)/(1 + Z₀G)\] | Modes with ω\_nΔt ≤ 0.1: 0.1% against the formula (run: 10⁻⁴). Every kept mode: measured decay rate equals that of the exact eigenvalues of the scheme's one-step coupled map within 10⁻⁴ relative; the deviation from the formula is reported per mode, not bounded (SI-0021). Implicit bridge scheme (D-13): every kept mode within 0.1% of the formula at Z₀G = 0.01; at Z₀G = 0.05 within 0.1% for ω\_nΔt ≤ 2.4 and 0.5% for every kept mode |

From the card: **A9-T5** against P0.5, the A4-T3 setup in the implicit scheme matches partial frequencies within 0.05 cent and decay rates within 2%.

### How to build them

Doctest, names starting with the ID and a space; labels `oracle`, `unit` (A9-T5: `integration`, reference part only in optimised builds).

**ORC-06 (implicit).** Use A4's ORC-06 string: flexible, B < 10⁻⁸, lossless, pinned, 48 kHz, all kept modes, `AtL`, pure conductance, θ\_b = 0, G\_h = 0. Run Z₀G = 0.01 and 0.05 through A9's implicit point.

For every kept mode:

1. the decay rate from the exact eigenvalues of the implicit one-step map, which you build from the SPEC-BRG "Bridge scheme" text yourself, as A4's test-author did for the explicit map;
2. the measured decay rate of a float32 run (fit with `pme_analysis`), which must equal (1) within 10⁻⁴ relative.

Pass, for (1) against the formula σ = −f₀ ln[(1 − Z₀G)/(1 + Z₀G)]:

- Z₀G = 0.01: every kept mode within 10⁻³;
- Z₀G = 0.05: modes with ω\_nΔt ≤ 2.4 within 10⁻³ and every kept mode within 5 × 10⁻³.

Report the worst mode per case. Expected from the planner's check: 1.7 × 10⁻⁴ and 4.3 × 10⁻³. If your map disagrees with the implementation, report both and do not adjust either.

**A9-T5.** A4-T3's setup (two plain strings ≈ 196 and 262 Hz, b₁ = 0.5, b₂ = 0, b₃ = 2 × 10⁻⁹, mass–spring–damper bridge as A4-T3 chose; θ\_b = 0 and the θ\_b = 0.3 asymmetric case) with A9's implicit point. Same reference runs, analysis and pass rule as A4-T3: 0.05 cent and 2% for the first 8 partials of each string. Report per-partial errors next to A4-T3's explicit values.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

## Working rules

Commit with `A9: ` and your session's attribution lines; do not push, tag or open a pull request; stage by name; report tests `not run` with local evidence. Never weaken a failing test.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A9`, `role` `test-author`, `model` `sonnet`;
- `requirements` empty;
- `tests` ORC-06 (implicit) and A9-T5 (`not run`), with per-mode and per-partial numbers in `notes`;
- `outputs` your files.
