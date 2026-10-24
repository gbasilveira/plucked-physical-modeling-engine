# ADR-0002: scikit-fem as the plate FEM library

- **Status:** Accepted
- **Date:** 2026-10-11
- **Deciders:** fixed by the Software guidelines of the phasing plan ("Design compiler: Python 3.12 or later with NumPy, SciPy, Gmsh, scikit-fem (plate elements) … FEniCSx MAY replace scikit-fem only through an ADR") and by B2-R3; recorded by the planner (Opus) in P0.1.
- **Phase:** P0.1 (requirement P0.1-R6)
- **References:** PLAN-STACK (Design compiler row), PLAN-ARCH (offline design compiler layer); SPEC-BODY (plate model, modal data); phase cards B2 and B3; phasing plan, Software guidelines

## Decision

The design compiler (`compiler/pme_compiler/body/`, phase B2) computes soundboard and body modes with scikit-fem: Gmsh meshes the outline, scikit-fem assembles the orthotropic Kirchhoff plate with ribs and bridges as local heterogeneities, and SciPy's sparse eigen-solver extracts the mass-normalised modes. FEniCSx, or any other FEM library, can replace scikit-fem only through a new ADR that supersedes this one.

## Context

- SPEC-BODY defines the body as an orthotropic Kirchhoff plate of thickness h(x, y) with grain angle θ\_g, ribs and bridges as attached beams and an elastically supported rim. Reissner–Mindlin theory MAY replace Kirchhoff; static prestress for crown and downbearing MAY be added. The output is K mass-normalised modes up to f\_max with Ω\_k and Φ\_k at bridge points and on the radiation grid.
- The Kirchhoff operator is fourth order in space, so a conforming finite element needs C¹ continuity (or a nonconforming element designed for plates).
- B2's tests set the accuracy: first 20 modes of a simply supported orthotropic rectangle within 0.5% of the analytic values (B2-T1), a clamped rectangle within 1% of published values (B2-T2), mass normalisation within 10⁻⁶ (B2-T3).
- The compiler runs offline, never on the audio thread (PLAN-ARCH). It must install with the project's Python lock file (ADR-0003) on Windows, macOS and Linux, because instrument authors run it on all three.
- The project is MIT-licensed; the compiler's dependencies need licences compatible with distributing the compiler as MIT source.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **scikit-fem** (chosen) | Pure Python on NumPy and SciPy; installs from PyPI wheels on all three systems. Provides plate elements: `ElementTriArgyris` (C¹-conforming quintic triangle) and `ElementTriMorley` (nonconforming quadratic plate element), and Hermite beam elements for ribs. Bilinear forms are written directly in Python, so the rotated orthotropic stiffness D(θ\_g) and a thickness field h(x, y) are a few lines. Reads Gmsh meshes. Licence: BSD-3-Clause. | Single-process assembly in NumPy; no built-in parallel eigen-solver. Extracting thousands of modes with SciPy's ARPACK wrapper (`eigsh`) is slow unless split into shift-invert frequency windows. Beam-to-plate coupling for ribs must be assembled by our code. |
| FEniCSx (DOLFINx) | Mature, parallel (MPI, PETSc, SLEPc eigen-solvers), scales to large meshes. | Heavy install (usually conda or containers), harder on Windows; Kirchhoff plates need a C⁰ interior-penalty or mixed formulation rather than a ready C¹ element; DOLFINx is LGPL-3.0. |
| Own FEM code | No dependency. | Reimplements assembly, quadrature and elements that scikit-fem already tests. |
| Commercial FEM (for example Abaqus) | Industrial reference. | Not open source; cannot be part of an open toolchain. Results MAY still serve as validation references. |
| MAESSTRO toolchain | Built for piano soundboards (PLAN, piano model section). | A research toolchain, not a general library we can call from the compiler; used as a validation reference instead (PLAN-VAL). |

## Consequences

- **B2** builds `compiler/pme_compiler/body/` on scikit-fem and Gmsh. Recommended element: `ElementTriArgyris`, because its vertex derivative degrees of freedom couple naturally to Hermite beam elements for ribs and bridges; `ElementTriMorley` MAY be used for fast previews. B2 records the element choice in docs/dev/body-compiler.md; changing the library needs an ADR, changing the element does not.
- **Eigen-solve.** B2 uses `scipy.sparse.linalg.eigsh` in shift-invert mode over successive frequency windows up to f\_max, merges and de-duplicates the windows, and mass-normalises in float64. A soundboard needs about 2,000–3,000 modes (PLAN-COMP budget); B2 reports the compile time per instrument in its handoff report so the cost is visible.
- **Reissner–Mindlin** (allowed by SPEC-BODY and B2-R3) is added in scikit-fem with mixed elements if needed; it does not need a new ADR.
- **Dependencies.** P0.1 adds scikit-fem, Gmsh's Python package and meshio to the Python lock file (ADR-0003), in an optional group for the FEM compiler so that users who only render do not install Gmsh.
- **Licences.** scikit-fem is BSD-3-Clause. Gmsh is GPL-2.0-or-later with exceptions; it is called as an external library by the offline compiler and is not distributed inside the plug-in. H1 confirms in the provenance audit that distributing the compiler as MIT source with Gmsh as a separately installed dependency is acceptable, and records it in the software bill of materials. Any bundle that ships Gmsh binaries needs H1's review first.
- **Validation.** B2-T4 compares a ribbed rectangle against the independent finite-difference plate of P0.5 (P0.5-R4), so a scikit-fem defect cannot hide behind a shared code path.

## Verification

- P0.1-T2: the Python package installs from the lock file and pytest runs.
- B2-T1 to B2-T5 and B2-T4's comparison against P0.5.
- Code review of B2: FEM assembly uses scikit-fem; no second FEM library in `compiler/`.

## Revisiting

Reopen this decision if B2 cannot meet B2-T1 to B2-T4 with scikit-fem, or if compile time for a full piano soundboard makes recompiling a body impractical for instrument authors (B2 or E1 handoff reports would show it). The likely replacement is FEniCSx with SLEPc, through a new ADR that also settles its install path on Windows.
