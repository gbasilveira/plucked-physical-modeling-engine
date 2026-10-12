# SI-0018 SPEC-BRG explicit coupling order is unstable for very light bridges

Status: resolved (owner decision 2026-10-12: both schemes, selectable) · Raised by: A4, implementer · Date: 2026-10-12
SPEC section: SPEC-BRG "Discrete coupling order (normative, per sample)"; SPEC-BODY "Local bridge model"

## Failing test
None fails today; found by A4's stability experiment (docs/dev/bridge.md, "Stability observations (explicit coupling)"). Lossless strings at 262 Hz, L = 0.65 m, d = 1 mm, f\_max 20 kHz, lossless mass–spring bridge at 300 Hz, 48 kHz, float64 states, 4 × 10⁵ samples; Z₀ = 2.1 kg/s, Z₀Δt = 4.4 × 10⁻⁵ kg:

| m (kg) | 1 string | 3 strings | 8 strings |
| --- | --- | --- | --- |
| 10⁻¹, 10⁻², 10⁻³ | stable, energy error 2–5 × 10⁻¹⁴ | stable | stable |
| 10⁻⁴ | stable | unstable (overflow) | unstable |
| 10⁻⁵ | unstable | unstable | unstable |

## Evidence
The SPEC order is explicit (bridge advanced with Fⁿ, strings with δ\_tt wⁿ). As a symmetric linear system it is stable exactly while the generalised eigenvalues of the coupled (K, M) stay below 4/Δt²; A4 observes the limit near m ≈ N\_strings · Z₀Δt. Real bridge masses are tenths of a kilogram, but a modal-set bridge (effective mass Φ\_k(x\_b)⁻² per mode) with a weakly coupled mode and many segments on one point could approach it. SPEC states no condition, and `BridgePoint::configure` accepts any m > 0.

## Proposed correction (for decision)
Option A, recommended default: keep the explicit order and add to SPEC-BRG "Discrete coupling order": "The explicit order is stable only while the spectral radius of the coupled update of a bridge point and its segments is at most 1. `configure` (non-audio) MUST check it — by power iteration on the linear update of the point's strings and bridge modes — and return `Rejected` when the largest eigenvalue of the (K, M) pencil exceeds (1 − 10⁻³)·4/Δt²; the rule of thumb is that each bridge mode's effective mass Φ\_k(x\_b)⁻² must exceed a few times Σ\_s Z₀,s Δt." A4 implements the check and a test at the table's unstable rows.

Option B: drop the offending bridge modes (as the bandwidth rule drops modes) instead of refusing.

Option C: make the bridge step implicit (one scalar solve per point and axis), which removes the limit at a small cost per sample.

This changes numerics: human review is needed before SPEC changes. It does not block A4's completion (no test depends on it).

## Resolution
Open. Recommended default: Option A.

## Owner decision (2026-10-12)

**Implement both, selectable (decision D-13).**

- The explicit order stays as the Realtime default.
- The implicit bridge step is added (Option C above). It is stable for any bridge mass. The planner checked it on this issue's configuration: spectral radius 1 within 3 × 10⁻¹⁴ down to m = 10⁻⁶ kg with 8 strings, where the explicit order diverges (m = 10⁻⁴ kg with 3 strings: spectral radius 1.19).
- In explicit mode `configure` runs Option A's stability check. The engine setting `numerics.unstableBridge` then decides:
  - **SwitchToImplicit** (default): that bridge point alone uses the implicit step, and a Diagnostic says so. Instruments never fail to load because of it.
  - **Reject:** loading fails with `Rejected`, naming the point.

**Why SwitchToImplicit is the default:**

- A real-time instrument must load.
- The implicit step costs about one extra dot product per segment and axis per sample at the affected point only, so the price is small and local.
- Option B (dropping modes) changes the instrument's sound silently and is not offered.

SPEC-BRG "Bridge scheme" and "Stability of the explicit order", SPEC-CONV "Quality and numerical options". Implemented by phase **A9** (engine/src/bridge). Interface members: ICR-0002.
