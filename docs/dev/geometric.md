# Geometric nonlinearity and longitudinal waves (A7)

SPEC sections implemented: SPEC-GEO (all of "Geometric nonlinearity and longitudinal waves", default
tier, including the SI-0015 energy paragraph), SPEC-NL ("SAV formulation", "Skipping", "Drift
control"), SPEC-BRG ("Bridge force", "Discrete coupling order"), SPEC-EXEC (per-sample steps 2, 5 to
8), SPEC-OSC ("Difference form", "Bandwidth", "Discrete energy"), SPEC-CONV ("Quality and numerical
options"). Internal to the engine (interfaces-v2 declares no geometric API); phase A10 adds the exact
tier and C1 wires the segment in (C1-R4).

| File | Content | Requirement |
| --- | --- | --- |
| `engine/src/nonlinear/include/pme/nonlinear/tension.hpp`, `tension.cpp` | `nl::TensionModulationPotential<S>` | A7-R1 |
| `engine/src/string/include/pme/string/longitudinal.hpp`, `longitudinal.cpp` | `LongitudinalModes`, `compileLongitudinal`, `axialStiffness`, `longitudinalForce<S>`, `slopeAtBridge<S>`, `longitudinalBridgeForce<S>`, `LongitudinalBank<S>` | A7-R2, A7-R3, A7-R4 |
| `engine/src/string/include/pme/string/longitudinal_segment.hpp` | `GeometricConfig`, `BridgeCoupling`, `GeometricSegment<S>`, `geometricConfigFrom` | A7-R1, A7-R4, A7-R5 |
| `engine/src/string/include/pme/string/longitudinal_tier.hpp`, `longitudinal_tier.cpp` | `GeometricTier`, `GeometricQuality`, `resolveGeometricTier`, `selectGeometricTier` | A7-R6 |

Build wiring: `pme_string` links `pme_nonlinear` (declared in `engine/tests/geometric/CMakeLists.txt`,
because `GeometricSegment` composes the SAV segment with the string tables). `pme_nonlinear` does not
depend on `pme_string`: the potential takes plain numbers. `GeometricSegment` includes no bridge header;
the bridge point is a template parameter of `addBridgeForces` (`bridge::BridgePoint<S>` in the engine).

## Units

Transverse coordinates q_n have unit m^(3/2) (orthonormal shapes, SPEC-STR), so do the longitudinal
coordinates xi_m. `longitudinalForce` returns f_m^L **per unit modal mass** in m^(3/2)/s^2, which is the
`force` argument of `osc::stepModes` (gamma f). Energies multiply the per-unit-mass H by rho A. S has unit m,
V_KC and Delta T are in J and N. Compile-time arithmetic is float64; run-time states are `S` (float in
the engine, double in tests); every reduction is float64 in fixed index order. All force and S functions
are templates on `S`, instantiated for `float` and `double` (ORC tests use `double`).

## Tension modulation (A7-R1)

`TensionModulationPotential<S>::configure(k2, c)` with k2 = k_n^2 and c = (EA - T)/(2L). With
S = sum_n k_n^2 (q_v,n^2 + q_h,n^2): `sum(z)` = S, `deltaT(z)` = c S (SPEC Delta T), `value(z)` = c S^2/4 =
(EA - T) S^2/(8L), `gradient(z, g)` = Delta T k_n^2 q_.,n (lumped entries set to 0).

SAV layout of a segment with tension modulation: the N vertical modes, then the N horizontal modes, then
`lumped` coordinates (hammer ...). The potential shares psi with any other potential of the segment
(potentials add, the solve stays rank 1; test "A7-R1 ... shares psi ... rank 1" uses `maxDashpots = 0`).
Because V_KC > 0 whenever the string sounds, the skip rule never applies (`solvesSkipped` stays 0), and
A3's persistent-contact drift relaxation does (it only lowers |psi|, so it only removes energy; switch it
off with `SavParams::driftRelaxation = false` for conservation tests).

EA is the **core's** axial stiffness E_c pi d_c^2/4 (`axialStiffness`); rho A is the full linear mass.

## Longitudinal modes (A7-R2)

