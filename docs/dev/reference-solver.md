# Reference solver (P0.5)

SPEC sections implemented: SPEC-STR ("Stiff string model": wound strings, tension for pitch, modes,
losses), SPEC-BRG ("Bridge coupling": moving support, polarisation coupling, the small-admittance
and conductance results), SPEC-HAM ("Hammer and action": contact law, Hunt-Crossley term, Oracle),
SPEC-BODY ("Soundboard and bodies": Kirchhoff plate, D11, D22, D12, D66, "Geometry inputs": ribs), SPEC-PLK ("Plucked
exciters and barriers": initial displacement, distributed barrier), SPEC-GEO ("Geometric
nonlinearity and longitudinal waves"). Background: PLAN-VAL (lossless energy drift at round-off
level; convergence order as derived). Usage and the scenario format: [reference/README.md](../../reference/README.md).

The reference is **independent of the engine**: it shares the description structs
(`engine/include/pme/`, header-only use) and `pme_analysis`, nothing else. There is no modal
synthesis, no oscillator coefficient of SPEC-OSC, no SAV and no shape table in it: every scheme is a
physical-space finite-difference scheme with an exact discrete energy identity, solved with banded
Cholesky factorisations and Newton iterations, all in float64 with a fixed operation order.
Targets live under `reference/` and `tools/reference-cli/`; `reference/CMakeLists.txt` fails the
configure step if any of them reaches `pme_engine` or a `pme_<module>` target. Setting
`-DPME_REFERENCE_ISOLATION_SELFTEST=ON` links `pme_string` into the reference on purpose, which makes
the check fire. Sources of schemes are cited in the comment of each function (Bilbao, *Numerical
Sound Synthesis*, 2009, abbreviated B2009).

## Schemes

All linear parts use the implicit theta-scheme on the coupled system `M d_tt x + C d_t. x +
K (x + theta k^2 d_tt x) = f` (B2009 sections 7.2 and 12.1), with `theta` in [1/4, 1] (default 1/4,
unconditionally stable, second order in `k` and `h`). Time step `k = 1 / (fs * oversampling)`.

| Req. | Physics | Scheme (source) | Order | Energy identity |
| --- | --- | --- | --- | --- |
| R1 | stiff damped string, SPEC-STR | theta-scheme on a uniform grid of N intervals; simply supported ends (ghost point `u_(-1) = -u_1`, so `y = d_xx y = 0`) or clamped (`u_(-1) = u_1`, half-weight curvature row); B2009 7.1-7.2 | 2 in `h`, 2 in `k` | `H = 1/2 \|v\|_M^2 + 1/2 x^n . K x^(n-1) + theta k^2/2 \|v\|_K^2`, `v = (x^n - x^(n-1))/k` |
| R2 | hammer, SPEC-HAM | two-step discrete gradient of `V = K [g]_+^(p+1)/(p+1)` with Newton on the contact gap (not SAV); B2009 8.1 | 2 in `k` | kinetic + `(V(g^n) + V(g^(n-1)))/2` + string |
| R3 | bridge, SPEC-BRG | bridge point as extra degrees of freedom, strings eliminated by Schur complement; mass-spring-dashpot (`m`, `k_b`, `r_b`) or conductance (`r = 1/G`); perpendicular axis a conductance `G_h`; rotation `theta_b` | 2 | bridge terms are part of `M, K, C` |
| R4 | orthotropic Kirchhoff plate, SPEC-BODY, with axis-parallel ribs as beams | `K = hx hy [D11 Dxx^2 + D22 Dyy^2 + 2 (D12 + 2 D66) Dxx Dyy]` on a rectangle, simply supported; the mixed term in the commuting form so that the discrete sine modes are exact eigenvectors; bilinear spreading and interpolation at bridge points; ribs add `E I hs (delta_ss w)^2` to `K` and lumped `rhoA hs` to the nodal mass (see "Ribs"); B2009 12.1 | 2 in `h`, 2 in `k` (ribs included, measured below) | as R1 (ribs are part of `M` and `K`) |
| R5 | barrier, SPEC-PLK | same discrete gradient per contact point, active-set Newton on the dense contact system, `V_B = sum_j dx_j K [eta_j]_+^(alpha+1)/(alpha+1)` | 2 | as R2 |
| R6 | tension modulation, longitudinal waves, SPEC-GEO | Kirchhoff-Carrier potential `V = (EA-T) S^2/(8L)`; force `a Kt (u+ + u-)/2` with `a = (EA-T)(S+ + S-)/(4L)` and a scalar Newton iteration on `a`; longitudinal theta-scheme with the forcing explicit at step n; B2009 8.1-8.2 | 2 | `(V(S+) + V(S))/2` added |

