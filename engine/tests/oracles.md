# Verification oracle registry

Registry of the SPEC-ORC rows (docs/plan/engine-specification.md, "Verification oracles"), one row
per oracle, in SPEC order. Created by P0.3-R2. SPEC governs the setup, expected value and tolerance
columns, which are copied verbatim; if they drift, SPEC wins and this file is corrected.

## How this file is maintained

- **ID.** `ORC-01` to `ORC-19`, the SPEC row order. An oracle test is a doctest `TEST_CASE` (or, for
  Python-side oracles, a pytest function) whose name begins with its ID, for example
  `TEST_CASE("ORC-01 oscillator accuracy at 27.5 Hz")`; it carries the CTest label `oracle`
  (see docs/dev/testing.md).
- **Status** is one of:
  - `not implemented`: no test exists yet.
  - `implemented`: the test exists and is registered, but does not pass yet or is not required to.
  - `passing`: the test exists, is registered with the `oracle` label and passes in CI on all three
    systems.
- **Owning phase** is the phase whose card implements the test (phases.yaml and the Gates section of
  docs/plan/phasing-plan.md). **Gate** is the gate that requires it to pass.
- **Test location** is the source file and test-case name once the test exists; `-` before that.
- The phase that implements an oracle updates its row (status and location) in the same pull request
  as the test. No agent edits the setup, expected or tolerance columns without a SPEC change.

