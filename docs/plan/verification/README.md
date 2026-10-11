# Planning-time numerical checks

These scripts were run while the engine specification was written, to check its derivations before any engine code existed. Their results are the rows marked ✓ in the Verification oracles section of [../engine-specification.md](../engine-specification.md). They are planning evidence, not engine tests: the engine's own oracle tests (ORC-01 to ORC-19) are written in phases P0.3, Track A and Track P.

Requires Python 3.12 and NumPy. Run from this folder:

| Script | What it checks | Expected result |
| --- | --- | --- |
| `osc.py` | Exact-pole oscillator; float32 difference form vs float64 and vs the naive two-term recursion | Difference form within 0.001 cent at 27.5 Hz; naive float32 recursion about 1.6 cents off |
| `bridge.py` | String on a moving bridge (lifting function) with a pure conductance | Decay rate equals −f₀ ln[(1 − Z₀G)/(1 + Z₀G)] to about 10⁻⁴ |
| `check2.py` | Raised-cosine window transform, closed-form tension for pitch, two strings on one bridge, rear segment | Window and tension match to round-off; in-phase decay 4 f₀ Z₀ G, anti-phase near zero |
| `sav.py` | SAV contact: mass on a rigid felt contact and hammer on a modal string | Contact time within 0.4% at 48 kHz; energy conserved to about 7 × 10⁻¹⁴ |
| `longi.py` | Longitudinal forcing pair sum and tension-modulation integral vs quadrature | Agreement to round-off |
| `osc_energy_f32.py` | Float32 round-off drift of the oscillator energy (ORC-02, [SI-0009](../../spec-issues/SI-0009-orc02-float32-energy-tolerance.md)); options `--evidence`, `--sweep`, `--resonance`; about 30 s, 70 s with `--sweep` | Correct kernel below 0.4 of the SI-0009 tolerance; one-ulp β error 36× to 63× above it at 27.5 Hz and 4186 Hz |

```bash
cd docs/plan/verification
for f in osc.py bridge.py check2.py sav.py longi.py; do python3 "$f"; done
```
