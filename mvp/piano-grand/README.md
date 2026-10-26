# Piano 0.7a "Grand" (offline reference of the grand's board and near field)

Status: stage 1 (board FEM) is described below; stages 2 to 5 are appended as they are completed (see the end of this file).
This is an experimental MVP iteration, not a phasing-plan phase. Nothing here is built into the 0.6 plug-ins.

## Why

0.6 Full sounds like an upright. Its board is a simply supported rectangle 1.4 x 1.6 m with homogenised ribs, assumed bridge curves,
a one-sided baffled Rayleigh radiation, mics 1.5 m above the board, no rim, cavity, lid, floor or air loading. 0.7a builds the most accurate
offline reference of a grand's body and near field that this machine allows (the room is excluded: it comes later as a downstream reverb).
Hammer and damper are unchanged (they are 0.8). Baking is 0.7b/0.7c.

## Offline tools used (not vendored, not linked into any plug-in)

| tool | version | licence | use |
| --- | --- | --- | --- |
| Python | 3.14 | PSF | scripts |
| NumPy | 2.5.4 | BSD-3-Clause | all |
| SciPy | 1.18.1 | BSD-3-Clause | sparse shift-invert eigensolver (ARPACK), Delaunay triangulation, interpolation |
| Matplotlib | 3.11.2 | PSF-based (BSD compatible) | figures |
| Pillow, contourpy, fonttools, kiwisolver, cycler, pyparsing, packaging, python-dateutil, six | pip-resolved | HPND / BSD / MIT / OFL / Apache-2.0 | Matplotlib dependencies |

No GPL tools (no gmsh, no Triangle). The mesher is our own (`fem.py: make_mesh`). The venv lives outside the repository.
Large intermediates (meshes, mode shapes, FDTD fields, raw IRs) live in `~/trabalho/gbasilveira/piano-grand-work/`, not in git.

## Stage 1: grand soundboard by FEM

Files: `geometry.py` (outline, thickness, ribs, bridges, key table), `fem.py` (element, beams, solver), `fem_verify.py`, `fem_beam_verify.py`,
`board_study.py` (convergence, variants), `converge_report.py`, `make_modes.py`, `board_analysis.py`, `tests/`.

### Model

* **Plate theory: Reissner-Mindlin** (unknowns w, phi_x, phi_y per node), orthotropic spruce, grain along the long axis y, across-grain axis x.
  Material: Chabassier-Durufle (RT-0425 v2) as the MVP: E_L = 11.6 GPa, E_R = 0.9 GPa, G_LR = 0.75 GPa, nu_LR = 0.37, rho = 390 kg/m3.
  Not in that table and therefore an assumption: the through-thickness shear moduli, taken from the Wood Handbook ratios for spruce
  (G_LT/E_L = 0.061, G_RT/E_L = 0.003) times E_L: G_LT = 0.71 GPa (shear in the y-z plane), G_RT = 0.035 GPa (x-z, rolling shear); shear
  correction factor 5/6. Structural loss factor 0.02 (sigma = 0.01 omega) as MVP 3, so the comparison with 0.6 isolates geometry and acoustics.
* **Why Mindlin, not Kirchhoff.** The across-grain rolling shear modulus is 26 times smaller than E_R, so shear deformation matters from a few
  hundred hertz. Same element with the shear stiffness x 1e4 (Kirchhoff limit): f1 = 53.2 Hz instead of 48.5 Hz, and only 128 modes below
  2.5 kHz instead of 201 (`board-variants-0.7a.txt`). Kirchhoff overestimates the frequencies by up to about 10 % at the lowest modes and makes
  the board far too stiff above 1 kHz.
* **Element.** Three-node triangle, linear w and phi, constant curvature, constant shear strain from the three edge shear gaps (least
  squares, discrete-shear-gap idea), lumped mass. Verified (`fem_verify.py`, test G-T2) against the exact simply supported Mindlin plate: thin
  (h/a = 0.002) and thick (0.1) plates differ by the same 1.1 % and 0.4 % at 24 x 24 x 2 elements: **no shear locking**; second-order
  convergence. Lumped mass was chosen because the consistent mass gave 2.7 times larger dispersion error for this element.
