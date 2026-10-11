# SAV framework and low-rank solve (A3)

SPEC sections implemented: SPEC-NL (all of "Nonlinear framework": coordinates, SAV formulation,
discrete scheme, explicit low-rank solve, skipping, drift control, contracts), with SPEC-OSC
"Difference form" and "Discrete energy" for the oscillator of every coordinate, and SPEC-CONV
(float64 reductions, float32 run-time states). interfaces-v2 declares no nonlinear API
(`pme::nonlinearApiDeclared == false`): everything here is internal to the engine, under
`engine/src/nonlinear/include/pme/nonlinear/sav.hpp` (namespace `pme::nl`), reachable through the
`pme_nonlinear` target (which links `pme_osc`). **A5** (hammer potential and dashpots), **A6**
(rank-one damper), **A7** (tension modulation), **P1/P2** (plucks, barriers) and **A8** (vectorising
the scalar kernel) build on it.

| File | Content | Requirement |
| --- | --- | --- |
| `sav.hpp` | `Potential`, `Dashpot`, `SavParams`, `SavCounters`, `SavSegment`, `lowRankSolve`, `savStep`, `savEnergy`, `savDissipation` | A3-R1 to A3-R5 |
| `sav.cpp` | Implementation; explicit instantiation for `float` and `double` | A3-R1 to A3-R5 |

## Internal API

- `SavSegment<S>` (`S` = `float` in the engine, `double` in tests). `configure(nModes, nLumped,
  maxDashpots, fs, params)` allocates everything (non-real-time) and returns
  `Status::InvalidArgument` for `maxDashpots` > 3, fs <= 0, no coordinates or a non-positive
  parameter, leaving the segment unchanged. Coordinates are stacked as modes first, then lumped
  coordinates. `setCoordinate(i, OscCoeffsF64, mass)` (from `compileOscillatorF64`) and
  `setLumped(i, mass)` (beta = 1, kappa = 0, gamma = dt^2) fill them; `q()`, `u()` give the state;
  `qPrev()`, `uPrev()` the state before the last step; `psi()` is psi^(n+1/2). `initialisePsi`
  sets psi = sqrt(2(V + eps0)) for the present state; `setPsi` overrides it; `resetState` zeroes
  the state. All arrays are 64-byte aligned (A1 `AlignedArray`) and padded to a multiple of 16;
  padding lanes are never written.
- `Potential<S>`: `double value(span<const S> z)` returning V >= 0 (exactly 0 when inactive) and
  `void gradient(span<const S> z, span<double> grad)` overwriting `grad`. Several potentials add
  and share one psi. The gradient buffers belong to the segment.
- `Dashpot<S>`: `{span<const S> c, double R, bool alongGradient}`; the caller sets `R` every sample
  and sets `alongGradient` when `c` is parallel to g (one contact), which keeps r = 1.
- `savStep(seg, potentials, dashpots, explicitForce)`: one sample. `explicitForce` is F^n in N per
  coordinate (it is divided by the mass inside); an empty span means no force. At most
  `maxDashpots` dashpots are read.
- `lowRankSolve(z, a, n, r, h, w)`: the Woodbury solve alone (A3-T1 calls it directly).
- `savEnergy(seg)`: sum m_i H_i + psi^2/2 - eps0 (float64, index order). `savDissipation(seg,
  dashpots)`: dt sum_j R_j (c_j^T delta_t. z^n)^2 for the last step.
- `SavCounters` (`seg.counters()`): `resets`, `driftCorrections`, `solvesSkipped`, plain
  `std::uint32_t`, monotonic; the runtime (C1) reads and clears them on the audio thread.

**ModeBank.** A1's `ModeBank` holds float32 state only, but A3-T1 to A3-T4 need a float64
instantiation, so `SavSegment<S>` keeps its own structure-of-arrays state of type `S` (using A1's
`AlignedArray` and the same scalar `stepModes<S, S>` kernel for the skip path). With `S = float`
the layout and the arithmetic equal `ModeBank`'s; the test "A3-R4 the skip rule is bit-identical
to the A1 free update" checks it against `ModeBank::step`.

## Derivation of a and its signs (A3-R2)