**Losses and the b2 = 0 limit.** SPEC-STR's `sigma_n = b1 + b2 sqrt(omega_n) + b3 omega_n^2`. The
finite-difference loss `2 sigma0 d_t y - 2 sigma1 d_t d_xx y + 2 sigma3 d_t d_xxxx y` (B2009 7.1.3)
has `sigma(omega) = sigma0 + sigma1 k^2 + sigma3 k^4`, which equals `b1 + b3 omega^2` for
`sigma0 = b1`, `sigma1 = b3 T/rhoA`, `sigma3 = b3 EI/rhoA`. The three terms are exactly
`C = 2 b1 M + 2 b3 K`, which the code uses (Rayleigh damping; the loss per step is
`k (v)^T C (v)` with the centred velocity, so the energy balance is exact). **`b2 sqrt(omega)` has
no local finite-difference form, so the reference accepts only `b2 = 0` and rejects other values with
an error that names `b2`.** Comparisons against the engine (A2-T3 and later) must therefore use
`b2 = 0`. The plate takes a Rayleigh loss `b1 + b3 omega^2` (`plate.loss`); a `lossFactor` eta is
mapped to `b3 = eta / (2 omega_ref)` at a reference frequency, so that `sigma = eta omega / 2` there.

**Coupling.** A bridge degree of freedom (or the plate nodes at a bridge point) is shared by all
strings attached to it, both polarisations; the strings enter through the Schur complement of
their interior block, so the joint solve keeps an exact energy balance (dashpot and conductance
terms are the only dissipation besides string loss and contact damping). Plate coupling is
energy-neutral: the same bilinear weights spread the string end force on the plate and interpolate
the plate displacement.