* **Variable thickness.** 9.0 mm in the middle, 6.5 mm at the rim, raised-cosine taper over the outer 0.25 m (typical of published boards;
  assumption). Mass of the board plate 4.7 kg.
* **Outline.** A 2 m grand class wedge with a curved bent side, board length 1.50 m, width 1.22 to 1.28 m at the front, 0.45 m at the tail, area
  1.495 m2 (a 200 cm grand has a soundboard of about 1.4 to 1.6 m2; the case is 200 x 152 cm for the Yamaha C5, manufacturer figures from
  memory). The board starts about 0.40 m behind the front edge of the case (pin block). `geometry.py` has the outline function; figure
  `geometry.png`.
* **Explicit ribs.** 12 ribs across the grain (along x) at 0.115 m spacing from y = 0.12 m, spruce 22 mm wide x 30 mm high (grain along the rib,
  E = 11.6 GPa), height tapering to 40 % over the last 0.10 m at each end, stopping 20 mm short of the rim. Each rib is a chain of Timoshenko beam
  elements on mesh edges: bending stiffness of the T section (plate flange of width b + 2h) about the composite neutral axis minus the flange
  already counted in the plate, Timoshenko shear, torsion, line mass, rotary inertia with the eccentricity of the rib above the mid-plane. Membrane
  coupling is not modelled (the neutral-axis shift is taken into account instead).
* **Bridges.** Treble (long) bridge from key 45 near (0.50, 1.10) to key 108 near (1.10, 0.14), 1.14 m long; bass bridge from key 21 (0.13, 1.33)
  to key 44 (0.40, 0.95), 0.49 m long; hard maple 28 x 45 mm, E = 12 GPa, rho = 650 kg/m3 (0.82 kg/m); also beams on mesh edges. The mesh is built
  with the beam lines as chains of nodes (lattice points closer than 0.6 h are dropped) so each beam is a chain of mesh edges. (An earlier version
  embedded the beams in elements; its frequencies varied by 8 % with the position of a rib inside the element, so it was replaced. The embedded
  version is kept out of the code.) Beam length modelled 13.26 m against 13.25 m nominal, beam mass 4.15 kg. Total board mass 8.9 kg.
* **String points.** `board-key-table-0.7a.csv`: 88 keys, evenly spaced in arc length along their bridge with half a pitch margin at each end
  (17.9 mm treble, 20.3 mm bass). Keys 21 to 44 on the bass bridge, 45 to 108 on the treble bridge (same split as 0.6). The 0.6 assumed curves are
  gone. Minimum distance from any key point to the rim 0.13 m. The per-key participation phi (88 x 395, mass normalised) is in `board-modes-0.7a.npz`.
* **Edge.** w = 0 on the whole rim, and the rotation clamped by default (`theta_k = None`); a rotational spring per length `theta_k` makes it elastic
  (parameter of `fem.build_board`). Sensitivity (`board-variants-0.7a.txt`): f1 = 48.5 Hz clamped, 44.4 Hz with 2000 N m/rad/m, 40.4 Hz with free
  rotation. Clamped follows the Chabassier-Chaigne-Joly model; a real board glued into the rim is stiffer than free rotation.
* **Not modelled:** crown and downbearing prestress, the cut-off bar, the bridge notches and pins, the iron plate's coupling, inhomogeneity of the
  wood, the rim's own vibration.

### Convergence (mesh study)

`board-convergence-0.7a.txt`, produced by `board_study.py convergence` and `converge_report.py`. Six Delaunay meshes, h = 40, 30, 20, 15, 10 and 7.5 mm
(2871 to 89238 degrees of freedom). The frequency error is measured mode by mode against the finest mesh:

| mesh h | 0-250 Hz median / max | 250-500 | 500-1000 | 1-2 kHz | 2-3 kHz |
| --- | --- | --- | --- | --- | --- |
| 20 mm | 1.3 / 1.5 % | 2.8 / 4.7 | 3.6 / 5.5 | 1.9 / 5.3 | 2.5 / 3.3 |
| 15 mm | 0.8 / 2.8 | 1.6 / 3.3 | 2.1 / 3.3 | 1.0 / 3.3 | 1.2 / 1.7 |
| 10 mm | 0.45 / 1.4 | 0.41 / 2.1 | 0.71 / 1.2 | 0.39 / 2.3 | 0.37 / 0.77 |

Observed order 1.8 (three finest meshes, modes below 1 kHz; the element is second order). Against the Richardson extrapolation (p = 2) of
the two finest meshes the 7.5 mm mesh is within a median 0.6 % and at most about 3 % of the mesh-converged frequency in every band up to 3 kHz.
The mesh-topology jitter around beam crossings keeps the error from falling below about 1 %. **Converged range: the production mesh (h = 7.5 mm)
resolves modes up to 3.6 kHz (395 modes, 479 up to 4.0 kHz) to about 1 % median and 3 % worst case.** Above 3.6 kHz the model is not used; the board
there is the statistical one (see the end of this file). Mesh sizes needed: 15 elements per across-grain wavelength at 3 kHz would need 3 mm; the
extrapolation is what makes 7.5 mm sufficient.
Timings (i3-8100, shift-invert ARPACK, up to 4 kHz): 5 s at 2871 dof, 168 s at 89 238 dof; the production run (3.6 kHz) 187 s.

### Results

First ten in-vacuo modes (Hz): 49.9, 84.4, 133.2, 142.9, 187.3, 197.8, 238.2, 253.4, 267.5, 311.3. Mode counts: 19 below 500 Hz, 50 below 1 kHz,
138 below 2 kHz, 286 below 3 kHz (0.118 modes/Hz at 1 to 3 kHz). Variants (h = 15 mm, `board-variants-0.7a.txt`): without ribs f1 = 25.4 Hz and 102
modes below 1 kHz; with the ribs (and bridges) 48.5 Hz and 48 modes: the ribs roughly halve the modal density and double the lowest frequencies; the bridges add mass and shift f1 only slightly (without bridges f1 = 50.4 Hz). The ranking is the one the literature gives for a ribbed board, whose across-grain stiffness is dominated by the ribs.

Driving-point mobility at the bridge (`board_analysis.txt`, `mobility.png`; in vacuo, loss factor 0.02). The infinite-plate mobility of the
bare board is 5.7 mm/s/N (Z = 175 N s/m). The band-averaged real part of the mobility at the 88 bridge points is 0.24, 0.17, 0.13, 0.11, 0.10,
0.09 of that in the bands 50-100, 100-200, ..., 1.6-3.2 kHz, that is about 0.5 to 1.5 mm/s/N for most keys (0.2 to 2 mm/s/N overall; the two
ends of each bridge, A0 and C8, are several times larger in some bands). Published grand soundboards are reported with mean bridge mobilities of the order of 1e-3 m/s/N
(Giordano 1998; Wogram 1980; Ege, Boutillon and Rebillat 2013). That order of magnitude, and a first board mode at 50 Hz (published in-situ
grands: roughly 50 to 100 Hz), are consistent; **these literature values are quoted from memory and were not re-checked against the papers**, so
the comparison is qualitative, as asked. The old 512-mode table had 20.7 Hz for its first mode and a mobility that the ribs did not reduce.

### Tests (ctest)

* `grand-board-fem` (`tests/test_board_fem.py`, skipped without scipy): G-T2 element verification (exact SS Mindlin plate thin and thick, embedded beam
  against a thick strip) and G-T1 regression: the first 24 modes of a 30 mm mesh against the production frequencies (limits 8 % max, 3.5 % median), board
  area and mass.
