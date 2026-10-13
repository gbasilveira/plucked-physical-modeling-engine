# Hammer and felt (A5)

SPEC sections implemented: SPEC-HAM (all of "Hammer and action", including "Flexible shank"),
SPEC-NL "Coordinates", "SAV formulation", "Explicit low-rank solve", "Contracts"; SPEC-EXEC
"Per-sample loop of one note group" steps 1, 2, 5, 6 and 7; SPEC-BRG "Discrete coupling order";
SPEC-STR "Forces and observations" (raised-cosine windows); SPEC-CONV (float64 reductions, float32
run-time states, approach sign s\_E).

Everything is internal: `engine/src/exciters/include/pme/exciters/hammer.hpp`, namespace
`pme::exciters`, target `pme_exciters` (links `pme_bridge`, `pme_nonlinear`, `pme_string`,
`pme_osc`). The parameter structs `HammerParams`, `UnaCordaParams`, `FeltLaw` are the frozen
interfaces-v2 ones. `ShankParams {mass, frequency, sigma}` mirrors ICR-0002 (proposed); when it is
applied, switch to the public type. P1 adds plucked exciters to the same module.

## API

| Item | Meaning |
| --- | --- |
| `hammerVelocity(nu, HammerParams, vH)` | v\_H = v\_min + (v\_max − v\_min) ν^γ\_v. `InvalidArgument` for ν ≤ 0 or not finite; ν > 1 is clamped to 1 |
| `smoothstep(s, s0, s1)` | S = 3t² − 2t³, t = clamp((s − s0)/(s1 − s0), 0, 1) |
| `strikeMap(hammer, una, pedal, nStrings, shift, StrikeMap&)` | per-string w\_s, K\_s(s), p\_s(s); shift clamped to [0, 1] |
| `launchVelocity(nu, shift, hammer, una, pedal, vH)` | velocity map, times (1 − s ρ\_up) for `SoftPedal::Upright` |
| `HammerPotential<S>` | V\_H, gradient, δ\_s helpers; `configure(slices, hammerIndex, lever, approachSign)`, `setStrikeMap`, `setActive` |
| `RigidFeltPotential<S>(K, p, index, target)` | test-only rigid-target contact (A5-T1, ORC-08) |
| `HammerFelt<S>` | Hunt–Crossley dashpots or Stulov force: `configure`, `reset`, `update(potential, z, force)`, `dashpots()`, `hereditaryWork(seg)` |
| `hammerDashpotCount(hammer, nStrings)`, `hammerDashpotBudgetLeft(...)` | dashpot slots used and left of `nl::kMaxDashpots` |
| `HammerDiagnostics` | `strikes`, `contacts`, `lastContactDuration`, `lastPeakForce`, `totalContactTime` |
| `StruckNoteDesc` | `strings` (1–3 `const StringModes*`), `end`, `hammer`, `unaCorda`, `pedal`, `approachSign`, `shank` (optional), `sav` |
| `StruckNote<S>` | `configure(desc, fs, BridgePoint<S>* or nullptr)`, `addPassiveSegment(modes, end)`, `launch(nu, shift)`, `setShift(shift)`, `step()`, `reset()`; accessors `segment()`, `potential()`, `felt()`, `inFlight()`, `headPosition()`, `headVelocity()`, `modeOffset(s)`, `modeCount(s)`, `qH(s)`, `diagnostics()`, `clearDiagnostics()`, `energy()`, `releasedEnergy()`, `resetEnergyRemoved()` |

`S` is `float` in the engine and `double` in energy tests; both are explicitly instantiated in
`hammer.cpp`. Configuration allocates; `launch`, `setShift`, `step` and the accessors are `noexcept`,
allocation free (tested under `ScopedRealtimeThread`).

## Coordinate layout

One SAV segment per struck note (SPEC-NL "Coordinates"): the vertical modes of string 0, then 1, then
2 (each in mode order, mass ρA), then the hammer coordinate x\_H (mass m\_H, free: β = 1, κ = 0,
γ = Δt²), then, with a shank, the shank's internal mode. All struck strings share one ψ. The
horizontal polarisations are linear, advanced in `StruckNote` with the bridge term only (as
`bridge::ModalSegment`). A string with weight 0 (the string that leaves the hammer at s = 1) stays in
the segment and on the bridge point.

## Potential, gradient and dashpots (A5-R2, A5-R3)

