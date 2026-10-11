# Reference solver (P0.5)

SPEC sections implemented: SPEC-STR ("Stiff string model": wound strings, tension for pitch, modes,
losses), SPEC-BRG ("Bridge coupling": moving support, polarisation coupling, the small-admittance
and conductance results), SPEC-HAM ("Hammer and action": contact law, Hunt-Crossley term, Oracle),
SPEC-BODY ("Soundboard and bodies": Kirchhoff plate, D11, D22, D12, D66), SPEC-PLK ("Plucked
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
| R4 | orthotropic Kirchhoff plate, SPEC-BODY | `K = hx hy [D11 Dxx^2 + D22 Dyy^2 + 2 (D12 + 2 D66) Dxx Dyy]` on a rectangle, simply supported; the mixed term in the commuting form so that the discrete sine modes are exact eigenvectors; bilinear spreading and interpolation at bridge points; B2009 12.1 | 2 in `h`, 2 in `k` | as R1 |
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
or 50 iterations; non-convergence throws `RefError` (never silent). The two-step discrete gradient
`(V(g+) - V(g-))/(g+ - g-)` is evaluated with `expm1/log1p` so that it has no cancellation.
Hunt-Crossley damping is `R = K mu [g^n]_+^p` times the centred gap velocity (explicit in `g`, hence
dissipative), lumped per contact point for barriers.

**Round-off.** `K x` is evaluated through first differences and differences of differences
(`string_ops.cpp`), because `x_(i+1) - x_i` is exact for smooth data; this keeps the energy monitor
at about 1e-14 relative per step on fine grids instead of eps N^2. Starting at rest uses the
time-symmetric start `x^(-1) = x^0 - k^2/2 (M + theta k^2 K + k C/2)^(-1) K x^0`.

## Tests

IDs, names and tolerances are those of the phase card. Each case finishes within 60 s in
`linux-debug`; `P0.5-T4 plate simulation ...` needs a fine grid and is compiled only into optimised
builds (`CMAKE_BUILD_TYPE` Release or RelWithDebInfo, so it runs in the `*-rt-safety` presets and in
`macos-universal`'s release build on all three systems); everything else runs in every build.

| Test | Case | Where |
| --- | --- | --- |
| P0.5-T1 | string frequencies converge at second order (B about 1e-4 and 1e-2, N = 32 .. 256, `k` proportional to `h`, first five partials, order from the two finest grids within 10% of 2, monotone error); cross-check against the scheme's exact discrete dispersion relation to 1e-7 | all builds |
| P0.5-T2 | lossless energy per step: string; string + lumped bridge (two strings, rotated axes); string + hammer; string + barrier; string with tension modulation; plate coupled to a string | all builds |
| P0.5-T3 | hammer against a rigid wall, m = 8 g, v = 3 m/s, p = 2.5, K = 1e9, 4.5e9, 2e10 N/m^p (piano felts, Chaigne and Askenfelt 1994: about 4.5e9 for p = 2.5 at middle C), contact time within 0.1% and rebound -v within 1e-9 | all builds |
| P0.5-T4 | the ten lowest simply supported orthotropic modes from the discrete operator's eigenvalues (nx = ny = 240) within 0.1%; the lowest three from simulation (90 x 60 grid, 12 kHz) within 0.1% | operator: all builds; simulation: optimised builds |
| P0.5-R3 | flexible string on a pure conductance decays at `-f0 ln[(1-Z0 G)/(1+Z0 G)]` within 1%; two strings on one conductance (in-phase and anti-phase); horizontal polarisation | all builds |
| P0.5-R5 | string released onto a flat barrier: energy constant (1e-9 per step required, ORC-15 setup), non-increasing with contact damping; the two barrier sides mirror each other | all builds |
| P0.5-R6 | large-amplitude pluck conserves energy within 1e-11; pitch glides down and its shift scales as amplitude squared; longitudinal modes within 0.1% of `K_m sqrt(EA/rhoA)` | all builds |
| P0.5-R7 | scenario errors, features, WAV, energy JSON, refinement tool, CLI (ctest), Python module (`tools/tests/test_p0_5_r7_python.py`) | all builds; Python in the `reference-python` job |

The plate for P0.5-T4 is spruce-like: `E_L = 10 GPa`, `E_R = 0.5 GPa`, `G_LR = 0.6 GPa`,
`nu_LR = 0.37`, `nu_RL = nu_LR E_R/E_L`, `rho = 400 kg/m^3`, `h = 3 mm`, 0.6 m x 0.4 m (grain along
the 0.6 m side). The ten lowest modes are (1,1), (1,2), (2,1), (1,3), (2,2), (2,3), (1,4), (3,1),
(3,2), (2,4), 25.9 Hz to 208 Hz.

## Using the reference from later phases

Render a scenario with `pme_reference_cli render` (or `pme_reference.render`) and compare the
engine's features with `pme_golden_compare` / `pme_analysis.golden`. Partials of the first string,
bridge or plate observation are indexed `round(f / f1Target)`. Lossless partials report `t60 = 1e12 s`.
Compare against the reference only with `b2 = 0` and, for plates, rectangles.
