# Bridge coupling (A4)

SPEC sections implemented: SPEC-BRG "Decomposition", "Modal equation with a moving support",
"Bridge force", "Discrete coupling order", "Unison coupling", "Polarisation coupling" and
"Contract"; the parts of SPEC-BODY "Local bridge model (per bridge point, per sample, exact)",
"Sympathetic path" and "Contracts" that a bridge point needs (the model, the `w_ext` input and the
modal-set interface); SPEC-EXEC "Per-sample loop of one note group" steps 2 to 4; SPEC-GEO
"Transmission to the body" (hook only); SPEC-OSC "Difference form", "Derived quantities", "Discrete
energy" and "Bandwidth" for the bridge modes; SPEC-CONV (float64 at compile time, float32 run-time
states, time convention e^(+iωt) of SI-0012).

The public entry point is `pme::compileBridgeTerms` (`engine/include/pme/bridge.hpp`, frozen at
interfaces-v2). Everything else is internal, under `engine/src/bridge/include/pme/bridge/`
(namespace `pme::bridge`), reachable from tests, A5, A7, B3 and C1 through the `pme_bridge` target
(which links `pme_string` and `pme_osc`).

## Internal API

| Header | Content | Requirement |
| --- | --- | --- |
| `terms.hpp` | `liftingOverlaps` (g\_n), `forceCoefficients` (a\_n, either polarisation), `horizontalForceCoefficients`, `bridgeForce` (templates and the float overload) | A4-R1 |
| `model.hpp` | `ParallelModel`, `conductanceModel`, `massSpringDamperModel`, `modalSetModel(const LocalBridge&)`, `compileBridgeMode` | A4-R2 |
| `bridge_point.hpp` | `BridgePoint<S>`: bridge model of one point, rotation, hooks, energy monitors | A4-R2 to A4-R5 |
| `modal_segment.hpp` | `ModalSegment<S>`: the bridge-facing half of one string segment (mode states, F\_b, the modal force −g\_n δ\_tt w, energy). Reference for the segment side; A5, A7 and C1 mirror or reuse it | A4-R2, A4-R3 |

`S` is the state type: `float` in the engine, `double` in the energy and shift tests.

### Bridge terms (A4-R1)

`compileBridgeTerms(modes, end, out)`: g\_n = √(2/L)(−1)^(n+1)/k\_n for `AtL`, √(2/L)/k\_n for `At0`
(SPEC-BRG "Modal equation with a moving support"), a\_n = ρA ω̂²\_n g\_n with ω̂²\_n =
`modes.oscV[n].omegaHat2` (SPEC-BRG "Bridge force": the discrete ω̂²), `tOverL` = T/L. Computed in
float64 and rounded to float32. `InvalidArgument` (out unchanged) for empty modes, inconsistent
vector lengths (`k`, `omega`, `sigma`, `oscV`, `oscH`), L, T or ρA ≤ 0, or a non-finite value
(including k\_n ≤ 0 and non-finite ω̂²).

`SegmentBridgeTerms` holds one `a`, the vertical vector. The horizontal polarisation has its own
discrete ω̂²\_h,n (detuned by `detuneH`), and SPEC-BRG requires the ω̂² of the coupled polarisation,
so `horizontalForceCoefficients(modes, end, aH)` returns a\_h,n = ρA `oscH[n].omegaHat2` g\_n
(float32). A member `aH` in `SegmentBridgeTerms` is a candidate for the next ICR; `bridge.hpp` is
unchanged.

`bridgeForce<S>(terms, q, wb)` = Σ a\_n q\_n − (T/L) wb: one dot product in fixed index order
(`S` accumulator; the scalar reference of the A8 kernel) plus the static term. The same with an
explicit coefficient span, `bridgeForce<S, C>(span<const C>, tOverL, q, wb)`, serves the horizontal
vector. `bridge::bridgeForce(terms, const float*, float)` is the engine's float function of the SPEC
contract.

### Parallel-axis models (A4-R2)

One representation for all three kinds: M bridge modes (Ω\_k, σ\_k, Φ\_k) advanced in the A1
difference form with modal force Φ\_k F\_∥ⁿ, a conductance G (w\_Rⁿ⁺¹ = w\_Rⁿ + Δt G F\_∥ⁿ), and
w\_∥ = Σ Φ\_k r\_k + w\_R + w\_ext (SPEC-BODY "Local bridge model").