δ\_s = x\_H − s\_E Σ\_n φ̄\_s,n q\_s,n; V\_H = Σ\_s w\_s K\_s/(p\_s+1) [δ\_s]₊^(p\_s+1); ∂V/∂x\_H =
Σ w\_s K\_s [δ\_s]₊^p\_s; ∂V/∂q\_s,n = −s\_E φ̄\_s,n w\_s K\_s [δ\_s]₊^p\_s. φ̄ is `windowedShapes` of a
raised-cosine window of width b\_H (0 = point contact, φ̄\_n = φ\_n(x\_c)) at x\_c = β\_c L on the segment's own x axis (float32 storage);
arithmetic is float64. `value` is exactly 0 when every δ\_s ≤ 0 or the potential is inactive, so the
A3 skip rule applies. Strings with weight 0 are ignored.

Hunt–Crossley: one `nl::Dashpot` per struck string along c\_s (+1 on x\_H, −s\_E φ̄ on that string's
modes), R\_s = w\_s K\_s μ\_HC [δ\_s]₊^p\_s. The dashpots are flagged `alongGradient` when exactly one
string contributes to the gradient (always for a single string; also a bichord at s = 1), giving
r = 1; a trichord in contact gives r = 1 + 3 = 4, A3's maximum.

**Dashpot budget with dampers.** SPEC-NL allows at most 3 dashpots per segment. A Hunt–Crossley
hammer occupies one slot per struck string, so a trichord in contact uses the whole budget
(`hammerDashpotCount` = 3, `hammerDashpotBudgetLeft` = 0) and a rank-one damper (A6, one slot) cannot be
added on the same segment: **C1 must use the damper's diagonal mode for that segment** while the
hammer is in contact, as SPEC-NL requires. In normal play the damper is lifted (R = 0, no column)
while the key is down, so the case arises only at very fast re-strikes. A bichord leaves one slot, a
single string two; Stulov uses none.

## Stulov hereditary felt (A5-R3)

h\_s^n = a h\_s^(n−1) + (1 − a) [δ\_s^n]₊^p\_s with a = e^(−Δt/τ). SPEC-HAM form: the felt force of string s
is f\_s = w\_s K\_s max(0, δ\_s^p − ε h\_s) (the hereditary term reduces the force; felt cannot pull).
The SAV elastic force on the generalised coordinates stays in V\_H (−w\_s K\_s δ\_s^p c\_s), and the
explicit hereditary term goes through `savStep`'s `explicitForce` (N per coordinate):
F\_hered,s = +w\_s K\_s min(ε h\_s, δ\_s^p) c\_s while δ\_s > 0, else 0, so the two always oppose; on x\_H
the elastic part is −wKδ^p and the hereditary part +wK·min(εh, δ^p), on string mode n they are
+s\_E φ̄\_n wKδ^p and −s\_E φ̄\_n wK·min(εh, δ^p). The variant is not provably passive:
`HammerFelt::hereditaryWork(seg)` returns the work of the hereditary force in the last step (float64,
Σ F\_s c\_s^T (u^(n+1) + u^n)/2), and the test closes E^(n+1) − E^n + dissipation = work to 1 × 10⁻¹⁴
per step. Measured: the hereditary force **removes** energy (work/E₀ < 0 over a strike at ν = 0.8,
ε = 0.3, τ = 0.5 ms, in `test_felt.cpp`). Rigid-wall bounce (8 g, K = 4 × 10⁹, p = 2.5, τ = 0.5 ms,
3 m/s): rebound KE / incoming KE = 1.000 at ε = 0 and 0.719 at ε = 0.3 at 48 kHz (0.717 converged) with the
no-pull clamp; without the clamp, K(δ^p − εh), which pulls during unloading, it would be 0.604 (the
earlier sign error, K(δ^p + εh), gave 1.358). The force is zero outside contact (the felt does not act on a separated
hammer) and carries the strike weight w\_s.

## Release, repetition (A5-R4)

After a step the hammer is released when the largest δ\_s (strings with w\_s > 0) is below −d\_check
and its head velocity is negative: it is frozen (u = 0, position held, shank internal state dropped),
the potential is deactivated and the Stulov state is cleared; the segment then takes the exact skip
path (strings bit-identical to a free run, tested). The energy it carries away is exposed by
`releasedEnergy()`. A double strike needs no code (tested with a 40 Hz string and a 15 g hammer at
ν = 0.05: two contact intervals). `launch` in flight within d\_check keeps x\_H (no jump; both
masses of a shank get the new v\_H and zero deflection, x\_H unchanged) and replaces the velocity;
otherwise the hammer starts at the gap v\_H Δt from the highest struck string's window-averaged
position, moving at v\_H, and touches it one sample later (δ = 0 at launch + 1, δ = v\_H Δt > 0 at
launch + 2: no action latency).