**Contacts.** For hammers and barriers every contact has a gap `g = g0 + c . x` (linear in the
state) and a force `-phi c`. With the response of the linear system to each contact direction
precomputed, the step reduces to `g+ = g_free - Gamma phi(g+, g-)` with a small dense `Gamma`.
Newton iteration with a line search, converged to a residual of 1e-14 of the largest gap (relative)
within 50 iterations. When the line search can no longer decrease the residual, the iteration stops
only if the residual is already below the round-off floor of 1e-13; otherwise, and when 50
iterations do not suffice, the solver throws `RefError` (never silent). The worst residual of a
run is reported in `Results::newton`. The two-step discrete gradient
`(V(g+) - V(g-))/(g+ - g-)` is evaluated with `expm1/log1p` so that it has no cancellation.
Hunt-Crossley damping is `R = K mu [g^n]_+^p` times the centred gap velocity (explicit in `g`, hence
dissipative). For barriers the dashpot is applied per contact point (`R_j = dx_j K_B mu_B [eta_j]_+^alpha`
along that point's own direction), not as the single dashpot along grad V_B that SPEC-PLK writes;
without contact damping (the ORC-15 setup) the two are identical.

**Round-off.** `K x` is evaluated through first differences and differences of differences
(`string_ops.cpp`), because `x_(i+1) - x_i` is exact for smooth data; this keeps the energy monitor
at about 1e-14 relative per step on fine grids instead of eps N^2. Starting at rest uses the
time-symmetric start `x^(-1) = x^0 - k^2/2 (M + theta k^2 K + k C/2)^(-1) K x^0`.

## Ribs (P0.5-R4 follow-up P0.5-ribs)

SPEC-BODY "Geometry inputs": ribs are straight beams with a rectangular section. The reference
models a `RibDesc` as an **Euler-Bernoulli beam along its axis `s`, attached to the plate's
mid-surface** (z = 0, SPEC-BODY "Body frame"):

- `E I = youngs width height^3 / 12` (out-of-plane bending, `height` normal to the plate),
  `rhoA = density width height`;
- energy added to the plate: `1/2 int E I (w_ss)^2 ds + 1/2 int rhoA (w_t)^2 ds`;
- **neglected**: torsional stiffness, rotary inertia, the offset between the beam and plate
  mid-surfaces, and the moisture dependence of rib properties (rib values are taken as given).

**Discretisation.** Only ribs parallel to x or y are accepted (`|dy|` or `|dx|` at most 1e-9 m),
with z = 0 at both ends, positive width, height, youngs and density, inside the rectangle; anything
else is a `RefError` naming `ribs[i]` and the field (a diagonal rib: "only axis-parallel ribs are
modelled"). On the chosen grid the rib's line and both end points must fall on grid nodes within
1e-9 h, otherwise `RefError` naming `ribs[i]` and the grid; the ends may lie on the plate edge or
inside the plate. The stiffness is `K_rib = sum_p E I hs (delta_ss w)_p^2` over the **interior
line nodes** `p` (a rib end is a free end), `delta_ss` the 3-point second difference along the line
with spacing `hs`, with `w = 0` at simply supported plate-edge nodes; it is added to the banded
`K` (the plate's half-bandwidth covers a line stencil) and is symmetric positive semi-definite. The
mass is lumped, `rhoA hs` per line node and `rhoA hs / 2` at the two end nodes (trapezoid rule),
added to the nodal mass (`PlateOperator::node_mass`; `PlateOperator::mass` stays the plate's own
`rho h hx hy`, and without ribs `node_mass[i] == mass` bit for bit). Crossing ribs simply add.
Ribs are part of `M` and `K`, so the coupled simulation with strings keeps the exact energy identity
(tested to 1e-12 per step with two crossing ribs).

**Modal solve.** `plate_modes(c, ribs, nx, ny, count, points, options)` returns the `count` lowest
eigenpairs of `K phi = omega^2 M phi` of the discrete operator: frequencies in Hz ascending and,
for `points` given, the mode values there (the same bilinear weights as bridge points; mass
normalised, `phi^T M phi = 1`; the sign makes the largest nodal value positive).

- Algorithm: `A = M^(-1/2) K M^(-1/2)` (banded, `M` diagonal), factored once with `BandCholesky`;
  block inverse (subspace) iteration with Rayleigh-Ritz on a block of `p = count + max(10, count/2)`
  columns; the starting block comes from splitmix64 with the fixed seed `0x9E3779B97F4A7C15`
  (column after column, uniform in [-1, 1)); the block is orthonormalised (modified Gram-Schmidt,
  two passes) each iteration, the projected `p x p` problem is solved by a **cyclic Jacobi**
  eigen-solver written in the reference. No LAPACK, no SciPy.
- Round-off: the products `A x` of the iteration, the Rayleigh quotients and the residuals use the
  difference form `plate_stiffness_apply` (second differences taken as differences of first
  differences, then their differences), not the expanded banded product, whose error `eps/(k h)^4`
  would stall the Ritz values at 1e-12; each inverse-iteration step carries one step of iterative
  refinement (solve with the banded factor, residual in difference form, solve again).
- Stopping rule: every one of the `count` lowest Ritz values changes by less than **1e-13**
  relative between iterations **and** each transformed residual `||A x - theta x|| / theta` is at
  most 1e-10 plus the round-off allowance below; `RefError` after **500** iterations (the B2-T4
  configurations need 27 to 46).
- Residual check on return: `||K phi - omega^2 M phi|| / (omega^2 ||M phi||) <= 1e-9` for each pair,
  else `RefError`. **Round-off allowance:** a float64 vector carries relative errors of eps in
  every component, i.e. a high-frequency part of size eps whose image under `K` is about
  `eps rho(K)`, so no float64 vector has a relative residual below `eps rho(K) / omega^2` (about
  3e-10 for the lowest mode of the ribbed 96 x 64 plate, measured; 6e-10 for the unribbed lowest
  mode). The limits above therefore read `1e-9 + 4 eps G / (omega^2 m_min)` and
  `1e-10 + 4 eps G_A / theta`, with `G` the largest absolute row sum of `K` (a bound on its spectral
  radius). Below roughly 100 x 100 nodes the allowance is below 1e-9 and invisible; the brief's bare
  1e-9 would fail spuriously on finer grids.
- Determinism: fixed operation order, no threads, a fixed starting block: the same input gives
  bit-identical output on one platform (tested).

**Using it from B2-T4.** Call `pme_reference.plate_modes(plate, nx, ny, count, points)` with the
same `plate` object as in a scenario (including `ribs`) on **three grids with ratio 2 that contain
the rib lines** (for ribs at `x = 0.15, 0.3, 0.45 m` on the 0.6 m plate, `nx` a multiple of 4:
24 x 16, 48 x 32, 96 x 64), and Richardson-extrapolate `f* = f3 + (f3 - f2)/3` (observed order
`log2((f1 - f2)/(f2 - f3))`). **Do not extrapolate by sorted index.** Two modes whose frequencies
lie closer than the discretisation error (309.30 Hz and 310.14 Hz in the Levy case below) swap
places between grids; extrapolating the swapped pairs gives errors of 0.25%. Request `points`
(a lattice of 8 x 6 points off the symmetry lines of the plate works) and match each mode of the
finest grid to the mode of the coarser grid with the largest modal assurance criterion
`MAC = (a.b)^2 / (a.a b.b)` of the sampled shapes; ask for about 8 more modes on the coarse grids
so that every partner is present. The C++ test `P0.5-R4 ribbed plate frequencies converge at
second order` does exactly this (`track_frequencies`). The mode values are interpolated at
`points`, which are not grid nodes; the interpolation error is part of the MAC, not of the
frequencies.

**Measured (linux-release, GCC, one core).** The B2-T4 configuration (three spruce ribs 20 mm x
15 mm parallel to y at x = 0.15, 0.3, 0.45 m on the P0.5-T4 plate), first 20 modes: observed
orders 1.86 to 1.995 (all within 10% of 2); `plate_modes` takes 0.2 s (24 x 16, 45 iterations),
0.9 s (48 x 32, 46) and 6.1 s (96 x 64, 42); with 45 modes on 96 x 64 about 15 s (41 iterations). Extrapolated
frequencies, Hz: 134.7326, 147.4555, 191.5590, 309.3032, 328.2106, 357.8721, 362.5614, 404.2061,
415.0008, 415.1049, 418.5332, 454.8122, 455.0650, 487.7954, 504.3917, 524.0463, 555.4888,
581.5171, 585.0162, 613.5732.

**Exact continuum check (Levy).** A simply supported orthotropic plate with one full-width rib
parallel to y at `x0` separates as `w = X(x) sin(beta y)`, `beta = n pi / Ly`. On each side of `x0`

    D11 X'''' - 2 (D12 + 2 D66) beta^2 X'' + (D22 beta^4 - rho h omega^2) X = 0,

with `X = X'' = 0` at `x = 0` and `Lx`; at `x0` the displacement, slope and moment are continuous
(`X`, `X'`, `X''`) and the shear jumps by the rib's line load. Derivation: the rib acts on the
plate with the load `p = -(E I w_yyyy + rhoA w_tt) delta(x - x0)` per unit length (force on the
plate by the rib = minus its inertia and elastic reaction). The plate equation
`D11 w_xxxx + 2 (D12 + 2 D66) w_xxyy + D22 w_yyyy + rho h w_tt = p` is integrated over
`x0 - e < x < x0 + e`; only `D11 w_xxxx` contains a delta, giving `D11 [w_xxx] = -(E I w_yyyy + rhoA
w_tt)|_(x0)`; the `w_xyy` terms of the shear force are continuous because `X'` is. With
`w_yyyy = beta^4 w` and `w_tt = -omega^2 w`:

    D11 [X'''(x0+) - X'''(x0-)] = -(E I beta^4 - rhoA omega^2) X(x0).

Sign check: a heavy rib without stiffness (`E I = 0`, `rhoA > 0`) lowers every root and the lowest
one by more than 5% (tested); a stiff light rib raises them. The test shoots from `x = 0` with
`X'(0)` and `X'''(0)` (a 4 x 4 matrix exponential of the first-order system per side, the jump
applied at `x0`), so the 8 x 8 determinant reduces to the 2 x 2 determinant of `X(Lx)` and
`X''(Lx)` against the two shooting parameters; roots are found by a 0.05 Hz scan for sign changes
and bisection to 1e-12 relative. Result for the B2-T4 plate with the single rib at `x0 = 0.3 m`: all
33 Levy frequencies below 706.7 Hz (the first 6 roots of n = 1 and n = 2 included) are matched by
the Richardson-extrapolated `plate_modes` frequencies (three grids, MAC-tracked) within 2.5e-4 (the
first ten within 3.4e-5); the observed orders are 1.95 to 2.0 except where tracked pairs are
nearly degenerate. Optimised builds run the full case (96 x 64 finest, 6 roots per family); debug
builds a smaller one (64 x 48 finest, 3 roots per family, 10 modes) to stay within 60 s.

**Other checks** (`reference/tests/test_plate_ribs.cpp`): the unribbed modal solve reproduces the 30
lowest `plate_discrete_frequency_hz` values within 1e-10 on two grids; ribs with E and rho 1e-20
times the plate's leave them unchanged within 1e-10; a full-width rib on the nodal line `x = Lx/2`
leaves every even-m mode unchanged within 1e-9; a rib scaled by 1e-4 shifts the ten lowest
non-degenerate frequencies by the closed-form first-order value
`2 S^2 (E I a_y^2 - omega^2 rhoA) / (rho h Lx)`, `S = sin(m pi x0 / Lx)`,
`a_y = (4/hy^2) sin^2(n pi / (2 ny))`, within 1%.

## Tests

IDs, names and tolerances are those of the phase card. Each case finishes within 60 s in
`linux-debug`; `P0.5-T4 plate simulation ...` needs a fine grid and is compiled only into optimised
builds (`CMAKE_BUILD_TYPE` Release or RelWithDebInfo, so it runs in the `*-rt-safety` presets and in
`macos-universal`'s release build on all three systems); everything else runs in every build.

| Test | Case | Where |
| --- | --- | --- |
| P0.5-T1 | string frequencies converge at second order (B about 1e-4 and 1e-2, N = 32 .. 256, `k` proportional to `h`, first five partials, order from the two finest grids within 10% of 2, monotone error); cross-check against the scheme's exact discrete dispersion relation to 1e-7 | all builds |
| P0.5-T2 | lossless energy per step: string; string + lumped bridge (two strings, rotated axes); string + hammer; string + barrier; string with tension modulation; plate coupled to a string; ribbed plate (two crossing ribs) coupled to a string | all builds |
| P0.5-T3 | hammer against a rigid wall, m = 8 g, v = 3 m/s, p = 2.5, K = 1e9, 4.5e9, 2e10 N/m^p (piano felts, Chaigne and Askenfelt 1994: about 4.5e9 for p = 2.5 at middle C), contact time within 0.1% and rebound -v within 1e-9 | all builds |
| P0.5-T4 | the ten lowest simply supported orthotropic modes from the discrete operator's eigenvalues (nx = ny = 240) within 0.1%; the lowest three from simulation (90 x 60 grid, 12 kHz) within 0.1% | operator: all builds; simulation: optimised builds |
| P0.5-R3 | lossless lumped bridge conserves energy to 1e-12 per step; flexible string on a pure conductance decays at `-f0 ln[(1-Z0 G)/(1+Z0 G)]` within 1% (worst 0.41%, N = 200, 48 kHz); two strings on one conductance (in-phase and anti-phase); horizontal polarisation; a bridge mass shifts the partials as the exact end-mass equation `cot(kL) = M k / rhoA` says, and SPEC-BRG's `-2 f0 Z0 Im Y` agrees with that shift within 5% | all builds |
| P0.5-R5 | string released onto a flat barrier: energy constant (1e-9 per step required, ORC-15 setup), non-increasing with contact damping; the two barrier sides mirror each other | all builds |
| P0.5-R6 | large-amplitude pluck conserves energy within 1e-11; pitch glides down and its shift scales as amplitude squared; longitudinal modes within 0.1% of `K_m sqrt(EA/rhoA)` | all builds |
| P0.5-R7 | scenario errors, features, WAV, energy JSON, refinement tool, CLI (ctest), Python module (`tools/tests/test_p0_5_r7_python.py`; `plate_modes` and the CLI `plate-modes` in `tools/tests/test_p0_5_ribs_python.py`) | all builds; Python in the `reference-python` job |
| P0.5-R4 ribs | the cases of "Ribs" above (`reference/tests/test_plate_ribs.cpp`); the convergence and Levy cases run reduced grids in debug builds | all builds |

The plate for P0.5-T4 is spruce-like: `E_L = 10 GPa`, `E_R = 0.5 GPa`, `G_LR = 0.6 GPa`,
`nu_LR = 0.37`, `nu_RL = nu_LR E_R/E_L`, `rho = 400 kg/m^3`, `h = 3 mm`, 0.6 m x 0.4 m (grain along
the 0.6 m side). The ten lowest modes are (1,1), (1,2), (2,1), (1,3), (2,2), (2,3), (1,4), (3,1),
(3,2), (2,4), 25.9 Hz to 208 Hz.

## Using the reference from later phases

Render a scenario with `pme_reference_cli render` (or `pme_reference.render`) and compare the
engine's features with `pme_golden_compare` / `pme_analysis.golden`. Partials of the first string,
bridge or plate observation are indexed `round(f / f1Target)`. Lossless partials report `t60 = 1e12 s`.
Compare against the reference only with `b2 = 0` and, for plates, rectangles (ribs only axis-parallel
and on grid lines; `pme_reference.plate_modes` and `pme_reference_cli plate-modes <scenario.json>
--count N [--points x,y,...] [--grid nx,ny]` give the discrete modes, see "Ribs").

**Accuracy of the reference itself.** The scheme is second order, so a partial of index n on a
grid of N intervals is low by about `(n pi / N)^2 / 12` (space) plus `(omega k)^2 / 12` (time, with
`theta = 1/4`). Phases that compare partial frequencies at the 0.05 cent level (A2-T3, A4-T3,
C1-T2) must either use a grid and time step fine enough for the partials they compare, or take the
Richardson-extrapolated values that `pme_reference_cli refine` reports (`extrapolated_hz`,
three grids with a constant ratio). Decay rates are accurate to a few 1e-4 on the grids of the
P0.5-R3 tests. The exact discrete frequencies of the scheme are available in closed form
(`string_scheme_frequency_hz`, `plate_scheme_frequency_hz`) for a lossless pinned string and a
plate, and the tests check the simulation against them (to 1e-7 for the string and 1e-5 for the plate).
