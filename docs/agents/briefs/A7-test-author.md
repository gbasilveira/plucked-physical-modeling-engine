# Brief: A7 Geometric nonlinearity and longitudinal waves — test-author

Status: **ready** once the A7 implementer's first commits with `longitudinalForce` and `TensionModulationPotential` are merged (`git log --grep '^A7: '`). P0.5 is merged, so the reference half of A7-T3 can be prepared now. Write ORC-11 and ORC-12 first.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

tension modulation, longitudinal modes and their transmission to the bridge.

Your job: write the two SPEC-ORC oracles the A7 card names, **ORC-11** and **ORC-12**, and the reference-solver comparison **A7-T3**. Write them independently of the A7 implementer, deriving every expected value from SPEC formulas, closed-form results or the P0.5 reference solver, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase cards **A7** and **P0.5 Reference solver**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-GEO — docs/plan/engine-specification.md, "Geometric nonlinearity and longitudinal waves" (tension modulation, longitudinal modes, F\_L, the SI-0015 energy paragraph)
  - SPEC-ORC — rows "Longitudinal forcing", "Tension modulation"
  - SPEC-CONV — symbols (φ\_n orthonormal, q\_n in m^(3/2))
  - PLAN-VAL — docs/plan/implementation-plan.md, "Validation and testing"
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md; docs/dev/signal-analysis.md.
- Headers only, never the `.cpp` files: the A7 headers (`engine/src/string/include/pme/string/longitudinal*.hpp`, `engine/src/nonlinear/include/pme/nonlinear/tension*.hpp`); `string_model.hpp`; `sav.hpp`; engine/include/pme/string.hpp.
- For A7-T3:
  - docs/dev/reference-solver.md, schemes table row R6, "Accuracy of the reference itself", "Using the reference from later phases";
  - reference/README.md: `strings[].geometric` (`tension_modulation longitudinal longitudinal_b1 longitudinal_b3 longitudinal_initial`), observation `longitudinal_bridge_force`, "Limits";
  - engine/tests/oracles/bridge/ for how A4-T3 runs `pme_reference_cli` in optimised builds only.

## Inputs

- Completed dependencies and their outputs: A2, A3, A4, A1, G0 (`pme_analysis`); the A7 implementer's commits; **P0.5** (103badf).
- Frozen interfaces: interfaces-v2 = 300b995.
- **Reference GEO scope** (P0.5 row of docs/status/phase-status.md, reference/README.md "Limits"):
  - tension modulation and longitudinal waves only for **a single fixed-end string with an initial displacement**: no hammer, barrier, bridge or plate;
  - one-way longitudinal coupling, as SPEC-GEO's default tier (SI-0015);
  - b₂ = 0.

  The reference computes F\_L at x = L as EA ξ\_x(L) + (EA − T)/2 · y\_x(L)², with one-sided second-order slopes.
- **The reference is second order** in space and time: use a refinement ladder (three grids, constant ratio, time step scaled) or a grid fine enough that its own arrival-time error is below 0.1% of the predicted time, and report the observed order.

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

- `engine/tests/oracles/geometric/**` (new);
- in `engine/tests/oracles.md` the status and test-location cells of rows ORC-11 and ORC-12 only;
- one line `add_subdirectory(geometric)` appended at the end of `engine/tests/oracles/CMakeLists.txt`.

No root `CMakeLists.txt` block.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/` or `reference/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A7-R1 to A7-R6, see docs/agents/briefs/A7-implementer.md). You verify them through ORC-11, ORC-12 and A7-T3.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-11 | Longitudinal forcing ✓ | Pair sum vs quadrature of −(EA − T)/(2ρA) ∫ y\_x² χ\_m' dx | Same value | 10⁻⁹ relative |
| ORC-12 | Tension modulation ✓ | Σ k\_n² q\_n² vs ∫ y\_x² dx | Same value | 10⁻¹² relative |

From the card: **A7-T3** against P0.5, the longitudinal precursor reaches the bridge at the predicted time within 1%.

### How to build them

All doctest, one `TEST_CASE` per case. Names start with the ID and a space and contain none of `,` `*` `?` `\` `"` `;`. Labels `oracle` and `unit` (A7-T3: `integration`).

**ORC-11.**

