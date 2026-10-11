# Architecture decision records (ADR)

Every decision not fixed by SPEC or PLAN gets an ADR here, named `ADR-NNNN-<slug>.md`. ADRs are planning documentation: the `planner` (Opus) writes them; see the model policy in [../../CLAUDE.md](../../CLAUDE.md).

Phase P0.1 creates the ADR template and the first three records (planning outputs of P0.1 in [../plan/phases.yaml](../plan/phases.yaml)):

- ADR-0001 — Google Highway as the SIMD library
- ADR-0002 — scikit-fem as the plate FEM library
- ADR-0003 — dependency pinning (CPM.cmake exact commits, Python lock file)

Template: [TEMPLATE.md](TEMPLATE.md).

Further records:

- ADR-0004 — no ThreadSanitizer preset on Windows (Accepted; defers one cell of P0.1-R2 and P0.1-T1)

Phase H1 adds the licence ADR that records decision D-1.
