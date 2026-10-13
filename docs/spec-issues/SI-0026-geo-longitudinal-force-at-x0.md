# SI-0026 SPEC-GEO defines the longitudinal bridge force only at x = L

Status: resolved (2026-10-12, planner ratifies A7's choice; convention, no change to physics) · Raised by: A7, implementer · Date: 2026-10-12
SPEC section: SPEC-GEO "Longitudinal force at the bridge"

## Failing test
None. Rear and duplex segments have their bridge at x = 0 (`bridgeEnd = At0`), but SPEC-GEO gives F\_L = EA Σ √(2/L) K\_m (−1)^m ξ\_m + (EA − T)/2 · y\_x(L)², the tension excess at x = L.

## Evidence
F\_L is the axial tension excess EA ξ\_x + (EA − T)/2 · (y\_x² + z\_x²) at the bridge end. With χ\_m = √(2/L) sin(K\_m x), ξ\_x = Σ √(2/L) K\_m cos(K\_m x) ξ\_m, so cos(K\_m L) = (−1)^m at x = L and 1 at x = 0. A7 evaluates rear segments at x = 0, dropping (−1)^m, which is this formula. The tension excess is a scalar; the direction in which it pulls the bridge is geometry, carried by κ\_L (fitted).

## Proposed correction
SPEC-GEO: name F\_L "the axial tension excess at the segment's bridge end", and add: "For a bridge at x = 0 (rear segments, BridgeEnd At0) the same quantity is evaluated at x = 0: F\_L = EA Σ\_m √(2/L) K\_m ξ\_m + (EA − T)/2 · y\_x(0)², without the factor (−1)^m. The direction of the pull on the bridge is carried by κ\_L."

## Resolution
Applied to SPEC on 2026-10-12. A7's implementation stands. Affected phases: A7 (none), C1 (composes rear segments).
