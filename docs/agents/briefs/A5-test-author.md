# Brief: A5 Hammer and felt — test-author

Status: **ready** once the A5 implementer's first commits with `HammerPotential` and `StruckNote` are merged (`git log --grep '^A5: '`). P0.5 is merged, so the reference half of A5-T2 can be prepared now. Write ORC-09 first.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the piano exciter: velocity map, multi-string felt contact, dissipation, release, repetition and una corda.

Your job: write independently of the A5 implementer, deriving every expected value from SPEC formulas, closed-form results or the P0.5 reference solver, never from the implementation's output:

- the SPEC-ORC oracles the A5 card names, **ORC-09** and **ORC-19**;
- the reference-solver comparison **A5-T2**;
- the hammer-potential case of **ORC-08** that A3 left to A5.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase cards **A5 Hammer and felt**, **A3** and **P0.5 Reference solver**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-HAM — docs/plan/engine-specification.md, "Hammer and action" (contact, felt dissipation, una corda, Oracle)
  - SPEC-NL — "Nonlinear framework" (the conserved energy: Σ m\_i H\_i + ½(ψ^(n+½))² − ε₀)
  - SPEC-BRG — "Discrete coupling order", "Unison coupling"; docs/dev/bridge.md "Energy of the partitioned scheme"
  - SPEC-ORC — rows "Impact time", "Hammer–string energy", "Una corda aftersound"
  - PLAN-VAL — docs/plan/implementation-plan.md, "Validation and testing"
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md ("Oracle registry", tolerance macros); docs/dev/signal-analysis.md (golden features, `pme_golden_compare`).
- Headers only, never `engine/src/exciters/*.cpp`:
  - the A5 internal headers under `engine/src/exciters/include/pme/exciters/`;
  - `sav.hpp`;
  - the A4 bridge headers;
  - `string_model.hpp`;
  - engine/include/pme/exciters.hpp.