| ID | Oracle | SPEC-ORC setup | Expected value | Tolerance | Owning phase | Gate | Status | Test location |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ORC-01 | Oscillator accuracy | One mode at 27.5 Hz, 261.6 Hz, 4186 Hz; T60 = 20 s; 48 kHz; float32 difference form | Frequency ω\_d/2π and decay e^(−σt) | 0.01 cent; T60 within 0.5% (run: under 0.001 cent) | A1 | G1 | implemented | `engine/tests/oracles/osc/test_orc01_accuracy.cpp` (`ORC-01 oscillator accuracy at 27.5 Hz`, `... at 261.6 Hz`, `... at 4186 Hz`) |
| ORC-02 | Oscillator energy | Lossless mode, 10 s; float32 at 48 kHz (N = 480 000) for 27.5 Hz, 4186 Hz and ω\_dΔt = 0.9π, none within 1% of fs/6, fs/4 or fs/3 | H^(n+½) constant (float32: bounded round-off drift) | 10⁻¹² relative in float64; in float32 24 u√N / (1 − ω̂²Δt²/4), u = 2⁻²⁴: 9.9 × 10⁻⁴ at 27.5 Hz, 1.07 × 10⁻³ at 4186 Hz, 4.05 × 10⁻² at 0.9π ([SI-0009](../../docs/spec-issues/SI-0009-orc02-float32-energy-tolerance.md)) | A1 | G1 | implemented | `engine/tests/oracles/osc/test_orc02_energy.cpp` (`ORC-02 energy is constant in float64`, `ORC-02 energy is constant in float32 at <f>`, `ORC-02 energy balance with loss and forcing`) |
| ORC-03 | Stiff-string frequencies | Compiled table vs n f₀ √(1 + B n²) | Exact formula | 10⁻⁹ relative | A2 | G1 | implemented | `engine/tests/oracles/string/test_orc03_orc04_frequencies.cpp` (`ORC-03 stiff-string frequencies pinned`, `ORC-03 clamped stiff-string frequencies with Fletcher correction`) |
| ORC-04 | Tension for pitch | Closed-form T, then first partial | f₁ target | 10⁻⁹ relative | A2 | G1 | implemented | `engine/tests/oracles/string/test_orc03_orc04_frequencies.cpp` (`ORC-04 tension for pitch at keys 21 45 69 108 and the string's own f1`) |
| ORC-05 | Window shape | Raised-cosine W(k) vs quadrature, n up to 60 | Same value | 10⁻⁹ relative | A2 | G1 | implemented | `engine/tests/oracles/string/test_orc05_window.cpp` (`ORC-05 window shape raised cosine vs quadrature in float64`, `... stored in float32 by windowedShapes`, `... window factor limits and removable singularities`, `... point shapes phiBar at the registered point`) |
| ORC-06 | Bridge damping | Flexible string on a pure conductance G, every kept mode | σ = −f₀ ln\[(1 − Z₀G)/(1 + Z₀G)\] | Modes with ω\_nΔt ≤ 0.1: 0.1% against the formula (run: 10⁻⁴). Every kept mode: measured decay rate equals that of the exact eigenvalues of the scheme's one-step coupled map within 10⁻⁴ relative; the deviation from the formula is reported per mode, not bounded ([SI-0021](../../docs/spec-issues/SI-0021-orc06-bridge-damping-high-modes.md)) | A4 | G1 | passing | `engine/tests/oracles/bridge/test_orc06_orc07.cpp` (`ORC-06 bridge damping every kept mode against the exact one-step map with Z0G = 0.01`, `... = 0.05`, `ORC-06 bridge damping modes with omega dt up to 0.1 with Z0G = 0.01`, `... = 0.05`); runs against the A4 `BridgePoint` through `bridge_rig_a4.hpp` (AUTO backend), SPEC-derived scratch rig with `-DPME_A4_ORACLE_BACKEND=SCRATCH` |
| ORC-07 | Unison coupling | Two identical strings, one conductance | In-phase 4 f₀ Z₀ G; anti-phase intrinsic | 0.1% | A4 | G1 | passing | `engine/tests/oracles/bridge/test_orc06_orc07.cpp` (`ORC-07 unison coupling in-phase decays at 4 f0 Z0 G plus the intrinsic rate`, `ORC-07 unison coupling anti-phase decays at the intrinsic rate only`); backend as ORC-06 |
| ORC-08 | Impact time | Mass 8 g at 3 m/s on rigid felt contact, p = 2.5 | t\_c from the hammer oracle; rebound −v | max(1%, 1 sample); rebound 0.1% (run: 0.4%, exact rebound) | A3 | G1 | implemented | `engine/tests/oracles/nonlinear/test_orc08_impact_time.cpp` (rigid wall (A3); hammer potential (A5) pending) |
| ORC-09 | Hammer–string energy | Lossless string and felt, float64 | Total energy including ½ψ² constant | 10⁻¹⁰ relative (run: 7 × 10⁻¹⁴) | A5 | G1 | not implemented | - |
| ORC-10 | Triangle pluck | q\_n(0) formula vs quadrature | Same value | 10⁻⁹ relative | P1 | G6 | not implemented | - |
| ORC-11 | Longitudinal forcing | Pair sum vs quadrature of −(EA − T)/(2ρA) ∫ y\_x² χ\_m' dx | Same value | 10⁻⁹ relative | A7 | G1 | not implemented | - |
| ORC-12 | Tension modulation | Σ k\_n² q\_n² vs ∫ y\_x² dx | Same value | 10⁻¹² relative | A7 | G1 | not implemented | - |
| ORC-13 | Damper (diagonal) | e = 1, single mode | σ\_n + e R\_d φ̄² / (2ρA) | σ passed to the oscillator compiler: 10⁻⁹ relative (float64). Measured decay rate: 0.1% in float64; in float32 0.1% + 2⁻²⁴/(2σΔt), the β quantisation bound with a factor-2 margin ([SI-0017](../../docs/spec-issues/SI-0017-orc13-float32-decay-resolution.md)) | A6 | G1 | passing | engine/tests/oracles/dampers/test_orc13_damper_diagonal.cpp |
| ORC-14 | Fretted pitch | Finger stop at fret k on a low-stiffness string | Open pitch × 2^(k/12) | 2 cents | P3 | G6 | not implemented | - |
| ORC-15 | Collision passivity | String released onto a flat barrier | Energy constant without contact damping; non-increasing with it | 10⁻⁹ relative per step in float64 | P2 | G6 | not implemented | - |
| ORC-16 | Sample-rate independence | Same instrument at 44.1, 48 and 96 kHz | Identical partial frequencies; matching decays | 0.01 cent; 0.5% | C1 | G2 | not implemented | - |
| ORC-17 | Sympathetic approximation | Sustained chord with pedal, then release; engine vs reference mode | Per-partial levels; no energy growth | 1 dB over the first 2 s | C3 and C7 | G2 | not implemented | - |
| ORC-18 | Determinism | 1, 2 and 8 threads | Bit-identical output | Exact | C5 | G2 | not implemented | - |
| ORC-19 | Una corda aftersound | Trichord, s = 1 | Passive string's bridge force starts at zero and grows over time, as in Miranda Valiente et al. | Qualitative, plus regression against the first accepted run | A5 | G1 | not implemented | - |
