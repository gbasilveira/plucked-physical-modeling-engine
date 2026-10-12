# SI-0022 SPEC-BODY leaves air constants, delay reference, radiating sides and the G\_hf band edge unstated

Status: open (planner defaults in effect for B3; items 1–3 and 5–8 are conventions, item 4 is physics and needs owner review) · Raised by: planner, while briefing B3 · Date: 2026-10-12
SPEC section: SPEC-BODY "Radiation weights (offline)", "Modal data (offline output)", "Local bridge model", "Contracts"; SPEC-INST "Piano defaults" (f\_split)

## Failing test
None yet. B3-R1 to B3-R5 and B3-T1 to B3-T3 cannot be implemented reproducibly without the choices below. The B3 briefs (docs/agents/briefs/B3-implementer.md) use the defaults proposed here, so B3 is not blocked.

## Evidence
SPEC-BODY gives the Rayleigh integral for A\_c,k, the decay σ\_k = ½ η Ω\_k + σ\_k,rad and the definition of G\_hf,b, but:

1. ρ₀ and c₀ have no values anywhere in SPEC or PLAN.
2. "The common delay R\_c / c₀" uses the vector R\_c as a scalar. The natural reading is its norm (distance from the body-frame origin). Any reference distance makes the real-time sum exact at Ω\_k (the residual phase stays in A\_c,k), but away from Ω\_k the error grows with the residual delay.
3. τ\_c is "an integer per-channel delay" at run time, but `buildLocalBridge` and `compileBody` are sample-rate independent (SI-0002) and `BodyModes::channelDelay` is in seconds. The fractional part must go somewhere if the sum is to stay exact at Ω\_k.
4. The Rayleigh integral as written radiates into the half-space z > 0 of an infinite baffle. A piano soundboard radiates from both faces, a guitar top mostly from its front. SPEC does not say which side a channel with z < 0 hears, or whether σ\_k,rad counts one face or two.
5. G\_hf,b is a mean over \[f\_split, f\_max\], but `BodyModes` has no f\_max, so `buildLocalBridge` cannot know the plate's f\_max.
6. `BodyModes::residualMobility` ("from which G\_hf,b is derived", provisional) cannot hold G\_hf,b for every (M\_b, f\_split) that `buildLocalBridge` may receive.
7. "Mean real part" does not say mean over what measure.
8. "The M\_b modes with largest Φ\_k(x\_b)² below f\_split" leaves ties and the boundary open.

## Proposed correction
Defaults, used by B3 now:

1. **Air constants.** ρ₀ = 1.204 kg/m³, c₀ = 343.2 m/s (dry air at 20 °C and 101.325 kPa). Compiler arguments with these defaults. Not instrument-file fields; a later ICR may add them.
2. **Delay reference.** R\_c = ‖R\_c‖, the distance of the channel from the body-frame origin, as SPEC writes it. The compiler reports, per channel, the largest residual delay max\_x |(|R\_c − x| − ‖R\_c‖)| / c₀ over the plate, so a badly placed origin is visible.
3. **Integer delay.** `channelDelay` holds τ\_c = ‖R\_c‖ / c₀ in seconds. At sample rate fs, n\_c = ⌊τ\_c fs⌋ samples, and the remainder δ\_c = τ\_c − n\_c / fs is folded into the gains: A′\_c,k = A\_c,k e^(−iΩ\_k δ\_c), α′ = Re A′, β′ = −Im A′. Then the real-time sum with the integer delay is exact at every Ω\_k. compileInstrument (C1) applies it; B3 provides the function.
4. **Radiating sides (physics; owner review).** An option with two values. `front` (default; SPEC as written): one baffled face, channels must have z > 0, σ\_k,rad is the power into z > 0. `both`: the plate radiates from both faces of the baffle; a channel with z < 0 hears the back face, whose pressure has the opposite sign (A\_c,k computed with −Φ\_k and |z|); σ\_k,rad is twice the one-face value. A channel with z = 0 is rejected in both modes. Per D-13 this is an instrument option. It is a compiler argument until an ICR adds `BodyParams::radiation` (or similar); candidate for the ICR after interfaces-v3.
5. **Band edge.** In `buildLocalBridge`, f\_max = max\_k Ω\_k / 2π of the body's modes. If f\_split ≥ f\_max, G\_hf,b = 0.
6. **`residualMobility`.** The blob array holds G\_hf,b for the instrument's own `defaults.localBridgeModes` and `defaults.localBridgeFSplit`, as written by B3. `buildLocalBridge` always recomputes G\_hf,b from the modes for its own arguments; with the instrument's defaults it reproduces the stored value within 10⁻⁹ relative (a consistency check, not an input).
7. **Mean.** G\_hf,b = (1 / (ω₂ − ω₁)) ∫ from ω₁ to ω₂ of Re Y\_R,b(ω) dω, ω₁ = 2π f\_split, ω₂ = 2π f\_max: the arithmetic mean over linear frequency, integrated exactly (per-mode closed form), with Y\_R,b(ω) = Σ over k not kept of Φ\_k(x\_b)² iω / (Ω\_k² − ω² + 2iσ\_kω) (e^(+iωt), SI-0012).
8. **Selection.** Kept modes satisfy Ω\_k / 2π < f\_split (strict). Rank by Φ\_k(x\_b)², descending; equal values go to the lower k. `modeIndex` is stored in ascending k.

Proposed SPEC text (SPEC-BODY, after the radiation formula): "ρ₀ = 1.204 kg/m³ and c₀ = 343.2 m/s unless the compiler is told otherwise. R\_c in the delay is ‖R\_c‖. `channelDelay` stores τ\_c in seconds; at fs the engine delays by ⌊τ\_c fs⌋ samples and folds the remainder into the gains as A\_c,k e^(−iΩ\_k δ\_c), so the sum stays exact at Ω\_k. A body radiates from its front face (z > 0) unless the instrument selects both faces; the back face radiates with the opposite sign and doubles σ\_k,rad (SI-0022)." And after the G\_hf definition: "Here f\_max is the highest mode of the body's table, the mean is over linear frequency, and kept modes lie strictly below f\_split, ties going to the lower index (SI-0022)."

## Resolution
Pending. The orchestrator resolves items 1–3 and 5–8 (conventions). Item 4 changes physics (the radiated sound and damping of a two-sided board) and goes to the owner. Affected phases: B3 (uses the defaults now), C1 (applies item 3), C5/C6 (global body radiation), B6 or a later ICR (an instrument field for item 4 and possibly for the air constants).