`compileLongitudinal(modes, params, ML, b1L, b3L, fs, fMax, out)`: K_m = m pi/L, omega_m^L = K_m sqrt(EA/rhoA),
sigma_m = b1L + b3L (omega_m^L)^2, coefficients by `osc::compileOscillatorF64`. The kept modes are the
longest prefix passing the SPEC-OSC bandwidth rule at (fs, fMax), at most ML; `kept()` reports the count,
which may be 0 (still `Ok`). EA - T <= 0 is `InvalidArgument`. The losses b1L, b3L are internal parameters
(default 0) until the ICR-0002 amendment adds `SegmentDesc::longitudinalB1`, `longitudinalB3`.
Kept modes for the test strings (plain steel 0.65 m, f_L1 = 3883 Hz): 5 at 48 kHz (fMax 20 kHz, or
infinite), 11 at 96 kHz with M_L = 16, 2 with fMax 10 kHz; a 0.1 m treble string keeps 0.

## Pair-sum forcing (A7-R3)

f_m^L = -(EA - T) K_m/(2 rhoA sqrt(2L)) sum_P sum_{i,j>=1} k_i k_j q_P,i q_P,j [delta_{i+j,m} + delta_{|i-j|,m}]
over ordered pairs. For each m: the sum over i with j = m - i, plus twice the sum over j with i = j + m, so
O(N) (the test counts reads: at most 2N + 2 per mode). Verified against the direct double sum (5e-16
relative) and against the modal projection quadrature -(EA - T)/(2 rhoA) integral (y_x^2 + z_x^2) chi_m' dx
(5e-15 relative): the SPEC normalisation and sign agree. The cosine-transform path for M_L > 16 is not
added (MAY).