SPEC-NL's scheme for coordinate i, with psibar = (psi^(n+1/2) + psi^(n-1/2))/2:

    delta_tt z_i + 2 sigmaHat_i delta_t. z_i + omegaHat_i^2 z_i
        = (1/m_i) [ F_i - g_i psibar - R c_i c^T delta_t. z ],
    psi^(n+1/2) = psi^(n-1/2) + (1/2) g^T (z^(n+1) - z^(n-1)).

Substituting the psi update gives psibar = psi^(n-1/2) + (1/4) g^T (z^(n+1) - z^(n-1)), so

    - g_i psibar = - g_i psi^(n-1/2) - g_i (g^T z^(n+1))/4 + g_i (g^T z^(n-1))/4,
    - R c_i c^T delta_t. z = - R c_i (c^T z^(n+1))/(2 dt) + R c_i (c^T z^(n-1))/(2 dt).

The oscillator update in difference form (SPEC-OSC) maps a force per unit mass f to
z^(n+1) = [q + beta u - kappa q] + gamma f, i.e. the free update plus gamma f. With
h_g = gamma g/m, w_g = g/4, h_c = R gamma c/m, w_c = c/(2 dt):

    z^(n+1) = a_free + gamma F/m  - h_g psi^(n-1/2) + h_g (w_g^T z^(n-1)) + h_c (w_c^T z^(n-1))
              - h_g (w_g^T z^(n+1)) - h_c (w_c^T z^(n+1)),

so z^(n+1) = a - sum_j h_j (w_j^T z^(n+1)) with

    a = (q^n + u_free) + gamma F/m + sum_j h_j (w_j^T z^(n-1)) - h_g psi^(n-1/2),

where u_free = beta u^n - kappa q^n and z^(n-1) = q^n - u^n. This is what the code evaluates (in
float64 from the float32 state). Then z^(n+1) = a - H (I + W^T H)^(-1) W^T a (Woodbury); u^(n+1) =
z^(n+1) - q^n; psi^(n+1/2) = psi^(n-1/2) + (1/2) g^T (u^(n+1) + u^n), because z^(n+1) - z^(n-1) =
u^(n+1) + u^n.

**Energy balance of the dashpot.** The dashpot contributes -R (c^T v)^2 of power with v = delta_t.
z^n = (u^(n+1) + u^n)/(2 dt); `savDissipation` returns dt R (c^T v)^2 summed over dashpots, and the
tests check E^(n+1) - E^n + dissipation = 0 to 10^-15 relative.

**Dashpot along g (merge into rank 1).** If c = lambda g then h_c w_c^T = R lambda^2 h_g g^T/(2 dt),
so the SAV column becomes h = gamma g/m, w = g (1/4 + sum R lambda^2/(2 dt)), with lambda = c^T g /
g^T g in float64; the known term of the merged dashpot is already included because the code forms
h (w^T z^(n-1)). The flag is trusted: a dashpot flagged `alongGradient` whose direction is not
parallel to g gives a wrong (but still stable) update. When g = 0 (V = 0) the flag is ignored and
the dashpot is an ordinary column. Dashpots with R = 0 add no column.

## Low-rank solve (A3-R3)

r = (1 if V > 0) + (number of dashpots with R > 0 not merged) <= 4. r = 1 is a separate closed
form, z = a - h (w^T a)/(1 + w^T h). For r = 2..4 the r x r matrix M = I + W^T H and W^T a are
formed in float64 in fixed order, M y = W^T a is solved by Gaussian elimination with partial
pivoting on `std::array` storage, and z = a - H y. M has real eigenvalues >= 1 (H and W are
positive diagonal scalings of the same directions), so it is never singular; debug builds assert a
non-zero pivot. Every reduction runs over i = 0..n-1 in order. Result: A3-T1 worst relative error
against a dense LU solve 1.2 x 10^-14 over 1000 systems (limit 10^-12).

## Rules (A3-R4)

- **Skip (MUST).** When V^n = 0 and every R^n = 0 the step is `stepModes` (the A1 kernel) with the
  force F/m, no gradient evaluation, no solve; `solvesSkipped` increments; V^(n-1) is set to 0.
  If V^n = 0 but some R^n > 0 the step is solved with the dashpot columns only (g = 0, psi unchanged).
