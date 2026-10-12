# Brief: A8 SIMD kernels — code-reviewer

Status: ready once the A8 implementer's commits are merged. A8 has no test-author: the card names no ORC row and no P0.5 comparison.

Model: sonnet
Branch: review the A8 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

vectorised versions of the hot loops with runtime dispatch and a scalar reference for each.

Your job: decide whether the A8 work meets the A8 card, ADR-0001, the Numerics and Real-time rules and the brief, and return a verdict. You are also the independent check of parity: write at least one parity test of your own.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Real-time rule 5, Numerics rules 2 to 5, Testing policy); the phase card **A8**; **Agent briefs and handoff**.
- SPEC and PLAN sections: PLAN-COMP — docs/plan/implementation-plan.md "Computation strategy"; SPEC-EXEC — "Threading contract"; SPEC-OSC — "Difference form"; SPEC-BRG — "Bridge scheme"; SPEC-NL — "Explicit low-rank solve".
- docs/adr/ADR-0001-highway.md; docs/agents/briefs/A8-implementer.md (ownership table, canonical 16-lane order, both bridge schemes); docs/dev/osc.md, docs/dev/bridge.md, docs/dev/nonlinear.md, docs/dev/testing.md.
- `.claude/agents/code-reviewer.md`.
- The handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A8: `.
- CI evidence: given by the orchestrator. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: test sources under `engine/tests/simd/`, only to add missing or independent tests for A8-T1 to T3 or A8-R1 to R5. Commit with `A8: `. Do not push.
- Scratch work in your scratchpad, never committed. One build tree at a time; delete scratch builds.
- Read only: everything else; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A8-R1 Vectorised mode-bank update with explicit force vector.
2. A8-R2 Dot products (bridge force, contact displacement) with a fixed reduction order.
3. A8-R3 Rank-one Woodbury vector operations.
4. A8-R4 Small matrix–matrix kernel with fixed reduction order for C2's projection and velocity products.
5. A8-R5 Runtime dispatch across SSE4.2, AVX2, AVX-512 and NEON; scalar fallback; 64-byte alignment.

## Tests that must pass

- **A8-T1** parity with the scalar reference within 10⁻⁶ relative for every instruction set;
- **A8-T2** repeated runs are bit-identical;
- **A8-T3** the AVX2 mode-bank update is at least 4× the scalar throughput on a 1,024-mode bank, with the measured baseline stored.

## Review checklist for A8

1. Scope: only engine/src/simd/**, engine/tests/simd/**, docs/dev/simd.md, one baseline JSON under tools/bench/baselines/, the root block `# ---- A8: … ----` and the interfaces.md sentence. No Highway include outside engine/src/simd (`git grep -n hwy/ -- ':!engine/src/simd' ':!cmake'` is empty). Nothing wired into osc, string, bridge or nonlinear.
2. Every kernel has a true scalar reference, protected from auto-vectorisation by a documented mechanism. Check the disassembly of one reference (objdump or `-S`) on GCC or Clang.
3. `stepModes` matches A1's operation order: bit-identical on non-FMA targets. Force null allowed. Previous-q variant present.
4. Canonical 16-lane order (8 for float64) and the fixed pairwise tree, independent of the target. Matmul reduces K in chunks of 64 in chunk order (SPEC-EXEC). Check with your own scalar re-implementation of the order: bit-identical on non-FMA targets.
5. No fast-math, no reassociation flags. FMA use documented (ADR-0001).
6. **Both bridge schemes:** the kernels are generic primitives. No kernel bakes in the explicit order (bridge force, then advance with −g δ\_tt w, in one fused pass). `axpyNeg` and `dot` serve the implicit scheme. `bridge_scheme_bench` composes both and its four medians are reported.
7. Dispatch resolved in `initialise()` (non-audio); kernels `noexcept` and allocation-free; RT test present. `ScopedFlushDenormals` sets and restores FTZ/DAZ (x86) and FZ (arm64).
8. Tails: no read or write past n (test with guard pages or canaries at n = 1, 15, 17, 1023). 64-byte alignment asserted in debug.
9. A8-T1: every compiled and supported target exercised and logged; the 10⁻⁶ norm stated; realistic and cancelling inputs. List which targets ran on which CI system. AVX-512 coverage is stated honestly.
10. A8-T2: bit-identical across 100 runs and shifted aligned copies, per target.
11. A8-T3: AVX2 against true scalar, ratio ≥ 4 on x86 CI machines, honest message on arm64. Baseline JSON committed in the testing.md format. MSVC result and the clang-cl decision recorded.
12. Write one independent parity test of your own (for example `A8-T1 reviewer parity of dot2 and axpyNeg in float64`).
13. clang-format, clang-tidy, ctest (`linux-debug` GCC and Clang, `linux-release`, rt-safety) and `test_p0_3_r1` pass. Coverage keeps engine/src at or above 90%. docs/dev/simd.md complete as the brief lists.

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

- `phase` `A8`, `role` `code-reviewer`, `model` `sonnet`;
- `requirements` A8-R1 to A8-R5 as verified;
- `tests` A8-T1 to A8-T3 with `ci_run` or `not run`;
- `outputs` the tests you added.

Start `notes` with `Verdict: …`, then the blocking items, then each checklist number with its command and result.