**Order and start.** The force is computed from q^n before the transverse update of the same sample and
drives the longitudinal modes through the A1 difference form (SPEC-EXEC step 8, one-way, SI-0015).
**Start convention.** `GeometricSegment::startLongitudinal()` (call once after setting the initial
transverse state) sets the Taylor start u^0 = -gamma f^0/2 for xi^0 = 0 at rest, because xi^-1 = dt^2 f^0/2
to second order. Starting with u^0 = 0 makes the front arrive about dt/2 early (A7-T3's arrival budget).
SPEC-EXEC does not state a start rule; this is the choice the test author uses.

## Bridge force and hook (A7-R4)

`longitudinalBridgeForce` = EA sum_m sqrt(2/L) K_m (-1)^m xi_m + (EA - T)/2 (y_x(L)^2 + z_x(L)^2), with
y_x(L) = sum_n q_v,n sqrt(2/L) k_n (-1)^n (`slopeAtBridge`). Reading: SPEC-GEO sums both polarisations in
the PDE, so the quadratic term has y_x^2 + z_x^2. For a segment with `bridgeEnd` At0 the point is x = 0 and
the factors (-1)^m, (-1)^n are dropped (F_L is the tension excess at the bridge end). The result is
float64; the segment casts at the hook. If the longitudinal modes are enabled but none are kept
(bandwidth), the quasi-static (EA - T)/2 y_x^2 term is still transmitted.

`GeometricSegment<S>` per sample: `addBridgeForces(point)` (step 2: `addForce(F_b,v, F_b,h)`, then
`addVerticalForce(kappa_L F_L + kappa_T Delta T)` only when a transmission is active), the caller advances the
bridge, then `advance(point.accelV(), point.accelH())` (steps 5 to 8). Both terms enter at step n in both
bridge schemes. Test: the instrumented order is addForce, addVerticalForce, advance.

## Flags and tier (A7-R5, A7-R6)

`tensionModulation` enables the potential (and Delta T); `longitudinalModes` > 0 enables the longitudinal
modes and F_L; `kappaL`, `kappaT` scale the hook. A segment with everything off, and no lumped coordinates,
does not use the SAV and runs the arithmetic of `bridge::ModalSegment` (test: bit-identical state and
bridge displacement over 3000 samples, float32). Longitudinal modes without tension modulation are allowed.
`geometricConfigFrom(SegmentDesc, fMax)` maps the description's flags.

`GeometricTier { Auto, Default, Exact }` and `GeometricQuality { Realtime, HighQuality }` mirror ICR-0002
(values 0, 1, 2 and 0, 1). `resolveGeometricTier`: Auto is Default under Realtime and Exact under
HighQuality; `selectGeometricTier` returns `NotImplemented` for Exact until A10. `GeometricSegment::configure`
asks for the tier only when a geometric feature is enabled.

## Energy (SI-0015)

`transverseEnergy(wV, dwV, wH, dwH)`: the modes of both polarisations with the moving-support shift, the
(T/L) w w terms and psi^2/2 - eps0 (which carries V_KC); add `BridgePoint::modeEnergy()` for the bridge.
`longitudinalEnergy()` = sum rhoA H_m^L, reported separately. Measured (float64): rigid bridge, 5 mm pluck at
0.12 L, 5 longitudinal modes: worst per-step relative change 3.4e-15, drift over 1e5 samples 2.1e-13.
E_L/E_transverse (time mean over 0.2 s): 8.2e-3 at 1 mm, 3.3e-2 at 2 mm, 1.4e-1 at 4 mm; ratio of ratios
for 1 mm/2 mm 4.019, E_L ratio 16.13; for 2 mm/4 mm 4.23.

**Spec issue (movable bridge).** On a mass-spring bridge the balance is not exact: drift 3.7e-3 over 2e4
samples (5 mm pluck, 0.3 kg, 300 Hz). SPEC-BRG's F_b = sum a_n q_n - (T/L) w contains only the linear tension; with
tension modulation the transverse force on the bridge also contains Delta T y_x(L). Adding -Delta T y_x(L)
to the vertical force (a diagnostic in the test, not implemented) lowers the drift to 5.2e-5; the rest is the w^2/L
term of S (the support-motion slope). Reported to the planner.

## A7-T1: Duffing derivation

Exciting mode 1 only keeps every other gradient zero (Delta T k_n^2 q_n = 0), so mode 1 obeys
q'' + w1^2 q + beta q^3 = 0, beta = (EA - T) k1^4/(2 L rhoA). Harmonic balance with q = a cos(w t): the
cubic term gives (3/4) a^3 cos(w t) in the fundamental, so w^2 = w1^2 + (3/4) beta a^2. With
w1 the measured small-amplitude frequency, the prediction is f(a) = f1 sqrt(1 + (3/4) beta a^2/w1^2).

For a multi-mode pluck, mode 1 sees Delta T(t) = c sum k_m^2 q_m^2; only the time average of the other
modes' squares is secular, and its own term gives the 3/4 factor:
dw1 = k1^2 c/(2 rhoA w1) [ sum_{m != 1} k_m^2 a_m^2/2 + (3/4) k1^2 a1^2 ].

Measured: windows of 20 cycles between upward zero crossings (linear interpolation), amplitude
a^2 = 2 mean(q^2) over the window, window kept when the predicted shift exceeds 20 f1^2 dt/20 = 20 estimator
resolutions (1.4 Hz). Lossless, 40 cents: 26 of 26 windows, ratio measured/predicted in [1.00106, 1.00140].
b1 = 0.5 1/s, 50 cents: 23 of 52 windows, ratio in [1.0012, 1.0127]. Triangle pluck at 0.12 L, 40-cent
single-mode-equivalent scale: predicted mode-1 glide 22.8 cents, measured mean 22.4 cents.

## A7-T2: phantom partials

String d = 2.2 mm (B = 1.55e-3), f1 = 262 Hz, 5 mm pluck at 0.12 L, 5 longitudinal modes kept, kappa_L =
0.026, lossless 0.3 kg 300 Hz bridge, 2 s, Hann window, FFT 131072 (bin 0.366 Hz). Peak above the local
median floor / weaker in the control (longitudinal modes off, kappa_L = 0): (1,2) 20.8 / 37.1 dB, (1,3)
26.1 / 43.0, (1,4) 39.9 / 64.6, (2,3) 38.8 / 68.9, (2,4) 33.5 / 71.5, (3,4) 33.3 / 40.2 dB. The sums use the
measured partial frequencies (the pitch glide moves every partial by about 1 percent).

## Real-time rules

Everything reachable from `GeometricSegment::addBridgeForces`, `advance`, `startLongitudinal`, the energies,
`longitudinalForce`, `longitudinalBridgeForce` and the potential's `value`/`gradient` is `noexcept` and
allocation-free; `configure` allocates. Test: label `rt`, float32, under `ScopedRealtimeThread`.

## Tests

`engine/tests/geometric/`: `test_potential.cpp` (A7-R1), `test_longitudinal.cpp` (A7-R2, A7-R3, A7-R4),
`test_segment.cpp` (A7-R4, A7-R5, A7-R6), `test_energy.cpp` (SI-0015), `test_glide.cpp` (A7-T1),
`test_phantom.cpp` (A7-T2), `test_rt.cpp`, `geometric_bench.cpp` (label `perf`, no baseline: D-4, V2).
ORC-11, ORC-12 and A7-T3 belong to the A7 test author.