- Inputs: plain and wound strings with random modal states q\_v,n, q\_h,n (fixed seed, N = 60, both polarisations, amplitudes typical of a forte note), and every longitudinal mode m ≤ 16.
- Your reference: the quadrature −(EA − T)/(2ρA) ∫₀ᴸ (y\_x² + z\_x²) χ\_m'(x) dx.
  - y\_x is from the orthonormal modes φ\_n = √(2/L) sin(k\_n x), so y\_x = Σ q\_n √(2/L) k\_n cos(k\_n x).
  - Evaluate it in float64 with composite Gauss–Legendre on enough panels that the quadrature's own error (from halving the panel width) is below 10⁻¹³. Or use mpmath through a pre-computed fixture generated by a committed script, `uv run --no-project --with mpmath python …`, if you prefer.
- Compare with the implementation's `longitudinalForce`. Pass: 10⁻⁹ relative per mode, relative to the largest |f\_m^L|.
- **Check the normalisation yourself.** SPEC states f\_m^L per unit modal mass with the pair-sum prefactor −(EA − T)K\_m/(2ρA√(2L)). If the SPEC pair-sum formula and the SPEC quadrature disagree by a constant factor or a sign, do not adapt either side. Report it with the numbers; the planner files a spec issue.

**ORC-12.**

- Same random states. Compare S = Σ k\_n² (q\_v,n² + q\_h,n²) (the implementation's S, through the A7 header) with the quadrature ∫₀ᴸ (y\_x² + z\_x²) dx.
- Pass: 10⁻¹² relative.
- Also check ΔT = (EA − T) S/(2L) against your own EA (core E\_c π d\_c²/4) and T.

**A7-T3** (engine/tests/oracles/geometric/, label `integration`; reference part compiled only into optimised builds; each case under 60 s in its preset).

*Physics.* A string released at rest from a triangular displacement with its kink at x\_p has y\_x² piecewise constant, so the longitudinal forcing (EA − T)/2 ∂\_x(y\_x²) is concentrated at the kink. A longitudinal front leaves x\_p at t = 0 and reaches the bridge end x = L at t\_pred = (L − x\_p)/c\_L, with c\_L = √(EA/ρA): core EA, full ρA. The transverse kink arrives much later, at (L − x\_p)/c\_T, so until then the term (EA − T)/2 · y\_x(L)² is constant and the change of F\_L is the longitudinal precursor. Derive t\_pred in your test from your own EA, ρA, L and x\_p.

*Setup.*

- A long wound bass string, chosen so that the travel time spans at least 60 samples at the analysis rate: for example L = 1.9 m, core d = 1.2 mm steel, winding giving ρA ≈ 0.05 kg/m, so c\_L ≈ 2100 m/s.
- x\_p = 0.12 L; amplitude giving a clear signal but small enough for the default tier.
- Lossless or b₁ small, b₂ = 0; tension modulation and longitudinal modes on; fixed ends, i.e. rigid bridge (G = 0, no bridge motion; the reference has no bridge).
- Engine at 96 kHz with M\_L = 16 (the pair-sum maximum; report how many the bandwidth rule kept). The reference on a grid fine enough as above.

*Estimator* (identical on both signals):

- Take F\_L(t) − F\_L(0).
- Apply the same zero-phase low-pass with its cutoff between the engine's highest kept longitudinal mode and the next one, so both signals have the same bandwidth.
- The arrival time is the first crossing of half the step height, the plateau after the front averaged over a short window, linearly interpolated. A band-limited step crosses half its height at the true arrival time, so the estimator is unbiased.
- Report the ringing level.

*Pass:* |t\_engine/t\_pred − 1| ≤ 0.01 and |t\_ref/t\_pred − 1| ≤ 0.01. Also report |t\_engine/t\_ref − 1|. If the reference itself misses 1%, refine it before judging the engine.

*Energy cross-check (SI-0015, reported):* the engine's transverse energy balance and E\_L, against the reference's energy trace (transverse only).

Link engine targets and run the reference CLI from the test, in a test target only. Never make an engine target depend on `reference/`.

**Registry.** Rows ORC-11 and ORC-12 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/geometric/<file>`. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-11, ORC-12 and A7-T3 exist under those names and pass locally (A7-T3 in an optimised preset) with GCC and Clang. They are clang-format and clang-tidy clean and follow the naming rule. Never weaken a failing test: report.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A7: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A7`, `role` `test-author`, `model` `sonnet`;
- `requirements` an empty object;
- `tests` ORC-11, ORC-12, A7-T3 (`not run`), with in `notes`:
  - the worst errors;
  - t\_pred, t\_engine, t\_ref;
  - the reference's observed order and grid;
  - the number of longitudinal modes kept;
  - the normalisation check of ORC-11;
- `outputs` your files;
- `pull_requests` empty.
