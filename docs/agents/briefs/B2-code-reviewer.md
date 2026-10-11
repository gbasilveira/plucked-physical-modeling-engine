# Brief: B2 Plate and body FEM compiler — code-reviewer

Status: ready once the B2 implementer's and test-author's commits are merged (B2 itself is **ready (pending CI on A2/A3/B1)**; B2-T4 proper waits for a reference follow-up that adds ribs).

Model: sonnet
Branch: review the B2 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

computes mass-normalised body modes from geometry and materials, as SPEC-BODY defines.

Your job: decide whether the B2 implementer's and test-author's work meets the B2 card, SPEC-BODY, the Software guidelines and the briefs, and return a verdict. Every piano and guitar body is compiled here; recompute the numbers yourself.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Languages and dependencies, Licensing, Testing policy); the phase card **B2 Plate and body FEM compiler**; **Agent briefs and handoff**.
- SPEC sections: SPEC-BODY — docs/plan/engine-specification.md, "Plate model (offline)" (D formulas, body frame, geometry inputs, SI-0013 moisture law), "Modal data (offline output)", "Contracts"; SPEC-INST "Instrument file".
- docs/agents/briefs/B2-implementer.md (including the shared ownership table and the blob layout) and docs/agents/briefs/B2-test-author.md.
- docs/adr/ADR-0002 (scikit-fem, Gmsh, licences), ADR-0003; docs/spec-issues/SI-0003, SI-0013; docs/dev/instrument-format.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The handoff reports, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `B2: ` (`git log --grep '^B2: ' --name-only`).
- CI evidence: given by the orchestrator after merging. Without a link, a test is `not run`.
- Frozen interfaces: interfaces-v2 = 300b995.

## Scope

- May edit: `compiler/tests/test_b2_review_*.py` (name your tests `test_b2_<id>_…`, for example `test_b2_t1_review_…`) — only to add missing or independent tests for B2-T1 to T5 or B2-R1 to R7. Commit with messages starting `B2: `; list them in your report. Do not push.
- Scratch work under your scratchpad directory, never committed.
- Read only: everything else. You MUST NOT change implementation, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B2-R1 Geometry input: outline polygon, thickness map, grain angle, ribs (position, section, wood), bridges (polyline, section), rim support stiffness.
2. B2-R2 Material library with orthotropic constants, density, loss factor η(f) and moisture coefficients for common tonewoods; every entry records its source.
3. B2-R3 Meshing with Gmsh; Kirchhoff plate elements in scikit-fem; ribs and bridges as local heterogeneities; MAY add Reissner–Mindlin elements.
4. B2-R4 Optional static prestress for crown and downbearing.
5. B2-R5 Sparse generalised eigen-solve up to f\_max; mass normalisation; mode shapes at bridge points and on the radiation grid.
6. B2-R6 Decay rates from η(Ω\_k), with radiation damping added by B3.
7. B2-R7 Output as a body blob described by the B1 manifest.

## Tests that must pass

- **B2-T1** simply supported orthotropic rectangle: first 20 frequencies within 0.5% of the analytic values;
- **B2-T2** clamped rectangle against published reference values within 1%;
- **B2-T3** mass normalisation within 10⁻⁶;
- **B2-T4** a ribbed rectangle against the P0.5 plate: first 20 modes within 2% (B2 test-author; blocked until the reference supports ribs);
- **B2-T5** a moisture change shifts frequencies in the direction and by the amount the coefficients predict, within 1%.

## Review checklist for B2

**Scope and process**

1. B2 commits touch only the B2 rows of the ownership table (the test-author's single pr.yml line included, actionlint clean); no change to `compiler/pme_compiler/schema/`, `pyproject.toml` or `uv.lock` unless the shared-file plan's rules were followed. Messages start `B2: `. The first commit is `modal_blob.py` with tests.

**Physics and numerics**

2. D₁₁, D₂₂, D₁₂ = ν\_RL D₁₁, D₆₆ = G\_LR h³/12 exactly as SPEC-BODY; the grain rotation by θ\_g is correct (test: a plate with θ\_g = 90° and swapped side lengths gives the same frequencies as θ\_g = 0); h(x, y) bilinear over the bounding box; SPEC-BODY comments cite the formulas.
3. Independent check: compute the 20 analytic simply supported frequencies of the B2-T1 plate yourself (mpmath or NumPy) and compare with B2's output; run one mesh refinement and confirm convergence.
4. Mass normalisation uses the full mass matrix (plate, ribs, bridges); Φᵀ M Φ = I to 10⁻⁶; bridge-point values interpolated with the element basis, not nearest node.
5. σ\_k = ½ η Ω\_k; no radiation term (B3's); moisture by SI-0013 with MC\_ref = 0.12; rigid-body modes dropped by the stated rule.
6. Ribs and bridges add the section's E I and mass; the chosen representation (beam elements or strips) is documented and tested.
7. Prestress adds the geometric stiffness with the right sign (tension raises frequencies); the analytic test passes.

**Data and provenance (B2-R2)**

8. Every material entry names a published source (author, title, year, table or page) and states how each value was obtained; spot-check at least three values against their sources if you can access them, and flag any value without a traceable source as blocking. Source licences recorded.

**Output (B2-R7)**

9. The blob follows layout 1 of the B2 brief (array names, shapes, units, float64 little-endian, 64-byte alignment, sorted Ω, groups complete); `attach` sets `source: ModalTable` and the hash; `load(verify_blobs=True)` and the C++ loader (`pme_loader` through its test tool or the B1 tests) accept the compiled examples; the CLI works.

**Tests and quality**

10. B2-T1, T2 (published values cited), T3, T5 as briefed; B2-T4 part 1 (`test_b2_r5_reference_…`) passes and part 2 is `not run` with the stated reason, or passes if the reference supports ribs; tests skip cleanly without the reference module and do not skip in the `reference-python` job.
11. `uv run ruff check`, `ruff format --check`, `mypy` (strict) clean; `test_p0_3_r1` passes; suite runtime stated and acceptable (under 5 minutes per system).
12. docs/dev/body-compiler.md names SPEC-BODY and documents the choices listed in the implementer brief.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

B2 has no engine/src code, so RT-safety, engine coverage and performance baselines do not apply. B2 cannot be marked complete while B2-T4 is `not run`; say so in the verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B2`, `role` `code-reviewer`, `model` `sonnet`; `requirements` B2-R1 to B2-R7 as verified; `tests` B2-T1 to B2-T5 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