| Kind | Factory | Modes | Notes |
| --- | --- | --- | --- |
| Conductance | `conductanceModel(G)` | 0 | G = 0 is a rigid bridge |
| Mass–spring–damper | `massSpringDamperModel(m, k_b, r_b)` | 1: Ω = √(k\_b/m), σ = r\_b/(2m), Φ = 1/√m | w'' + 2σw' + Ω²w = F/m. A mode the bandwidth rule refuses makes `configure` return `Rejected` |
| Modal set | `modalSetModel(const LocalBridge&)` | M, G = `conductanceHf` | the interface B3's `buildLocalBridge` output plugs into; modes the bandwidth rule refuses are dropped (`droppedModes()`), as SPEC-OSC "Bandwidth" requires |

**Free mass.** k\_b = 0 gives Ω = 0, which A1's `compileOscillatorF64` refuses (σ ≥ ω, "overdamped"
even for σ = 0). `compileBridgeMode` handles Ω = 0 explicitly: ω̂² = 0, σ̂Δt = tanh(σΔt),
κ = 0, β = (1 − σ̂Δt)/(1 + σ̂Δt), γ = Δt²/(1 + σ̂Δt) (the overdamped continuation of the SPEC-OSC
formulas as Ω → 0; the poles are exactly 1 and e^(−2σΔt)). For Ω > 0 it calls A1 unchanged. A mass
with a damper and no spring (k\_b = 0, r\_b > 0) is therefore valid; a spring-mass-damper with σ ≥ Ω
is refused by the SPEC-OSC rule (`Rejected`). Whether the bandwidth rule should apply to heavily
damped bridge modes is a SPEC question; nothing depends on it in A4.

