<!-- SPDX-License-Identifier: MIT -->
# Body data import (B4)

Routes for bodies that do not come from the plate compiler (`compiler/pme_compiler/importers/`; the
directory is `importers`, not `import`, because `import` is a Python keyword). Every route returns a
`BodyModalTable` (layout 1, `pme_compiler.body.modal_blob`, owned by B2) and writes it with
`modal_blob.write` and `modal_blob.attach`. Spec sections used: SPEC-BODY ("Modal data (offline
output)": Omega_k, mass-normalised Phi_k, sigma_k; "Small bodies"; "Contracts"), SPEC-CONV (units and
the e^(+i omega t) time convention, SI-0012), SPEC-INST (membrane body for the shamisen, "Instrument
file", blob manifest).

## Conventions

- SI units. Mode shapes are mass-normalised, integral(rho h Phi_k Phi_l dA) = delta_kl, unit kg^(-1/2).
- sigma_k in 1/s is the decay rate: loss factor eta gives sigma = eta Omega / 2, quality factor Q
  gives sigma = Omega / (2 Q).
- Mobility (velocity per force) and complex gains use e^(+i omega t):
  `Y_ab(w) = sum_k i w Phi_k(a) Phi_k(b) / (Omega_k^2 - w^2 + 2 i sigma_k w)`.
- Modes are sorted by ascending Omega. P bridge points and C channels come from the instrument file
  (`bridgePoints`, `channels`); the table's `points` array is a copy of the bridge-point positions.
- Output: `source: ModalTable` (CSV, MAESSTRO, membranes) or `MeasuredMobilityFit` (vector fitting);
  blob `blobs/body-modal-table.bin` relative to the instrument file; producer
  `pme_compiler.importers`. The result loads with `pme_compiler.schema.load(..., verify_blobs=True)`.

## Command line

`python -m pme_compiler.importers <route> --instrument in.json --out out.json [--blob-path P] ...`

| Route | Requirement | Key arguments |
| --- | --- | --- |
| `csv` | B4-R2 | `--table t.csv`, `--channel-delay-csv d.csv` or `--channel-delay s0,s1,...` |
| `maesstro` | B4-R1 | `--basis file.mat\|file.npz` |
| `fit` | B4-R3 | `--mobility m.npz\|m.csv --pairs N [--f-min --f-max --max-iter --tol --driving]` |
| `helmholtz` | B4-R4 | merges one mode into the table already attached to `--instrument`: `--volume --hole-area [--plate-thickness --neck-length --c0] --q-factor --phi p0,p1,...` |
| `circle`, `rectangle` | B4-R5 | `--tension --areal-density --f-max (--radius \| --size a,b)` and `--loss-factor` or `--q-factor` |

Errors exit with status 1 and a message on stderr.

## B4-R2 CSV

One header row, then one row per mode, ascending frequency. Columns (any order):

| Column | Meaning |
| --- | --- |
| `frequency_hz` | Omega / (2 pi), > 0 |
| exactly one of `sigma_per_s`, `loss_factor`, `q_factor` | sigma (>= 0), eta (>= 0), Q (> 0) |
| `phi_0` ... `phi_<P-1>` | Phi_k at each bridge point, kg^(-1/2); all P required |
| `alpha_<c>`, `beta_<c>` (optional) | radiation gains, both for every channel c = 0 .. C-1 |

Channel delays tau_c (seconds, one per channel, >= 0) come from a one-column CSV with header
`channel_delay_s` or the CLI argument, and are required with alpha/beta. Rejected, each with the
file line and column in the message: unknown, duplicate or missing columns, a P or C that does not
match the instrument, non-numbers, non-finite values, negative sigma or eta, Q <= 0, frequency <= 0,
duplicate frequencies, rows not in ascending frequency order (the input is rejected rather than
reordered, so a swapped row cannot go unnoticed), a wrong field count.

## B4-R1 MAESSTRO (status: partial)

MAESSTRO 1.0.0 (Boutillon et al., HAL hal-03409789; `maesstro-matlab` computes the soundboard modal
basis with an automatic Gmsh mesh; user guides at maesstro.cnrs.fr with a section "Structure du
fichier de sortie et donnees normalisees") is the intended source. **The output-file specification
could not be retrieved** (the hosts were unreachable from the implementation environment), so the
variable names and the file type are not verified and the **MAESSTRO licence is unverified** (to be
recorded for H1). The adapter reads the closest layout we can state, from a `.mat` (MATLAB v5) or
`.npz` file, and every name is remappable (`names=` in `read_maesstro`) so a real sample needs
configuration, not new code:

| Variable | Shape | Meaning |
| --- | --- | --- |
| `frequencies` | [K] | Hz |
| `loss_factor` or `sigma` | [K] | eta (sigma = eta Omega / 2) or sigma in 1/s |
| `nodes` | [N, 2 or 3] | mesh node positions, m (x, y used) |
| `modes` | [N, K] | nodal modal displacement shapes |
| `nodal_mass` | [N] | lumped node masses, kg (optional if already normalised) |
| `normalisation` | text | `mass` when sum_i m_i Phi_k(i)^2 = 1 already holds; anything else means arbitrary scale |

Shapes are interpolated linearly (Delaunay in x, y) at the bridge points; a point outside the mesh
is an error. Mass normalisation is checked (1e-6) when declared and applied otherwise
(Phi <- Phi / sqrt(sum m Phi^2)). What the owner must supply to close B4-R1: one genuine MAESSTRO
modal-basis output file (small mesh) plus its documentation page for the output structure, and the
licence text.

**Synthetic fixture** (B4-T3): `compiler/tests/data/b4/synthetic_maesstro.mat`, written by
`make_maesstro_fixture.py` next to it. It is ours (MIT), not MAESSTRO data: a simply supported
0.30 m x 0.40 m plate, mesh pitch 0.01 m (1271 nodes), analytic modes (m, n) in 1..3, 9 modes in
descending frequency order, unit-peak scaling, rho h = 3.2 kg/m^2, loss factors 0.01 + 0.002 m n.

## B4-R3 vector fitting

Own implementation (`vector_fit.py`) of vector fitting: B. Gustavsen and A. Semlyen, "Rational
approximation of frequency domain responses by vector fitting", IEEE Trans. Power Delivery 14(3),
1999, with the relaxed non-triviality constraint of B. Gustavsen, "Improving the pole relocating
properties of vector fitting", IEEE Trans. Power Delivery 21(3), 2006. Complex-conjugate pole pairs
only (real-coefficient basis), common poles for all responses, optional constant and proportional
terms (d + e s) per response to absorb out-of-band modes (reported, not part of the table).

- Poles: p = -sigma + i omega_d; Omega = |p|, sigma = -Re p. A pair's residue is
  R = Phi(a) Phi(b) p / (2 i omega_d), so Phi(a) Phi(b) = Re(R 2 i omega_d / p); a relative
  imaginary part above 0.25 is rejected (wrong convention or inconsistent data).
- Initial poles: `n_pairs` lightly damped pairs, log-spaced imaginary parts over the fit band,
  Re = -Im / 100. Each iteration solves the relaxed sigma problem (weights 1/|H|, i.e. relative error,
  by default) and relocates the poles to the zeros of sigma(s); unstable zeros are flipped. Frequencies
  are scaled by the band edge for conditioning.
- Stopping: largest relative pole change < `tol` (default 1e-9) or `max_iter` (default 40).
- Inputs: frequencies in Hz (positive, strictly increasing) and complex mobility (s/kg) per `(a, b)`.
  `.npz`: `frequency_hz` and `y_<a>_<b>`; CSV: `frequency_hz`, `re_<a>_<b>`, `im_<a>_<b>`.
- The driving point `a0` needs `(a0, a0)` (Phi(a0) = sqrt of the product, positive sign) and each other
  point b needs `(a0, b)` or `(b, a0)`: Phi(b) = product / Phi(a0). Overall sign is therefore fixed
  by the driving point.
- Reported: iterations, convergence, RMS relative error of the fit and of the modal table alone.

## B4-R4 Helmholtz air cavity

`f_H = (c0 / 2 pi) sqrt(S / (V L_eff))`, `L_eff = t + 1.7 r` with `r = sqrt(S / pi)`: the 1.7 r is
two flanged-end corrections of 0.85 r (Rayleigh, *The Theory of Sound*; Fletcher and Rossing, *The
Physics of Musical Instruments*, Helmholtz resonator). c0 = 343 m/s at 20 degC by default. Damping from a
given Q (sigma = Omega / 2Q). Phi_H at the bridge points is supplied by the caller (from a B4-R3 fit
or a measurement); `merge_mode` inserts the mode keeping Omega sorted. Tables with alpha/beta need the
new mode's gains; tables with a radiation grid cannot be merged. **Out of scope:** the two-oscillator
top-plate and cavity coupling.

## B4-R5 membranes

Clamped edge, tension per unit length T_m (N/m), areal density rho_s (kg/m^2), c = sqrt(T_m / rho_s).
Mass-normalised shapes; decay from `loss_factor` or `q_factor`. Air loading is out of scope.

- Circular, radius a, **centre at the origin**, angle from +x: `f_mn = j_mn c / (2 pi a)` (j_mn the
  n-th positive zero of J_m); `Phi_mn = J_m(j_mn r / a) {cos, sin}(m theta) / sqrt(rho_s N_mn)`,
  `N_0n = pi a^2 J_1(j_0n)^2`, `N_mn = (pi a^2 / 2) J_{m+1}(j_mn)^2` (m > 0), from
  integral_0^a J_m(j r / a)^2 r dr = (a^2 / 2) J_{m+1}(j)^2. Both cos and sin variants for m > 0.
- Rectangular a x b, **corner at the origin**: `f_mn = (c / 2) sqrt((m/a)^2 + (n/b)^2)`,
  `Phi_mn = 2 / sqrt(rho_s a b) sin(m pi x / a) sin(n pi y / b)`.
- Modes up to `f_max` (Hz). Bridge points are sampled by (x, y) in that frame and must lie on the
  membrane.

## Tests

`compiler/tests/test_b4_*.py`: B4-T1 (`test_b4_t1_*`), B4-T2 (`test_b4_t2_*`), B4-T3
(`test_b4_t3_*`), requirement tests `test_b4_r1_*` to `test_b4_r4_*`, CLI tests in `test_b4_cli.py`
(`test_b4_r1_cli_*` to `test_b4_r5_cli_*`). B4-T1 modes: 12 modes 20 Hz to 2 kHz with sigma rising
1 to 40 1/s with frequency, so that every resonance is wider than the log grid pitch; fit with
12 pole pairs; noise is relative (each sample multiplied by 1 + 0.01 z, z complex normal of unit
variance).

## Dependency on B2

`pme_compiler.body.modal_blob` is owned by B2. The importers use `BodyModalTable`, `validate`,
`write`, `read` and `attach` exactly as fixed in the B2/B4 briefs; they never touch the file.
