# Dampers and pedals (A6)

SPEC sections implemented: SPEC-DMP (all of "Dampers and pedals" except the optional "Landing
noise" and the harpsichord jack, which are placement, not model: "Damper model" diagonal and
rank-one, "Lift fraction", "Timing", "Sostenuto" with the thresholds of
[SI-0016](../spec-issues/SI-0016-sostenuto-thresholds.md), "Undamped notes"), with SPEC-OSC
"Time-varying parameters" and "Bandwidth" (diagonal refresh), SPEC-NL "Discrete scheme" and
"Explicit low-rank solve" (rank-one), SPEC-STR "Forces and observations" (raised-cosine window)
and SPEC-EXEC per-sample step 10. interfaces-v2 declares only `DamperParams` and `DamperMode`
(`engine/include/pme/dampers.hpp`); everything here is internal to the engine under
`engine/src/dampers/include/pme/dampers/` (namespace `pme::dmp`), reachable through the
`pme_dampers` target (which links `pme_string`, `pme_nonlinear`, `pme_osc`). **C1** composes the
dampers into the note-group loop and **C4** turns events into the controller inputs. The Soft
pedal is A5's.

| File | Content | Requirement |
| --- | --- | --- |
| `lift.hpp`, `lift.cpp` | `pedalLift`, `liftFraction`, `engagementTarget`, `landingTau`, `validateParams`, `validateHalfPedal`, `EngagementFollower` | A6-R3, A6-R4 |
| `controller.hpp`, `controller.cpp` | `DamperController` (per-key state for keys 0 to 127, sustain, sostenuto latch) | A6-R3 to A6-R5 |
| `diagonal.hpp`, `diagonal.cpp` | `padShape`, `DiagonalDamper`, `OverdampingReport` | A6-R1 |
| `rank_one.hpp`, `rank_one.cpp` | `RankOneDamper<S>` (explicit instantiation for `float` and `double`) | A6-R2 |

## Pad shape

The pad acts on the vertical polarisation through phi-bar\_d,n = phi\_n(x\_d) W(k\_n), x\_d =
`xRatio` L, with the raised-cosine window of width `width`. `padShape(modes, params)` forms it in
float64 from `L`, `k_n` and `x_d` (`sqrt(2/L) sin(n pi xRatio)` times A2's `windowFactor`); it is
not read back from the float32 `windowedShapes` output (a test shows the two agree to 10^-6).
Unit m^(-1/2).

## Diagonal damper (A6-R1)

Per mode (SPEC-DMP "Diagonal"), with the string's sigma\_n, omega\_n and rho A from `StringModes`:

    sigma_n,d    = sigma_n + e R_d phi-bar_d,n^2 / (2 rho A)
    omega_n,d^2  = omega_n^2 + e K_d phi-bar_d,n^2 / rho A

`DiagonalDamper::coefficients(n, e, out)` is `compileOscillatorF64(omega_n,d, sigma_n,d, fs)`.
At e = 0 the float32 coefficients equal the string's own `oscV` bit for bit (tested). Only the
vertical polarisation is modelled: `oscH` is never read or written, and the horizontal modes keep
their own coefficients.

**Per-sub-block refresh.** The audio thread does, once per sub-block of S = 16 or 32 samples while
e moves:

1. `plan(eEnd, S, ramps)`: `eEnd` is e at the end of the sub-block. For every mode it compiles the
   target (`compileOscillatorF64`, no allocation) and calls A1's `planRamp`, which uses
   `exceedsRampThreshold` (|d omega| > 10^-3 omega or |d sigma| > 10^-3 sigma) to choose between an
   immediate switch and a linear ramp over the sub-block. Returns false, doing nothing, if `eEnd`
   equals the value in force.
2. For each sample s: `osc::applyRampsAt(bank, firstMode, ramps, s)`, then the bank step. The state
   (q, u) is never touched (SPEC-OSC "Time-varying parameters").
3. `commit(bank, firstMode)`: the target becomes the value in force and `ModeBank::setMode` writes
   it, including omegaHat^2 and sigmaHat, which the ramp does not touch.

A mode whose target fails to compile (unreachable after a successful `configure`) keeps its
coefficients and increments `refreshFailures()`. Cost per mode: one `compileOscillatorF64` (one
`tanh`, `sinh`, `sin`, `cosh`) plus `planRamp`; see Benchmarks.

**Overdamping check.** The mode count is fixed at compile time, so a damper must not drop modes.
`DiagonalDamper::analyse(modes, params)` reports the largest sigma\_n,d / omega\_n,d and the first
mode that `compileOscillatorF64` rejects, over e = 0, 1/16, ..., 1. `configure` returns `Rejected`
(object unchanged, nothing clamped or skipped) if any mode is rejected. Results are in "Overdamping
results".

## Rank-one damper (A6-R2)

SPEC-DMP: spring K\_d on the centred average mu\_t. y and dashpot R\_d on delta\_t. y, with y =
c^T z, c = phi-bar\_d on the vertical modal coordinates (0 elsewhere), both scaled by e. In
SPEC-NL's low-rank solve this is one pair h = gamma (.) e c / m, w = c (K\_d / 2 + R\_d / (2 dt)).

A3's API is used unchanged. The pad force on coordinate i is

    -e c_i [ (K_d/2 + R_d/(2 dt)) y^(n+1) + (K_d/2 - R_d/(2 dt)) y^(n-1) ].

A3's dashpot of coefficient R along c contributes -R c_i delta\_t. y = -R c\_i (y^(n+1) -
y^(n-1)) / (2 dt) (its pair is h = R gamma c / m, w = c / (2 dt), and it forms h w^T z^(n-1) itself).
Choose R = e (R\_d + K\_d dt). Its y^(n+1) coefficient is then -e c\_i (K\_d/2 + R\_d/(2 dt)), as in
the pad force. The y^(n-1) coefficient of the true force is -e (K\_d/2 - R\_d/(2 dt)) c\_i and that of
the dashpot is +e (K\_d/2 + R\_d/(2 dt)) c\_i, so the difference is **-e K\_d c\_i y^(n-1)**, an explicit
force known at step n. Equivalently e K\_d mu\_t. y = e K\_d dt delta\_t. y + e K\_d y^(n-1). So:

- `RankOneDamper::dashpot(e)` returns `nl::Dashpot{c, R = e (R_d + K_d dt), alongGradient = false}`;
- `RankOneDamper::addSpringForce(e, seg, force)` adds -e K\_d c (c^T z^(n-1)) to the explicit force
  (N per coordinate; A3 divides by the mass), with z^(n-1) = q^n - u^n from the segment's present
  state;
- then `nl::savStep`.

The test `A6-R2 Dashpot plus explicit force equals SPEC's low-rank pair` runs both forms for 1000
steps with a varying e in float64: the stored `savStep` state and a direct implementation of SPEC's
pair through `lowRankSolve` (with the y^(n-1) part of the force moved into the known term a) agree
to 2.5 x 10^-14 of max |q| (limit 10^-12).

**Energy.** For constant e, E\_d^(n+1/2) = (e K\_d/4)[(c^T z^(n+1))^2 + (c^T z^n)^2] =
`springEnergy(e, seg)`; the total is `nl::savEnergy(seg)` + `springEnergy(e, seg)` (+ 1/2 psi^2 - eps0
when a potential is active). The damper uses one of the segment's kMaxDashpots = 3 dashpot
directions.

## Lift fraction and timing (A6-R3, A6-R4)

`pedalLift`, `liftFraction`, `engagementTarget` and `landingTau` are the formulas of SPEC-DMP
"Lift fraction" and "Timing": l = max(l\_key, l\_ped), l\_ped = clamp((P - P0)/(P1 - P0), 0, 1),
e\_target = (1 - l)^eta\_d, tau\_down = tau\_0 / (1 + k\_r nu\_rel). P0, P1 come from
`Defaults::halfPedalP0/P1` (registry `damper.halfPedalStart/End`); `validateHalfPedal` and
`DamperController::configure` return `InvalidArgument` unless 0 <= P0 < P1 <= 1.

`EngagementFollower` is the one-pole: e^(n+1) = e\_target + (e^n - e\_target) exp(-dt / tau), exact
for a constant target, tau = tau\_up while lifting (e\_target < e) and tau\_down while landing
(e\_target > e), tau\_down from the release velocity of the last NoteOff (clamped to [0, 1]; 0
before any NoteOff). When |e - e\_target| < 10^-9 e snaps onto the target and `moving()` turns
false; this is far below the 0.1% ramp threshold.

**Per-sample versus per-sub-block.** e itself advances per sample (`DamperController::advance()` /
`EngagementFollower::step()`, SPEC-EXEC step 10), and only moving keys cost anything (a 128-bit mask
of moving keys). The coefficients of the diagonal damper are refreshed per sub-block from e at the
sub-block end: `controller.projected(key, S)` gives e after S more samples in closed form at the
present target, so C1 can plan the ramp at the start of the sub-block. If an event changes the
target inside the sub-block, e itself is exact from that sample on and the coefficients follow in
the next sub-block's plan (lag at most one sub-block); C1 may also split the sub-block at the event.
The rank-one damper needs no refresh: C1 sets `Dashpot::R` and the spring force from the present e
every sample.

## Controller, sostenuto and undamped keys (A6-R3 to A6-R5)

`DamperController` holds preallocated per-key state for keys 0 to 127 (`std::array`, nothing is
allocated after `configure` and `setKey`).

- `setKey(key, std::optional<DamperParams>)`: no value, or `hasDamper == false`, means no damper:
  l = 1 and e = 0 whatever the inputs, never latched (SPEC-DMP "Undamped notes"). A damped key
  starts at rest, e = 1.
- `noteOn(key)`: l\_key = 1. `noteOff(key, nuRel)`: l\_key = 0 and nu\_rel stored for tau\_down.
  `keyDepth(key, d)`: l\_key = clamp(d, 0, 1) (half-key control). Keys outside 0 to 127 and
  non-finite values are ignored.