`BridgePoint<S>::configure(desc, model, fs)` compiles the coefficients at fs (non-audio, allocates).
The per-sample members are `noexcept` and allocation free. The per-sample order (SPEC-BRG "Discrete
coupling order", SPEC-EXEC steps 2 to 4):

1. each segment computes F\_b,s from qⁿ and `wV()` / `wH()` (= wⁿ) and calls `addForce(F_v, F_h)` in
   segment-index order (and `addVerticalForce` for the SPEC-GEO hook);
2. `advance()`: rotate (F\_v, F\_h) to the bridge frame, advance the modes and the conductances with
   the force at step n to wⁿ⁺¹, add the integrated external motion, rotate back, form δ\_tt wⁿ;
3. each segment reads `accelV()` / `accelH()` and advances its modes with −g\_n δ\_tt wⁿ.

`stage()` reports Collecting / Advanced for tests of this order. δ\_tt wⁿ is formed from the
increments wⁿ⁺¹ − wⁿ, which the point keeps (modes' uⁿ⁺¹, conductance increments), so no large
displacement values are subtracted: (Δwⁿ⁺¹ − Δwⁿ)/Δt².

**Energy monitors** (float64, for tests): `modeEnergy()` is Σ\_k [½(u\_k/Δt)² + ½Ω̂²\_k r\_kⁿ r\_kⁿ⁺¹]
(SPEC-OSC "Discrete energy", unit modal mass); `conductanceDissipatedEnergy()` integrates F times
the centred velocity of the conductance displacement, G(Fⁿ + Fⁿ⁻¹)/2, for G (∥) and G\_h (⊥).

### Unison sharing (A4-R3)

Any number of segments (`AtL` speaking, `At0` rear or duplex) call `addForce` on one `BridgePoint`;
all see the same wⁿ and δ\_tt wⁿ. The forces are summed in call order, so the result is
bit-reproducible for a fixed segment order; another attaching order differs by summation rounding
only (tested at 10⁻⁹ of the peak displacement). A caller that compiles a rear segment with a mute
passes `StringParams::detuneH` as the fourth argument of `pme::string::applyMute` (A2 review).

### Polarisation rotation (A4-R4)

F\_∥ = cos θ\_b F\_v + sin θ\_b F\_h, F\_⊥ = cos θ\_b F\_h − sin θ\_b F\_v; w\_v = cos θ\_b w\_∥ − sin θ\_b
w\_⊥, w\_h = sin θ\_b w\_∥ + cos θ\_b w\_⊥, i.e. R(θ\_b) = [[c, s], [−s, c]] and the bridge's ∥ axis is
(cos θ\_b, sin θ\_b) in the (v, h) plane. SPEC-BRG fixes R(θ\_b) only by name; this sign convention
is an assumption (θ\_b → −θ\_b mirrors the sense of the h–v energy exchange and nothing else). The
⊥ axis is a pure conductance: w\_⊥ⁿ⁺¹ = w\_⊥ⁿ + Δt G\_h F\_⊥ⁿ with G\_h =
`BridgePointDesc::conductanceH`. At θ\_b = 0, c = 1 and s = 0 exactly, so the polarisations are
independent bit for bit (tested against single-axis runs).

### Hooks (A4-R5), inert by default

- `setExternalVelocity(v_ext)`: w\_extⁿ⁺¹ = w\_extⁿ + Δt v\_extⁿ, added on the ∥ axis in `advance`
  (SPEC-BODY "Sympathetic path", SPEC-EXEC step 3). The value persists until changed; A7 sets it once
  per sub-block or sample. w\_ext is part of w, so the strings see it in the static term and in
  δ\_tt w.
- `addVerticalForce(f)`: adds to F\_v before the rotation, for κ\_L F\_L + κ\_T ΔT (SPEC-GEO
  "Transmission to the body", SPEC-EXEC step 2). A7 computes F\_L and ΔT.

## Energy of the partitioned scheme (A4-T1)

Write p\_n = q\_n + g\_n w (the displacement coefficient including the lifting term). With σ = 0 the
string modes obey δ\_tt q\_n + ω̂²\_n q\_n = −g\_n δ\_tt w, i.e. δ\_tt p\_n = −ω̂²\_n q\_n, so the bridge
force can be written F\_b = −ρA Σ g\_n δ\_tt p\_n − (T/L) w. Substituting into the bridge-mode
equation δ\_tt r\_k + Ω̂²\_k r\_k = Φ\_k F\_∥ gives a single symmetric second-order system
M δ\_tt X + K X = 0 for X = (q\_n of all segments and polarisations, r\_k), with

- M = Gram matrix of the "velocity" form ρA Σ (δq\_n + g\_n δw)² + Σ (δr\_k)², where δw is the
  bridge displacement increment, w = R(θ\_b)ᵀ(Σ Φ\_k r\_k, w\_⊥) (the same operator appears in the
  string rows and, transposed, in the bridge rows, which is why the explicit order loses no
  symmetry);
- K = diag(ρA ω̂²\_n, Ω̂²\_k) plus the (T/L) wᵀw term.

The centred scheme M δ\_tt X + K X = 0 conserves, exactly in exact arithmetic,

```math
E^{n+1/2} = \sum_{s}\Big\{\tfrac{\rho A_s}{2}\sum_{n}\Big[\Big(\tfrac{u_n^{n+1} + g_n\,\Delta w_s}{\Delta t}\Big)^{2} + \hat\omega_n^2\,q_n^n q_n^{n+1}\Big] + \tfrac12\tfrac{T_s}{L_s}\,w_s^n w_s^{n+1}\Big\} + \sum_k\Big[\tfrac12\Big(\tfrac{u_k^{n+1}}{\Delta t}\Big)^2 + \tfrac12\hat\Omega_k^2\,r_k^n r_k^{n+1}\Big]
```

summed over segments s and both polarisations, with Δw\_s = wₛⁿ⁺¹ − wₛⁿ the increment of the
displacement seen by the segment's polarisation. It is the sum of the SPEC-OSC discrete energies of
the string modes (the string term ρA Σ H\_n with the velocity u\_n replaced by u\_n + g\_n Δw: the
moving-support kinetic energy), the bridge-mode energy, and the static (T/L) w wⁿ⁺¹ term. No
separate coupling term is needed because the SPEC order is the symmetric scheme above. With
dissipation (σ > 0, r\_b > 0, a muted rear segment) the energy only falls (tested); the conductances
G and G\_h are not part of it (their absorbed energy is reported by `conductanceDissipatedEnergy`).

`ModalSegment::energy(wNext, dw, ...)` and `BridgePoint::modeEnergy()` evaluate the two groups in
float64 after a step. Measured values are in the report below.

## Measured results (local, GCC 13.3 and Clang 18.1.3, x86-64 Linux)

- **A4-T1**: two lossless strings (262 Hz L = 0.65 m, 330 Hz L = 0.75 m, plain steel d = 1 mm, f\_max
  16 kHz, 89 modes in all), lossless mass–spring bridge (m = 0.4 kg, f\_b = 300 Hz), double states,
  10⁶ samples at 48 kHz, energy every 100 samples. θ\_b = 0: max |E − E₀|/E₀ = 4.3 × 10⁻¹⁴, slope
  −2.2 × 10⁻¹⁴ per 10⁶ samples. θ\_b = 0.1 rad: 2.3 × 10⁻¹⁴, slope −2.0 × 10⁻¹⁴. Requirement:
  below 10⁻⁶.
- **A4-T2** (e^(+iωt), Y = 1/(iωm), Im Y = −1/(ωm), so Δω\_n = 2 f₀ Z₀/(ω\_n m) > 0): plain steel
  d = 0.5 mm, L = 1 m, f₁ = 262 Hz (B n² < 10⁻³ for n ≤ 10), m = 0.1 kg (Z₀/(ω₁m) = 0.0049),
  lossless, 20 s at 48 kHz, double states, partials 1 to 10 by `pme_analysis` partial tracking. The
  estimator's rigid-bridge error is below 5 × 10⁻¹³ Hz (the shifts are 0.04 to 0.41 Hz). Measured
  over predicted: 0.9986, 1.0001, 1.0009, 1.0018, 1.0028, 1.0041, 1.0056, 1.0073, 1.0092, 1.0114.
  The exact ideal-string end-mass equation cot(kL) = m k/ρA agrees with the formula to 0.1% for
  n ≤ 10 (4.0852e-1 Hz against 4.0916e-1 Hz at n = 1). The growth of the ratio with n is the
  discrete mass response (sin(ωΔt/2)/(ωΔt/2))² (about 1% at n = 10, 2.6 kHz).
  **Stiffness matters:** with d = 1 mm, L = 0.65 m, m = 0.3 kg (B = 3.2 × 10⁻⁴, B n² = 0.032 at
  n = 10) the ratio is 0.9991, 1.0014, 1.0037, 1.0067, 1.0105, 1.0152, 1.0207, 1.0271, 1.0345,
  1.0427 for n = 1 to 10, so the 2% limit fails from n = 7: the small-admittance formula uses the
  ideal-string Z₀ and the bending stiffness changes the string's end impedance by O(B n²). The test
  therefore uses the thin string above; no tolerance was loosened.
- **Benchmark** (`pme_bridge_bench`, release, GCC, this machine): 3 segments (59, 59 and the rear
  segment's modes) × 2 polarisations and a 16-mode modal set, 64 samples per call, median
  29.9 µs per call (about 0.47 µs per sample).

## Stability observations (explicit coupling)

The SPEC order is explicit and is left unchanged; no damping was added. Because the scheme is the
symmetric system above, it is stable exactly while the generalised eigenvalues of (K, M) stay below
4/Δt², and the energy of a stable run is conserved to rounding. A very light bridge mass against
several strings is outside that region. Measured (lossless strings 262 Hz × (1 + 0.001 s), L = 0.65 m,
d = 1 mm, f\_max 20 kHz, lossless mass–spring bridge, f\_b = 300 Hz, 48 kHz, double states, 4 × 10⁵
samples, max |E − E₀|/E₀ sampled every 100 samples; Z₀ = 2.1 kg/s, Z₀Δt = 4.4 × 10⁻⁵ kg):

| m (kg) | 1 string | 3 strings | 8 strings |
| --- | --- | --- | --- |
| 10⁻¹, 10⁻², 10⁻³ | 2 to 5 × 10⁻¹⁴ | 3 to 5 × 10⁻¹⁴ | 2 to 4 × 10⁻¹⁴ |
| 10⁻⁴ | 3 × 10⁻¹⁴ | **unstable** (overflow) | **unstable** |
| 10⁻⁵ | **unstable** | **unstable** | **unstable** |

The limit is near m ≈ N\_strings · Z₀ Δt (about 4 × 10⁻⁵ kg per string here), far below any real
bridge mass (tenths of a kilogram), but a modal-set bridge with a very small Φ\_k⁻² mode and many
unisons could approach it. The planner may want a spec issue (stability condition of the explicit
order; whether `configure` should refuse such a configuration). The scratch program that produced
the table is not part of the repository.
