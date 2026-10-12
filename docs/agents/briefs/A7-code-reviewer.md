# Brief: A7 Geometric nonlinearity and longitudinal waves — code-reviewer

Status: ready once the A7 implementer's and test-author's commits are merged.

Model: sonnet
Branch: review the A7 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

tension modulation, longitudinal modes and their transmission to the bridge.

Your job: decide whether the A7 implementer's and test-author's work meets the A7 card, SPEC-GEO, the Software guidelines and the briefs, and return a verdict. Recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (including "Quality settings and numerical options (decision D-13)"); the phase card **A7**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-GEO — docs/plan/engine-specification.md, "Geometric nonlinearity and longitudinal waves"
  - SPEC-NL — "Nonlinear framework" (shared ψ, skip and drift rules)
  - SPEC-BRG — "Bridge force", "Bridge scheme"; SPEC-EXEC — steps 2, 6, 8
  - SPEC-OSC — "Bandwidth"; SPEC-CONV — "Quality and numerical options"
  - SPEC-ORC — rows "Longitudinal forcing", "Tension modulation"
- docs/agents/briefs/A7-implementer.md (ownership table, shared files) and docs/agents/briefs/A7-test-author.md.
- docs/dev/nonlinear.md, docs/dev/bridge.md, docs/dev/reference-solver.md; SI-0015; ICR-0002.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A7: `.
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: test sources under `engine/tests/geometric/` and `engine/tests/oracles/geometric/`, only to add missing or independent tests for A7-T1 to T3, ORC-11, ORC-12 or A7-R1 to R6. Commit with `A7: `; list them in your report. Do not push. Test naming rule as in the briefs.
- Scratch work in your scratchpad, never committed. One build tree at a time; delete scratch builds.
- Read only: everything else. You MUST NOT change implementation, build files or configuration.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A7-R1 `TensionModulationPotential` over both polarisations, sharing the segment's ψ.
2. A7-R2 Longitudinal mode compilation (fixed ends, core EA, own losses).
3. A7-R3 Pair-sum forcing for M\_L ≤ 16; MAY add the cosine-transform path for larger M\_L.
4. A7-R4 Longitudinal bridge force F\_L and the κ\_L, κ\_T contributions to the vertical bridge force.
5. A7-R5 Per-segment enable flags.
6. A7-R6 Geometric tier selector (`numerics.geometricTier`, D-13): the default tier is selectable per segment, and the selector reports `NotImplemented` for the exact tier until A10 provides it (amended 2026-10-12 from MAY; the exact tier itself is phase A10).

## Tests that must pass

- ORC-11; ORC-12 (A7 test-author)
- **A7-T1** a large-amplitude pluck's instantaneous frequency follows the Kirchhoff–Carrier prediction within 2%;
- **A7-T2** the bridge-force spectrum shows phantom partials at sums f\_i + f\_j;
- **A7-T3** against P0.5, the longitudinal precursor reaches the bridge at the predicted time within 1% (A7 test-author).

## Review checklist for A7

**Scope and process**

1. A7 commits add only new `longitudinal*` files in engine/src/string and new `tension*` files in engine/src/nonlinear (no change to `string_model.*` or `sav.*`), plus engine/tests/geometric/**, docs/dev/geometric.md, the root block `# ---- A7: … ----`, one line in engine/tests/oracles/CMakeLists.txt and rows ORC-11, ORC-12 of oracles.md. `pme_nonlinear` gained no link to `pme_string`. Messages start `A7: `.

**Physics and numerics**

2. EA is the core's E\_c π d\_c²/4; ρA the full mass; EA − T ≤ 0 rejected.
3. V\_KC = (EA − T)S²/(8L) over both polarisations; gradient ΔT k\_n² q. Shares ψ with other potentials (rank 1). The skip rule does not apply while sounding; drift relaxation does. Finite-difference check yourself.
4. Longitudinal modes: K\_m = mπ/L, ω\_m^L = K\_m √(EA/ρA), σ\_m = b1L + b3L ω², bandwidth rule, at most M\_L kept. Check two strings by hand.
5. Pair sum: the SPEC prefactor and the [δ\_{i+j,m} + δ\_{|i−j|,m}] structure, O(N) per mode, fixed order, float64 accumulation. Verify independently against a direct double sum and a quadrature in a scratch script.
6. One-way coupling (SI-0015): the transverse modes never see ξ. Energy tests use the transverse energy only and report E\_L.
7. F\_L = EA Σ √(2/L) K\_m (−1)^m ξ\_m + (EA − T)/2 · y\_x(L)². y\_x(L) sign and polarisation handling are stated. κ\_L F\_L + κ\_T ΔT reach `BridgePoint::addVerticalForce` before the bridge advances (SPEC-EXEC step 2), and nothing in A7 assumes the explicit bridge order beyond using the hook.
8. Flags off are bit-identical to a linear segment. The tier resolver matches SPEC-CONV's table; `Exact` returns `NotImplemented`.
9. Per-sample path `noexcept`, allocation-free (RT-safety test passes); templated on `S`; no fast-math.

**Tests**

10. A7-T1: single-mode Duffing setup; prediction ω² = ω₁² + ¾βa² with β = (EA − T)k₁⁴/(2LρA) (re-derive); the 2% criterion applies to the shift, not the frequency; estimator resolution checked.
11. A7-T2: f\_i + f\_j separated from partials by more than 5 bins; 20 dB above floor; control with longitudinal off and κ\_L = 0.
12. ORC-11, ORC-12: SPEC setups and tolerances verbatim; independent quadrature; the normalisation check reported.
13. A7-T3: inside the reference GEO scope (single fixed-end string, initial displacement, no bridge, b₂ = 0); t\_pred from core EA and full ρA; identical band-limiting and half-height estimator on both sides; 1% for engine and reference; no engine target depends on `reference/`.
14. Every requirement and test ID has a test named with it.

**Quality**

15. clang-format, clang-tidy clean. Tests pass in `linux-debug` (GCC, Clang) and rt-safety. `test_p0_3_r1` passes. Coverage keeps engine/src at or above 90%. Benchmark registered with `perf`. docs/dev/geometric.md complete.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A7`, `role` `code-reviewer`, `model` `sonnet`;
- `requirements` A7-R1 to A7-R6 as verified;
- `tests` A7-T1 to A7-T3, ORC-11, ORC-12 with `ci_run` or `not run`;
- `outputs` the tests you added.

Start `notes` with `Verdict: …`, then the blocking items, then each checklist number with its command and result.