- `setSustain(P)`: l\_ped for every key. `setSostenuto(D)` (SI-0016): the pedal is down while D >=
  0.5, released below 0.5, no hysteresis. At the call where it goes down every damped key with
  l\_key >= 0.5 is latched (l\_key = 1, whatever its key events, until the pedal is released);
  keys pressed later behave normally; keys without a damper are not affected. When it goes up all
  latches are cleared and each key returns to its own l\_key.
- `lift(key)` returns (l\_key, l\_ped, l, latched) after the last input; `engagement(key)`,
  `target(key)`, `projected(key, n)`, `tauDown(key)`, `anyMoving()`.

One controller slot per key holds the parameters of that key's representative damper. A group
whose segments carry different timing parameters can run one `EngagementFollower` per segment
from the controller's `lift(key).l`.

## Overdamping results

Pad of the example instruments (x\_d/L = 0.0537 for the test strings and 0.05 in the instrument,
b\_d = 0.02 m, K\_d = 5000 N/m, R\_d = 5 kg/s), fs = 48 kHz, f\_max = 16 kHz, e swept 0 to 1. No mode
is rejected anywhere, so no mode is dropped and nothing is clamped (a damper with R\_d = 10^6 is
rejected by `configure`, as the test `A6-R1 overdamping check on the test strings at full
engagement` shows).

| String | Modes | Largest sigma\_n,d / omega\_n,d at e = 1 |
| --- | --- | --- |
| treble, L 0.1 m, f1 2000 Hz | 6 | 0.048 (mode 4) |
| mid, L 0.65 m, f1 262 Hz | 46 | 0.090 (mode 7) |
| wound bass, L 1.9 m, f1 27.5 Hz | 220 | 0.014 (mode 7) |
| every damper of `generic-grand-88` (176 segments) | 9 to 220 | **0.078 (key 73, mode 7)** |

The worst ratio sits at e = 1; the check also samples e = 1/16 ... 15/16.

## Benchmarks

`pme_dampers_bench` (label `perf`), linux-release, GCC, development machine; no baseline file until
V2 and decision D-4:

| Benchmark | Median | Per unit |
| --- | --- | --- |
| `damper_refresh_80_modes_per_subblock`: `plan` + 16 `applyRampsAt` + `commit`, 80 modes, e moving | 6.8 us | 85 ns per mode; at S = 16 and 48 kHz about 2% of a core per moving damper |
| `damper_rank_one_step_80_modes_x64`: `addSpringForce` + `savStep` (r = 1), 80 modes, 64 samples | 41.5 us | 650 ns per sample (8 ns per mode-sample) |
| `damper_controller_advance_moving_keys`: `advance()` with 88 moving keys | 246 ns | 2.8 ns per key-sample |

Line coverage of `engine/src/dampers` (gcovr, linux-coverage, tests labelled A6): 98.4%.

## Real-time rules

Every function used by `Engine::process` is `noexcept` and allocation-free: the controller events,
`advance`, `projected`, `lift`, `DiagonalDamper::plan`/`commit`/`coefficients`/`sigmaAt`/`omegaAt`,
`RankOneDamper::dashpot`/`addSpringForce`/`springEnergy`/`pad`, and `EngagementFollower`. Only
`configure`, `setKey` (no allocation either), `padShape`, `DiagonalDamper::configure`/`analyse` and
`RankOneDamper::configure` run on non-audio threads. The tests `A6-R5 controller events and advance
allocate nothing`, `A6-R1 diagonal refresh and ramps allocate nothing` and `A6-R2 rank-one step
allocates nothing` (label `rt`) run them under `ScopedRealtimeThread`.

## Tests

| Test | File |
| --- | --- |
| A6-T1 monotone energy, lossless string (46 modes), rank-one damper, e = 1 and 0.3, 2 s: worst E^(n+1/2)/E^(n-1/2) - 1 = -4.2 x 10^-14 (limit +10^-13), decay 65 000 (e = 1) and 4 900 (e = 0.3) | `engine/tests/dampers/test_rank_one.cpp` |
| A6-T2 table of 20 key, sustain and sostenuto sequences | `test_controller.cpp` |
| A6-T3 damping against pedal depth, 1.00 down to 0.00 in steps of 0.01 | `test_diagonal.cpp` |
| A6-T4 engagement steps against the undamped signal's; window from the engagement event (20 ms) to 0.2 s; largest damped step / undamped D: diagonal 0.82 (landing, nu_rel 0), 0.81 (nu_rel 1), 0.75 (lifting); rank-one 0.87, 0.87, 0.36; no margin needed | `test_engagement.cpp` |
| A6-R1 formulas, vertical only, ramp rule, overdamping | `test_diagonal.cpp`, `test_instrument.cpp` |
| A6-R2 Dashpot form equals SPEC's pair | `test_rank_one.cpp` |
| A6-R3, A6-R4, A6-R5 | `test_controller.cpp` |
| RT-safety | `test_rt.cpp` (label `rt`) |
| Benchmark `pme_dampers_bench` (label `perf`) | `dampers_bench.cpp` |

ORC-13 (diagonal damper against the reference solver) is written by the A6 test-author under
`engine/tests/oracles/dampers`.
