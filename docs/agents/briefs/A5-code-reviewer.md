# Brief: A5 Hammer and felt — code-reviewer

Status: ready once the A5 implementer's and test-author's commits are merged.

Model: sonnet
Branch: review the A5 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the piano exciter: velocity map, multi-string felt contact, dissipation, release, repetition and una corda.

Your job: decide whether the A5 implementer's and test-author's work meets the A5 card, SPEC-HAM, SPEC-NL, the Software guidelines and the briefs, and return a verdict. Every piano note is struck through this code, so recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (including "Quality settings and numerical options (decision D-13)"); the phase card **A5 Hammer and felt**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-HAM — docs/plan/engine-specification.md, "Hammer and action" (including "Flexible shank")
  - SPEC-NL — "Nonlinear framework"
  - SPEC-BRG — "Discrete coupling order", "Bridge scheme"; SPEC-EXEC — per-sample loop, steps 1 to 7 and 3′ to 5′
  - SPEC-DMP — "Damper model"; SPEC-CONV — "Quality and numerical options"
  - SPEC-ORC — rows "Impact time", "Hammer–string energy", "Una corda aftersound"
- docs/agents/briefs/A5-implementer.md (including its ownership table and shared-file plan) and docs/agents/briefs/A5-test-author.md.
- docs/dev/nonlinear.md, docs/dev/bridge.md, docs/dev/dampers.md, docs/dev/reference-solver.md; SI-0011, SI-0019; ICR-0002 (`ShankParams`); ADR-0006.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A5: ` (`git log --grep '^A5: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995 (v3 if ICR-0002 was applied meanwhile; then check the rebase).

## Scope

- May edit: test sources under `engine/tests/exciters/` and `engine/tests/oracles/exciters/`, only to add missing or independent tests for A5-T1 to T4, ORC-08 (hammer case), ORC-09, ORC-19 or A5-R1 to R7. Commit with messages starting `A5: `; list them in your report. Do not push. Test names start with an ID and a space and contain none of `,` `*` `?` `\` `"` `;`.
- Scratch work under your scratchpad directory, never committed. Disk space is limited: one build tree at a time, delete scratch builds.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A5-R1 Velocity map and launch at a gap of v\_HΔt, with no added latency.
2. A5-R2 `HammerPotential` over all struck strings with windows and approach sign s\_E.
3. A5-R3 Hunt–Crossley dashpots per struck string; Stulov hereditary felt as an option.
4. A5-R4 Release and check at d\_check; repetition while in flight.
5. A5-R5 Una corda map: strike weights, felt-zone stiffness and exponent; upright soft pedal as velocity scaling.
6. A5-R6 Flexible shank as one extra oscillator (SPEC-HAM "Flexible shank"), selected per instrument by `HammerParams::shank`; absent = rigid hammer (D-13; amended 2026-10-12 from MAY).
7. A5-R7 Diagnostics: contact duration and peak force per strike.

## Tests that must pass

- ORC-09; ORC-19 (A5 test-author); ORC-08 hammer case (A5 test-author)
- **A5-T1** contact time scales as v^((1−p)/(1+p)) within 2% against a rigid wall;
- **A5-T2** against P0.5 with elastic felt, peak force and contact duration within 2% (A5 test-author);
- **A5-T3** una corda weights and felt-zone values match SPEC-HAM tables;
- **A5-T4** re-launching a hammer in flight keeps its position continuous.

## Review checklist for A5

**Scope and process**

1. A5 commits touch only the A5 rows of the ownership table, the root `CMakeLists.txt` block `# ---- A5: … ----`, one line in `engine/tests/oracles/CMakeLists.txt`, the stub sentence of docs/dev/interfaces.md, and rows ORC-08 (location note), ORC-09 and ORC-19 of engine/tests/oracles.md. `git diff <base> -- engine/include engine/src/osc engine/src/string engine/src/nonlinear engine/src/bridge engine/src/dampers` is empty. Messages start `A5: `.

**Physics and numerics**