## Una corda (A5-R5)

Grand: w of the string `leavingString` is 1 − S(s), the others 1 (a single string, or −1: all 1);
K\_s(s) = K[1 − s(1 − κ\_uc)] and p\_s(s) = p + s Δp\_uc for every string (also single strings).
Upright: weights, K and p unchanged, v\_H scaled by (1 − s ρ\_up). S is the cubic smoothstep
3t² − 2t³. `StruckNote::setShift` recomputes the map; call it every sample while s ramps.

## Flexible shank (A5-R6)

X = (m\_H x\_H + m\_S x\_S)/M (free, mass M = m\_H + m\_S), Y = x\_H − x\_S (mass μ = m\_H m\_S/M,
Ω\_S = 2π f\_S, σ\_S, `compileOscillatorF64`), x\_H = X + (m\_S/M) Y; the generalised force on X is F
and on Y is (m\_S/M) F, so the contact gradient and every dashpot direction have the two hammer entries
(1, m\_S/M). `HammerParams::mass` is the head mass; a very stiff shank behaves as a rigid hammer of
mass m\_H + m\_S.

## Diagnostics (A5-R7)

Per completed contact interval: duration (first to last sample with any δ\_s > 0, sub-sample linear
interpolation at the zero crossings of max δ\_s), peak force g\_H (ψ^(n+½) + ψ^(n−½))/2 in N, a
contact counter, and a strike counter (launches). The runtime reads and clears them like
`nl::SavCounters` (`clearDiagnostics()`).

## StruckNote and extension by C1 and A9

Per sample, SPEC-EXEC order: bridge force of each struck string (`bridgeForce` with the vertical and
horizontal A4 coefficients) → `BridgePoint::addForce`/`advance` → explicit modal forces −ρA g\_n
δ\_tt w and the Stulov force → `savStep` → horizontal polarisations and passive segments. The bridge
force and the bridge term are separate inputs; A9's implicit scheme (steps 3′ to 5′) replaces the
middle part by the low-rank solve applied to a second right-hand side without changing the potential
or the felt. `energy()` is the SAV segment's conserved energy (vertical modes, hammer, ψ);
`releasedEnergy()` and `resetEnergyRemoved()` give the energy removed by release and by the SPEC-NL
reset at separation, so that conservation can be checked across them.

## Open points for the planner

- SPEC-HAM defines δ\_s from the modal displacement only; with a moving bridge the true string
  displacement at the contact also contains the lifting term w ψ̄ (ψ̄ ≈ β\_c for ψ = x/L). The A5
  test-author measured an energy deviation of 2.4 × 10⁻⁵ at bridge mass 0.3 kg scaling as 1/m. Implemented as
  written; reported as a possible SPEC issue.
- ICR-0002 (`ShankParams` in `HammerParams`), Stulov gating and weight (above).

## Tests and measured results

`engine/tests/exciters/` (doctest, labels `unit`, one `rt`, benchmark `perf`):

| Test | Result |
| --- | --- |
| A5-T1 contact time vs v^((1−p)/(1+p)), p = 2.5 and 3.5, v = 0.5…6 m/s, 48 and 96 kHz | ratio − 1 within 1.1 × 10⁻³ (limit 2 × 10⁻²); rebound within 2.2 × 10⁻⁶ of −v; absolute t\_c at 3 m/s, p = 2.5: 8.7046 × 10⁻⁴ s vs closed form 8.6972 × 10⁻⁴ s (8.6 × 10⁻⁴ at 48 kHz, 2.4 × 10⁻⁴ at 96 kHz) |
| A5-T3 una corda tables | exact to 10⁻¹² |
| A5-T4 repetition | x\_H unchanged at the re-launch sample (in flight and in contact); re-strike energy change equals ½ m (v′² − v²) to 10⁻¹²; per-step balance after it 1.2 × 10⁻¹⁴ |
| A5-R3 energy balance (HC, 1/2/3 strings; Stulov with work) | worst 9 × 10⁻¹⁵ relative per step |
| A5-R6 shank | free response frequency error 1.2 × 10⁻¹⁵; head position vs two-mass solution 9 × 10⁻¹⁵; stiff shank (20 kHz, 2 g on 8 g) vs rigid 10 g: t\_c 6.5 × 10⁻⁶, peak force 3.5 × 10⁻⁵ |
| Benchmark `pme_exciters_bench` (linux-release, GCC, 64 samples per call, 184 coordinates) | trichord in contact with bridge 326 µs; skip path with bridge 35 µs |

ORC-09 and ORC-19 and A5-T2 are the test-author's (`engine/tests/oracles/exciters`).
