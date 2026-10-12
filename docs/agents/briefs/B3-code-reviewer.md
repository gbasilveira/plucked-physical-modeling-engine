# Brief: B3 Radiation, mobility and local bridge builder — code-reviewer

Status: ready once the B3 implementer's commits are merged (`git log --grep '^B3: '`). B3 has no test-author: its card names no ORC row and no comparison against P0.5, so the independent checks below are yours.

Model: sonnet
Branch: review the B3 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

radiation weights, radiation damping, mobilities, high-frequency conductance and the per-point local bridge models.

Your job: decide whether the B3 implementer's work meets the B3 card, SPEC-BODY, SI-0022's conventions, the Software guidelines and the implementer brief, and return a verdict. Every channel of every instrument is radiated with these gains and every bridge point runs this local model: recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (including "Quality settings and numerical options (decision D-13)", Testing policy); the phase card **B3 Radiation, mobility and local bridge builder**; **Agent briefs and handoff**.
- SPEC sections: SPEC-BODY — docs/plan/engine-specification.md, "Soundboard and bodies": "Modal data (offline output)", "Local bridge model", "Global body" step 3, "Radiation weights (offline)", "Small bodies", "Contracts"; SPEC-CONV "Sign conventions" (e^(+iωt), SI-0012) and "Quality and numerical options"; SPEC-INST "Piano defaults" (M\_b, f\_split); SPEC-API (`buildLocalBridge`, `LocalBridge`, `BodyModes`).
- docs/agents/briefs/B3-implementer.md (ownership table, conventions table, D-13 options, how each test is built).
- docs/spec-issues/SI-0022 (conventions in effect), SI-0012, SI-0002, SI-0003.
- docs/dev/body-compiler.md (B2 grid and blob layout 1), docs/dev/bridge.md (`modalSetModel`), docs/dev/radiation.md (B3's note).
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `B3: ` (`git log --grep '^B3: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995 (or interfaces-v3 if ICR-0002 has been applied meanwhile; B3 must not depend on any v3 field).

## Scope

- May edit: `compiler/tests/test_b3_review_*.py` (name your tests `test_b3_<id>_review_…`, for example `test_b3_t1_review_…`) — only to add missing or independent tests for B3-T1 to T3 or B3-R1 to R5. Commit with messages starting `B3: `; list them in your report. Do not push.
- Scratch work under your scratchpad directory, never committed. Disk is limited: one build tree at a time, delete it when done.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B3-R1 Rayleigh-integral complex gains A\_c,k per mode and channel; α, β and integer delays τ\_c exactly as SPEC-BODY.
2. B3-R2 Radiation damping σ\_k,rad from each mode's radiated power.
3. B3-R3 Driving-point and transfer mobilities at every bridge point.
4. B3-R4 G\_hf,b as the mean real part of the residual mobility over \[f\_split, f\_max\].
5. B3-R5 `buildLocalBridge`: the M\_b modes with largest Φ\_k(x\_b)² below f\_split, M\_b ≤ 64.
6. B3-R6 MAY: boundary-element radiation for lid and case effects.

## Tests that must pass

- **B3-T1** a uniformly moving baffled rectangle matches the analytic baffled-piston on-axis pressure within 1%;
- **B3-T2** local bridge mobility within 0.5 dB of the full modal mobility below f\_split, and mean real part within 10% above it;
- **B3-T3** the α, β real-time sum equals the complex gain at each mode's frequency.

## Review checklist for B3

**Scope and process**

1. B3 commits touch only the B3 rows of the ownership table: `compiler/pme_compiler/radiation/**`, `compiler/tests/test_b3_*.py`, `compiler/tests/data/b3/**`, `docs/dev/radiation.md`, new `engine/src/body/local_bridge*` files, the removal of the `buildLocalBridge` stub from `body_stub.cpp` (`compileBody` still a stub), `engine/tests/body/**`, the `buildLocalBridge` SUBCASE of `test_stubs.cpp`, the root `CMakeLists.txt` block exactly as briefed, and the one `docs/dev/interfaces.md` sentence. Nothing in `compiler/pme_compiler/body/`, `schema/`, `engine/include/`, `engine/src/bridge/`, `pyproject.toml`, `uv.lock` or workflows. Messages start `B3: `. No blob over 1 MB committed.

**Physics and numerics**

2. A\_c,k matches SPEC-BODY term by term: ρ₀/2π, e^(−iΩ|R − x|/c₀)/|R − x|, the common factor e^(+iΩ‖R\_c‖/c₀), α = Re A, β = −Im A; ρ₀ = 1.204, c₀ = 343.2 by default (SI-0022); `channelDelay` = ‖R\_c‖/c₀ in seconds. Independent check: for one compiled plate, one mode and one channel, compute A yourself by direct summation over the grid in a scratch script and compare to 10⁻¹².
3. `fold_integer_delay` uses n\_c = ⌊τ\_c fs⌋ and A e^(−iΩδ\_c) with the right sign: verify by the B3-T3 time-domain identity at 44.1 kHz with your own code.
4. σ\_k,rad: the formula and its derivation in docs/dev/radiation.md are right (W/(2E) with E = ½Ω² at unit modal amplitude; kernel sin(kr)/r with self-term k); the cell aggregation or hemisphere method agrees with a brute-force double sum on a small grid within 10⁻³; the low-frequency limit ρ₀Ω²(∫Φ)²/(4πc₀) holds; `sides="both"` doubles it; `sigma` is rewritten idempotently (`sigma − old sigmaRad + new`).
5. Mobility: Y = Φ\_aΦ\_b iω/(Ω² − ω² + 2iσω) under e^(+iωt) (a free mass gives 1/(iωm)); symmetric; Re Y\_aa ≥ 0.
6. G\_hf,b: residual = every mode not kept (including lost-ranking modes below f\_split); band \[f\_split, max Ω/2π\]; arithmetic mean over linear frequency; closed form checked against your own adaptive quadrature on a 300-mode table to 10⁻⁸; σ = 0 and critical damping handled; G\_hf ≥ 0.
7. `buildLocalBridge` (C++): strict f\_split, ranking by Φ², ties to the lower index, `modeIndex` ascending, M\_b = min(maxModes, available); every invalid input listed in the brief returns `InvalidArgument` with `out` unchanged; float64; no audio-thread claims; output accepted by `modalSetModel`; parity with the Python reference (exact indices, 10⁻¹² conductance).

**D-13 and conventions**

8. Options `sides` (front default, both) and `model` (rayleigh default, bem explicitly deferred with a clear error) exist in the API and CLI, are documented with defaults, and nothing picks an alternative silently. `front` rejects channels with z ≤ 0; `both` gives the back face the opposite sign. B3-R6's feasibility note is factual (licence and platform claims checked against the library's own pages if you can) and adds no dependency.
9. Every SI-0022 convention is implemented as the brief's table states and written in docs/dev/radiation.md; any deviation is reported, not silent.

**Tests and evidence**

10. B3-T1: the reference is the exact angular integral (your own scratch evaluation of p(z) for two cases agrees with the test's to 10⁻⁸); mesh rule stated; near-null points handled as briefed and listed; 1% met.
11. B3-T2: the three bodies as briefed, grid as briefed (refined around each Ω\_k), "above" means by adaptive quadrature, not the closed form; per-body table in the report. If the 0.5 dB criterion fails on a realistic body, the implementer reported it with the measured values and did **not** change f\_split, M\_b or the metric; say in your verdict whether the failure is genuine (recompute one failing point yourself).
12. B3-T3: time-domain identity to 10⁻⁹ relative, also with integer delays at 44.1 and 48 kHz; phasor identity.
13. Every requirement and test ID (B3-R1 to R6, B3-T1 to T3) has a test whose name starts with it; `uv run pytest tools/tests/test_p0_3_r1.py` passes; the Python suite stays under 5 minutes per system; ruff, ruff format, mypy strict, clang-format and clang-tidy clean; coverage of `local_bridge.cpp` stated.
14. docs/dev/radiation.md names the SPEC-BODY sections and documents the items the implementer brief lists. Units in blob arrays match layout 1. The CLI output loads with `load(verify_blobs=True)` and the C++ loader.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

B3's engine code (`buildLocalBridge`) runs off the audio thread: no RT-safety test or performance baseline; the coverage rule applies. B3-R6 is deferred pending the planner's ADR; B3 can be complete with R6 deferred once that ADR is approved. B3 cannot be complete while B3-T2 fails: say so in the verdict and give the measured numbers.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B3`, `role` `code-reviewer`, `model` `sonnet`; `requirements` B3-R1 to B3-R6 as verified; `tests` B3-T1 to B3-T3 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty; `spec_issues` SI-0022 plus anything new. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