2. V\_H, δ\_s, s\_E and the gradient match SPEC-HAM exactly. Recompute V and ∇V by finite differences in a scratch program (`uv run --no-project --with mpmath python` is fine) for one and three strings, s\_E = ±1. `value` is exactly 0 out of contact (skip rule reachable).
3. Hunt–Crossley R\_s = w\_s K\_s μ\_HC [δ\_s]₊^p\_s along c\_s. Single string: `alongGradient` set (r = 1); trichord: r = 4, never above A3's limit. The dashpot-budget note for a rank-one damper is documented.
4. Stulov: hⁿ recursion with a = e^(−Δt/τ), explicit force +w\_s K\_s min(ε h\_sⁿ, [δ\_sⁿ]₊^p) c\_s while δ\_s > 0, i.e. opposite to the elastic force (towards the string on x\_H), total felt force K(δ^p − εh) clamped at ≥ 0 (SPEC-HAM as corrected 2026-10-13, SI-0029); a rigid-wall bounce (8 g, K 4·10⁹, p 2.5, ε 0.3, τ 0.5 ms, 3 m/s) must lose energy (KE ratio 0.7187 at 48 kHz with the clamp, the SPEC law; the A5 regression test asserts 0.7187 ± 1 %; 0.598 is the unclamped K(δ^p − εh), which pulls on unloading, and is context only; corrected 2026-10-13), never gain it; the energy test closes the balance with the hereditary work (SPEC: "energy monitors MUST check it").
5. Launch at gap v\_HΔt along s\_E, so contact starts on the next sample. Velocity map formula. Upright soft pedal scaling (1 − s ρ\_up).
6. Release: hammer frozen beyond d\_check and moving away; strings afterwards bit-identical to a free run. Repetition: no jump in x\_H inside d\_check; restart from the gap outside.
7. Una corda: smoothstep definition stated; the weights table per note type with `leavingString`; K\_s(s), p\_s(s); s ramps per sample.
8. Shank: the normal-mode transform is correct (independent two-mass check). Absent shank is bit-identical to the rigid path. The internal type mirrors ICR-0002's `ShankParams`.
9. The bridge term enters `savStep` as an explicit force −ρA g\_n δ\_tt w in N per coordinate (savStep divides by the mass). It is kept separate from the potential and the solve, so A9's implicit scheme (second right-hand side) can be added. Uses A4's `bridgeForce`, `BridgePoint` and the SI-0019 rotation without copying them.
10. Precision: ψ, V, gradients, R and reductions in float64; states `S`; no fast-math; fixed reduction order. Per-sample path `noexcept`, allocation-free; RT-safety test present and passing.

**Tests**

11. A5-T1: five speeds, two exponents, interpolated contact times; ratio criterion within 2% and the absolute ORC-08 check; rebound 0.1%.
12. A5-T3 and A5-T4 as briefed.
13. ORC-08 (hammer), ORC-09, ORC-19: SPEC setups and tolerances verbatim; expected values computed independently. ORC-19's golden envelope is labelled "first accepted run" with a 0.5 dB tolerance. oracles.md: only status and location cells changed.
14. A5-T2: b₂ = 0, no `detuneH`, no `At0`, vertical polarisation; the window-definition check done and reported; three-grid Richardson with observed orders; 2% for peak force and duration at every speed and window; no engine target depends on `reference/`.
15. Every requirement ID A5-R1 to A5-R7 and every test ID has at least one test named with it (traceability).

**Quality**

16. `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` clean. All tests pass in `linux-debug` with GCC and Clang and in the rt-safety preset. `test_p0_3_r1` passes. Coverage of `engine/src/exciters` keeps engine/src at or above 90%. The benchmark is registered with `perf`.
17. docs/dev/hammer.md names the SPEC sections and documents what the implementer brief lists.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A5 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A5`, `role` `code-reviewer`, `model` `sonnet`;
- `requirements` A5-R1 to A5-R7 as verified;
- `tests` A5-T1 to A5-T4, ORC-08, ORC-09, ORC-19 with the CI run you observed (`ci_run`) or `not run`;
- `outputs` listing tests you added;
- `pull_requests` empty.

Start `notes` with `Verdict: approve` or `Verdict: request changes`, then the blocking items, then each checklist number with its command and result.