- **Reset at separation (MUST).** When V^n = V^(n-1) = 0, psi is set to sqrt(2 eps0) if
  |psi| > sqrt(2 eps0), and `resets` increments. SPEC-NL states psi <- sqrt(2 eps0); the condition on
  |psi| is needed because psi is a linear variable that can be negative (observed: -3 x 10^-4 at
  separation in the A3-T4 bounce). Setting psi to the positive value lowers psi^2 and so only removes
  energy; when |psi| is already at or below the threshold nothing is done (raising it would add
  energy of order eps0).
- **Persistent contact (SHOULD, implemented).** The SPEC relaxation fallback, not power-balanced
  regulation: when V has been positive for more than `driftAfterSeconds` (default 50 ms,
  rounded to samples) of consecutive samples and |psi| > psi* = sqrt(2(V^n + eps0)), the magnitude is
  relaxed, |psi| <- psi* + (|psi| - psi*) exp(-dt/tau), tau = `driftTau` (default 10 ms), keeping the
  sign. It only lowers |psi| and so only removes energy; `driftCorrections` increments. It is
  switched off per segment with `SavParams::driftRelaxation = false` (A3-T2 and A3-T3 do).
  Power-balanced regulation was not implemented.
- **Initial psi.** `configure` and `resetState` start at sqrt(2 eps0) (no contact). Call
  `initialisePsi` when the state starts inside a contact.

## Precision

`S` is the state type; psi, V, eps0, every reduction (w^T a, w^T h, g^T z^(n-1), the energy, the
dashpot sums) and the r x r solve are float64 whatever `S` is. The coefficients beta, kappa, gamma
are rounded to `S` (exactly as `ModeBank`); a, H, W and g are float64 scratch. u^(n+1) = z^(n+1) -
q^n is rounded to `S` and q^(n+1) = q^n + u^(n+1) is formed in `S`, so the stored state is the
float32 difference form of SPEC-OSC. No algebraic rearrangement of SPEC-NL's update is made, there
is no fast-math, and the reduction order is fixed (index order), so results are bit-identical for
any thread count on one build.

## Real-time rules

`savStep`, `savEnergy`, `savDissipation`, `lowRankSolve`, `initialisePsi`, `setParams` and
the accessors are `noexcept` and allocation-free; only `configure` allocates (non-real-time). The
test "A3-R2 savStep with r = 1 and r = 4 allocates nothing" runs them under
`pme::rtcheck::ScopedRealtimeThread` (label `rt`), float32, so the `rt-safety` preset aborts on any
allocation. `Potential::value` and `gradient` implementations (A5, A7, P1, P2) must obey the same
rules.

## Tests

| Test | File | Result (GCC and Clang, debug, release) |
| --- | --- | --- |
| A3-T1 Woodbury vs dense LU, 1000 random systems, n_z 1..256, r 1..4 | `engine/tests/nonlinear/test_solve.cpp` | worst relative error 1.2 x 10^-14 |
| A3-T2 energy drift, 64 lossless modes, 10^5 steps, float64, drift rule off | `test_energy.cpp` | quartic 9.6 x 10^-14, tension-modulation 1.2 x 10^-14 (limit 10^-10) |
| A3-T3 dashpot balance, r = 1, 2, 4, constant and state-dependent R | `test_energy.cpp` | worst step increase 3.8 x 10^-16 (limit 10^-13), worst balance error 1.2 x 10^-15 (limit 10^-12) |
| A3-T4 mass between two one-sided walls, 20 contacts | `test_contact.cpp` | 20 resets for 20 separations, none during contact, energy never raised |
| A3-R1, A3-R4, A3-R5 configuration, padding, skip, drift relaxation (first correction at 2400 samples = 50 ms) | `test_contact.cpp` | |
| A3-R2 RT safety | `test_rt.cpp` (label `rt`) | |
| Benchmark `pme_nonlinear_bench` ("coordinate-samples", 64 samples per call; label `perf`) | `sav_bench.cpp` | see below |

ORC-08 (rigid-wall contact time) is written by the A3 test-author. There is no baseline file until
V2 and decision D-4 define the reference machines. Release-build medians on the development machine
(per call of 64 samples; a linux-release build, GCC): 65 coordinates r = 1 74.7 us, r = 4 173.8 us;
257 coordinates r = 1 136.8 us, r = 4 438.3 us; the skip path at 257 coordinates 42.6 us.
