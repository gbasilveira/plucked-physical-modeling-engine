# Brief: A1 Modal oscillator — test-author

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the discrete oscillator every mode in the engine uses, with float64 coefficient compilation and a structure-of-arrays mode bank.

Your job: write the two SPEC-ORC oracle tests the A1 card names, **ORC-01** and **ORC-02**, independently of the A1 implementer, deriving every expected value from SPEC formulas and closed-form results, never from the implementation's output.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, Numerics rules); the phase card **A1 Modal oscillator**; "Agent briefs and handoff", **Roles** (Oracle author).
- SPEC and PLAN sections:
  - SPEC-OSC — docs/plan/engine-specification.md, "The modal oscillator"
  - SPEC-CONV — docs/plan/engine-specification.md, "Scope, conventions and notation"
  - SPEC-ORC — docs/plan/engine-specification.md, "Verification oracles", rows "Oscillator accuracy" and "Oscillator energy"
- `.claude/agents/test-author.md` — your rules.
- engine/tests/oracles.md and docs/dev/testing.md ("Oracle registry", "Where tests live", tolerance macros); docs/dev/signal-analysis.md (partial tracking and energy accounting).
- engine/include/pme/osc.hpp (public) and the A1 internal **header(s)** under `engine/src/osc/include/pme/osc/` (declarations of `ModeBank`, the difference-form kernel, energy, `compileOscillatorF64`). Read headers only, never `engine/src/osc/*.cpp`.
- docs/spec-issues/SI-0008-oscillator-coefficient-cancellation.md (context for A1-R1; it does not change the oracles).

## Inputs

- Completed dependencies and their outputs: **G0** passed (docs/gates/G0.md); P0.3's `pme_test_framework`, `pme_add_doctest`, tolerance macros (`PME_CHECK_CENTS`, `PME_CHECK_REL`) and `pme_analysis` (partial tracking, decay fitting, `account_energy`).
- The A1 implementer's commits (`git log --grep '^A1: '`), merged by the orchestrator before you start.
- Frozen interfaces: interfaces-v2 (ICR-0001); osc.hpp unchanged since interfaces-v1 (2daee85).

## Concurrent work

The ownership table in docs/agents/briefs/A1-implementer.md ("Concurrent work") applies to you. Your rows: `engine/tests/oracles/**` (new) and, in `engine/tests/oracles.md`, the status and test-location cells of rows ORC-01 and ORC-02 only. Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A1 (test-author): oracle tests ----
add_subdirectory(engine/tests/oracles)
# ---- end A1 (test-author) ----
```

`engine/tests/oracles/CMakeLists.txt` contains `add_subdirectory(osc)`; later oracle authors add one line each. Your sources go in `engine/tests/oracles/osc/`.

## Scope

- May edit: the paths above.
- Read only: everything else. Never edit implementation code under `engine/src/`.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

The card's requirements are the implementer's (A1-R1 to A1-R6, see docs/agents/briefs/A1-implementer.md). You verify them through ORC-01 and ORC-02 only.

## Tests that must pass

From SPEC-ORC, verbatim:

| ID | Test | Setup | Expected | Tolerance |
| --- | --- | --- | --- | --- |
| ORC-01 | Oscillator accuracy ✓ | One mode at 27.5 Hz, 261.6 Hz, 4186 Hz; T60 = 20 s; 48 kHz; float32 difference form | Frequency ω\_d/2π and decay e^(−σt) | 0.01 cent; T60 within 0.5% (run: under 0.001 cent) |
| ORC-02 | Oscillator energy | Lossless mode, 10 s | H^(n+½) constant | 10⁻¹² relative in float64; 10⁻⁵ in float32 |

### How to build them

**ORC-01** (doctest, labels `oracle` and `unit`; one `TEST_CASE` per frequency, named `ORC-01 oscillator accuracy at 27.5 Hz` and so on, no commas):

- σ = 3 ln 10 / 20 s⁻¹ (T60 = 20 s); ω chosen so that ω\_d = 2π f with f the stated frequency (ω = √((2πf)² + σ²)); fs = 48 000 Hz.
- Coefficients from the public `pme::compileOscillator`; the state advances in the engine's float32 `ModeBank` kernel (the float32 difference form is what the oracle tests). Initial condition: q⁰ = 1, u⁰ chosen for a pure damped cosine (or impulse forcing; document which), f = 0.
- Simulate 4 s (or longer for 27.5 Hz if your estimator needs it; state the length), convert qⁿ to float64 and measure frequency and decay with `pme_analysis` partial tracking and decay fitting (or an independent least-squares fit of a damped cosine written in the test).
- Pass: |1200 log₂(f̂ / f)| ≤ 0.01 cent and |T̂60 / 20 s − 1| ≤ 0.005. Report the achieved errors (SPEC's run reached under 0.001 cent).

**ORC-02** (labels `oracle` and `unit`):

- Lossless (σ = 0), f = 0, 10 s at 48 kHz (480 000 steps), for at least three frequencies: 27.5 Hz, 4186 Hz and one near the bandwidth limit (ω\_dΔt = 0.9π). Energy H^(n+½) = ½(uⁿ⁺¹/Δt)² + ½ ω̂² qⁿ qⁿ⁺¹ (SPEC-OSC "Discrete energy"), accumulated in float64.
- **float64 case:** step the difference form in float64. Use the implementer's templated kernel instantiated for `double` **and**, as an independent check, your own five-line float64 implementation of uⁿ⁺¹ = βuⁿ − κqⁿ + γfⁿ, qⁿ⁺¹ = qⁿ + uⁿ⁺¹ written from SPEC-OSC, with float64 β, κ, γ from `compileOscillatorF64` (or computed in the test from SPEC formulas). Pass: max over n of |Hⁿ⁺½ − H½| / |H½| ≤ 10⁻¹².
- **float32 case:** the engine's float32 `ModeBank`, energy computed from the float32 states in float64 with float64 ω̂². Pass: max relative drift ≤ 10⁻⁵.
- **Tolerances are explicit in the test.** Do not rely on the golden-feature default energy tolerance (10⁻⁹ absolute in `pme_analysis`'s golden format): it is looser than ORC-02 (a P0.3 follow-up). If you use `account_energy`, pass the ORC-02 tolerance explicitly.
- Add a balance check for one damped, forced case: H^(n+½) − H^(n−½) equals Δt[−2σ̂ (δ\_t· qⁿ)² + fⁿ δ\_t· qⁿ] within 10⁻¹² relative of H in float64 (name it `ORC-02 energy balance with loss and forcing`).

**Registry.** Update rows ORC-01 and ORC-02 of engine/tests/oracles.md: status `implemented` (the orchestrator sets `passing` once CI is green on all three systems), test location `engine/tests/oracles/osc/<file>`. Never edit the SPEC columns.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For the test-author: ORC-01 and ORC-02 exist under those names, pass locally in `linux-debug` and `linux-rt-safety` with GCC and Clang, are clang-format and clang-tidy clean, and follow the naming rule (`uv run pytest tools/tests/test_p0_3_r1.py`). If a test fails against the implementation, do not weaken it: report the measured values; the orchestrator routes the fix to the implementer, or the planner files a spec issue.

## Working rules

- Work on the branch your worktree is on. Commit with messages starting `A1: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- You do not push, so report every test `not run` with local evidence in `notes`.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A1`, `role` `test-author`, `model` `sonnet`; `requirements` an empty object (the implementer reports A1-R1 to A1-R6); `tests` ORC-01 and ORC-02 (`not run`), with the measured worst errors in `notes`; `outputs` your files; `pull_requests` empty.
