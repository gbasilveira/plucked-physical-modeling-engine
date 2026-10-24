# Brief: A3 SAV framework and low-rank solve — code-reviewer

Status: ready once the A3 implementer's and test-author's commits are merged (A3 itself waits for A1 to be complete).

Model: sonnet
Branch: review the A3 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the generic nonlinear update used by hammers, plucks, dampers, barriers and tension modulation.

Your job: decide whether the A3 implementer's and test-author's work meets the A3 card, SPEC-NL, the Software guidelines and the briefs, and return a verdict. Every hammer, pluck, damper, barrier and tension-modulation term will run through this step, so check the derivation and the energy behaviour yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines**; the phase card **A3 SAV framework and low-rank solve**; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-NL — docs/plan/engine-specification.md, "Nonlinear framework"
  - SPEC-OSC — "The modal oscillator" ("Difference form", "Discrete energy")
  - SPEC-HAM — "Hammer and action", "Oracle"
- docs/agents/briefs/A3-implementer.md (including its ownership table) and docs/agents/briefs/A3-test-author.md.
- docs/dev/osc.md; docs/dev/rtcheck.md; docs/adr/ADR-0006.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `A3: ` (`git log --grep '^A3: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995; no public header changes for A3.

## Scope

- May edit: test sources under `engine/tests/nonlinear/` and `engine/tests/oracles/nonlinear/` — only to add missing or independent tests for A3-T1 to T4, ORC-08 or A3-R1 to R5. Commit with messages starting `A3: `; list them in your report. Do not push.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A3-R1 `Potential` interface (value, gradient) and segment coordinate stacking (modes plus lumped coordinates with masses).
2. A3-R2 `savStep` per SPEC-NL: g evaluation, half-step ψ, explicit forces, dashpot pairs.
3. A3-R3 Woodbury solve: closed form for r = 1; a small dense solve in float64 for r ≤ 4.
4. A3-R4 Skip rule, reset-at-separation rule and persistent-contact drift regulation (power-balanced regulation, or the SPEC relaxation fallback).
5. A3-R5 Diagnostics counters for resets and drift corrections.

## Tests that must pass

- ORC-08 with a rigid-wall test potential (A3 test-author)
- **A3-T1** Woodbury result equals a dense solve within 10⁻¹² on random systems (float64 build);
- **A3-T2** a quartic test potential across many modes conserves energy within 10⁻¹⁰ (float64);
- **A3-T3** any dashpot makes energy non-increasing;
- **A3-T4** the reset rule fires on separation and never raises energy.

## Review checklist for A3

**Scope and process**

1. A3 commits touch only the A3 implementer's and test-author's rows of the ownership table, the root `CMakeLists.txt` block `# ---- A3: … ----`, one line in `engine/tests/oracles/CMakeLists.txt` and the one stub sentence of docs/dev/interfaces.md. `git diff <base> -- engine/include engine/src/osc` is empty. Messages start `A3: `.

**Derivation (A3-R2)**

2. Re-derive **a**, **h**\_g, **w**\_g, **h**\_c, **w**\_c and their signs from SPEC-NL's discrete scheme with the difference form (zⁿ⁻¹ = qⁿ − uⁿ, zⁿ⁺¹ = qⁿ + uⁿ⁺¹) and compare with the code and docs/dev/nonlinear.md. The ψ update uses **g**ⁿ evaluated at zⁿ, and (**g**ⁿ)ᵀ(zⁿ⁺¹ − zⁿ⁻¹)/2. Explicit forces enter through γ/m.
3. Independent check: write a scratch float64 implementation of one SPEC-NL step for a 3-mode segment with one lumped mass, one potential and two dashpots (by solving the full linear system, not Woodbury), and compare with `savStep` over 1 000 steps from random states: agreement within 10⁻¹² relative.

**Woodbury (A3-R3, A3-T1)**

4. r = 1 is the closed form; r = 2 … 4 use a float64 r × r solve with partial pivoting on fixed arrays; `alongGradient` dashpots merge into the SAV rank; more than 3 dashpots are rejected at `configure`. A3-T1 compares against an N × N dense solve written in the test, over n\_z up to 256 and r up to 4.

**Rules and counters (A3-R4, A3-R5)**

5. Skip: V = 0 and all R = 0 give a result bit-identical to A1's free update and evaluate no gradient. Reset: only when Vⁿ = Vⁿ⁻¹ = 0; counter counts resets that changed ψ. Drift: the relaxation fallback (50 ms, τ = 10 ms, only when ψ > √(2(V + ε₀))) or a documented power-balanced method with the fallback selectable; it can be disabled; it never raises energy (check A3-T3/T4 and the `A3-R4 …` test).

**Energy (A3-T2, A3-T3, A3-T4)**

6. `savEnergy` = Σ mᵢHᵢ + ½ψ² − ε₀ in float64 with SPEC-OSC's Hᵢ; A3-T2 runs with drift regulation disabled, 64 modes, 10⁵ steps, both potentials, ≤ 10⁻¹⁰; A3-T3 checks the balance with `savDissipation`; A3-T4 counts resets per separation. Rerun and record the numbers.

**Real-time and precision**

7. `savStep` is `noexcept`, allocation-free, lock-free, and tested inside `ScopedRealtimeThread` with the `RT` label (passes in `linux-rt-safety`) for the float32 instantiation; ψ, V and every reduction are float64 in fixed order; no fast-math.

**Oracle (test-author)**

8. ORC-08 uses SPEC's setup (8 g, 3 m/s, p = 2.5) and tolerances verbatim (max(1%, 1 sample); rebound 0.1%), computes t\_c from the SPEC-HAM closed form in the test, runs float32 and float64 at 48 and 96 kHz, and measures sub-sample. engine/tests/oracles.md row ORC-08: only status and test location changed.

**Quality**

9. `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` clean; all tests pass in `linux-debug` and `linux-rt-safety` with GCC and Clang; `test_p0_3_r1` passes; coverage of `engine/src/nonlinear` keeps engine/src at or above 90%.
10. docs/dev/nonlinear.md names SPEC-NL (and SPEC-OSC), documents the derivation, the internal API for A5, A6, A7, P1, P2 and A8, the drift method and the precision choices. The benchmark is registered with `perf`.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For A3 the stored performance baseline waits for V2 and decision D-4; the benchmark itself must exist.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A3`, `role` `code-reviewer`, `model` `sonnet`; `requirements` A3-R1 to A3-R5 as verified; `tests` A3-T1 to A3-T4, ORC-08 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