- engine/tests/oracles/nonlinear/test_orc08_impact_time.cpp (A3's rigid-wall ORC-08) and engine/tests/oracles/bridge/ (how A4-T3 drives `pme_reference_cli` in optimised builds only).
- For A5-T2:
  - docs/dev/reference-solver.md, especially "Contacts", "Accuracy of the reference itself" and "Using the reference from later phases";
  - reference/README.md ("Scenario format": `hammers[]` with `mass K p mu width strike_x velocity gap strings[] wall`; observation kinds `hammer_force`, `hammer_penetration`, `hammer_position`; "Limits");
  - reference/scenarios/hammer_two_strings_bridge.json.

## Inputs

- Completed dependencies and their outputs:
  - A2 (`pme_string`), A3 (`pme_nonlinear`), A4 (`pme_bridge`), A1 (`pme_osc`), G0 (`pme_test_framework`, `pme_analysis`);
  - the A5 implementer's commits;
  - **P0.5** (merged 103badf): `reference/`, `pme_reference_cli`, the `refine` subcommand.
- Frozen interfaces: interfaces-v2 = 300b995.
- **Reference scope** (docs/status/phase-status.md, P0.5 row): b₂ = 0 only; no `sigmaOverride`, `detuneH` or `bridgeEnd = At0`; hammers strike the vertical polarisation; clamped ends only without a bridge. No una corda weights and no Stulov law in the reference: A5-T2 is elastic felt (μ = 0, the card's "elastic felt").
- **The reference is second order** in space and time. A contact duration or peak force at the 2% level needs a refinement ladder: three grids with a constant ratio (and the time step refined with the grid, `oversampling`), Richardson-extrapolated, with the observed order reported. The engine side has its own modal truncation error: report its sensitivity to f\_max (16 kHz and 20 kHz) and fs (48 and 96 kHz).

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

- `engine/tests/oracles/exciters/**` (new);
- in `engine/tests/oracles.md` the status and test-location cells of rows ORC-09 and ORC-19, and the test-location cell of row ORC-08 (append your hammer case; keep A3's text);
- one line `add_subdirectory(exciters)` appended at the end of `engine/tests/oracles/CMakeLists.txt`.

No root `CMakeLists.txt` block.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/` or `reference/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A5-R1 to A5-R7, see docs/agents/briefs/A5-implementer.md). You verify them through ORC-09, ORC-19, ORC-08 (hammer case) and A5-T2.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-08 | Impact time ✓ | Mass 8 g at 3 m/s on rigid felt contact, p = 2.5 | t\_c from the hammer oracle; rebound −v | max(1%, 1 sample); rebound 0.1% (run: 0.4%, exact rebound) |
| ORC-09 | Hammer–string energy ✓ | Lossless string and felt, float64 | Total energy including ½ψ² constant | 10⁻¹⁰ relative (run: 7 × 10⁻¹⁴) |
| ORC-19 | Una corda aftersound | Trichord, s = 1 | Passive string's bridge force starts at zero and grows over time, as in Miranda Valiente et al. | Qualitative, plus regression against the first accepted run |

From the card: **A5-T2** against P0.5 with elastic felt, peak force and contact duration within 2%.

### How to build them

All doctest, one `TEST_CASE` per case. Names start with the ID and a space and contain none of `,` `*` `?` `\` `"` `;`. Labels `oracle` and `unit` (A5-T2 and ORC-19: `integration`).

**ORC-08 (hammer case).**

- Setup: A5's `HammerPotential` in its fixed-target (rigid felt contact) form, mass 8 g at 3 m/s, p = 2.5, K = 4.5 × 10⁹ N/m^p, 48 kHz, through `savStep`.
- Expected t\_c from the SPEC-HAM closed form, computed in your test (Γ functions from `<cmath>`).
- Pass: |t\_c − t\_c,exact| ≤ max(1%, 1 sample), rebound speed within 0.1% of −v.
- Name it `ORC-08 impact time with the A5 hammer potential …`.

**ORC-09.**

- Setup:
  - one lossless plain steel string (b₁ = b₂ = b₃ = 0), pinned, 262 Hz, all modes below 16 kHz, rigid bridge (no bridge motion);
  - A5 hammer: m\_H = 8 g, K = 4.5 × 10⁹, p = 2.5, μ\_HC = 0, raised-cosine window b\_H = 10 mm at β\_c = 1/8, v\_H = 3 m/s;
  - `S = double`, 48 kHz, drift relaxation off (`SavParams::driftRelaxation = false`), run 0.1 s through the strike and rebound.
- Total energy: ρA Σ H\_n (SPEC-OSC discrete energy) + m\_H H\_hammer + ½(ψ^(n+½))² − ε₀, computed in your test from the state accessors (or A3's `savEnergy`, whose formula you check against SPEC-NL in a comment).
- Pass (amended 2026-10-12, SI-0027): up to the first ψ reset, max |E − E₀|/E₀ ≤ 10⁻¹⁰; over the whole run, max |E + E\_removed − E₀|/E₀ ≤ 10⁻¹⁰ with E\_removed the cumulative energy the resets removed (A5's `resetEnergyRemoved()`, checked against ½ψ² − ε₀ at each reset in your test), and every reset's removed energy ≥ 0.
- Second case: a trichord (three strings detuned by 0, +0.5, −0.5 cent, w = 1 each) on the same rigid bridge, same pass criterion.
- Report (not bounded) a third case: the trichord on a lossless mass–spring bridge (A4 `BridgePoint`, m = 0.3 kg, 300 Hz), with the energy of docs/dev/bridge.md plus the hammer and ψ terms. If its deviation exceeds 10⁻¹⁰, report the value (2.4 × 10⁻⁵ at 0.3 kg, ∝ 1/m, was measured: SI-0028, open for the owner; keep it reported, not bounded).

**ORC-19.**

- Setup:
  - a trichord (three identical plain strings, 220 Hz, small intrinsic loss b₁ = 0.3 s⁻¹, b₃ = 2 × 10⁻⁹ s, b₂ = 0) on one bridge point;
  - the bridge: a mass–spring–damper (m = 0.3 kg, 250 Hz, r\_b giving Z₀|Y| ≈ 0.02 near f₀; state the values), θ\_b = 0.1 rad;
  - una corda s = 1, `leavingString` = 0 (so w₀ = 0), hammer as in ORC-09 with μ\_HC > 0;
  - run 3 s in float32 (the engine instantiation) through `StruckNote`.
- Observable: the passive string's own bridge force F\_b,0(t) (vertical, from its modes and w).
- Qualitative checks:
  1. |F\_b,0| is exactly 0 until the first sample at which the bridge moves (it starts at zero; its own modes start at rest);
  2. its RMS envelope over 50 ms windows starts at least 60 dB below the struck strings' first window and rises by at least 6 dB above its own first window within 0.5 s, with the envelope's maximum after 0.1 s (amended 2026-10-12, SI-0030: the earlier 20 dB figure had no source and is not met at Z₀|Y| ≈ 0.02; report the measured rise; do not keep a 20 dB expected-failure case). SHOULD, reported and not gating: the rise within the first 100 ms grows monotonically with Z₀|Y| over 0.005, 0.01 and 0.02;
  3. the struck strings' bridge forces start immediately.
- Regression: write the envelope (window centre times and RMS values, the first 3 s) as a golden feature JSON `engine/tests/oracles/exciters/data/orc19_envelope.json` from your first run, and compare later runs with `pme_golden_compare`-style tolerances: 0.5 dB per window, which allows cross-platform float32 differences. Say in the file header that it is "the first accepted run", provisional until the owner accepts it after a re-run on the merged A5 code (SI-0030).
- Cite Miranda Valiente et al. (2024) in a comment.

**A5-T2** (engine/tests/oracles/exciters/, label `integration`; reference part compiled only into optimised builds, as A4-T3 does; each case under 60 s in its preset):

- Setup:
  - one plain steel string (f₁ ≈ 262 Hz, B ≈ 2 × 10⁻⁴, b₁ = 0.5 s⁻¹, b₂ = 0, b₃ = 2 × 10⁻⁹ s), pinned, fixed ends (no bridge);
  - hammer: m\_H = 8 g, K = 4.5 × 10⁹ N/m^p, p = 2.5, μ = 0 (elastic felt), strike at x\_c = L/8;
  - three hammer speeds: 1, 3 and 5 m/s;
  - two windows: point contact (`width` 0 in the reference and the engine's point shape φ\_n(x\_c)), and raised-cosine b\_H = 10 mm.
- **Check first that the reference's raised-cosine window is the same function as SPEC-HAM's**: same width convention, same normalisation, as A2's `windowedShapes` computes. If it is not, run only the point-contact case, report the difference, and the planner files a spec issue.
- Reference: render the `hammer_force` observation on three grids with constant ratio 2 (and `oversampling` scaled with the grid). Extrapolate the contact duration (zero crossings of the force, interpolated) and the peak force (parabolic interpolation around the maximum sample) with Richardson; report the observed orders and the extrapolation's own estimate.
- Engine: `StruckNote` (or the A5 potential with A3's `savStep` on the string's modes), float32 at 48 kHz and float64 at 96 kHz, f\_max 20 kHz; the same estimators.
- Pass: |peak\_engine/peak\_ref − 1| ≤ 0.02 and |t\_c,engine/t\_c,ref − 1| ≤ 0.02 for every speed and window, at 48 kHz.
- Report the per-case errors and the 96 kHz values.
- Link engine targets and run the reference CLI from the test, in a test target only. Never make an engine target depend on `reference/`.

**Registry.** Update rows ORC-09 and ORC-19 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/exciters/<file>`. In row ORC-08, append "; hammer potential (A5): `engine/tests/oracles/exciters/<file>`" to the test location. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-08 (hammer case), ORC-09, ORC-19 and A5-T2 exist under those names and pass locally (A5-T2 in an optimised preset) with GCC and Clang. They are clang-format and clang-tidy clean and follow the naming rule. If a test fails against the implementation, do not weaken it: report the measured values; the orchestrator routes the fix, or the planner files a spec issue.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A5: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A5`, `role` `test-author`, `model` `sonnet`;
- `requirements` an empty object;
- `tests` ORC-08, ORC-09, ORC-19 and A5-T2 (`not run`), with in `notes`:
  - the measured worst errors;
  - the reference's observed orders and extrapolation estimate;
  - the window-definition check;
  - the ORC-09 bridge-case deviation;
- `outputs` your files;
- `pull_requests` empty.
