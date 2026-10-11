# SI-0001 compileString contract omits registeredPoints

Status: resolved · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-STR "Contract"; SPEC-API "Compile functions"

## Failing test
None. Found in the P0.4 code review: the two SPEC sections give different signatures for the same function.

- SPEC-STR: `StringModes compileString(const StringParams&, double fs, double fMax)`.
- SPEC-API: `compileString(const StringParams&, double fs, double fMax, std::span<const double> registeredPoints)`.

## Evidence
SPEC-STR requires "the shape vectors φ̄ at every registered point", which the three-argument form cannot receive. The frozen header (`engine/include/pme/string.hpp`, interfaces-v1 = 2daee85) follows SPEC-API and adds `Status` plus an out-parameter (ADR-0006).

A second gap sits behind the first: φ̄\_n = φ\_n(x₀) W(k\_n) needs the window width b, and `registeredPoints` carries positions only. The header's `StringModes::phiBar` documentation says "window-averaged", which compileString cannot compute without b.

## Provisional choice (P0.4)
`registeredPoints` holds positions x₀ in metres; `StringModes::phiBar[p][n]` is documented as window-averaged at point p.

## Proposed correction
SPEC-STR "Contract" takes the SPEC-API signature and states:

- registered points are positions x₀ in metres on the segment's own x axis, returned in argument order;
- compileString evaluates point shapes φ\_n(x₀) (W = 1);
- the caller that knows the window width (compileInstrument) forms φ̄\_n = φ\_n(x₀) W(k\_n) in float64 from L, k\_n and x₀, then stores float32;
- the bridge needs no registered point (compileBridgeTerms provides g\_n).

The formula for φ̄ and the float64 rule are unchanged, so this is not a physics or numerics change.

## Resolution
Resolved by the planner on 2026-10-11 in SPEC-STR "Contract" (commit "PLAN: file spec issues SI-0001 to SI-0007"). No human review needed: no formula, tolerance or behaviour changes.

Follow-up (routed through [ICR-0001](../icr/ICR-0001-interfaces-v2.md); documentation-only, interface agent): change the `StringModes::phiBar` comment in `string.hpp` and the `compileString` parameter comment from "window-averaged" to "point shape values φ\_n(x₀), W = 1". The signature and layout stay. Bundle with the SI-0002 follow-up.

Affected phases: A2 (compileString), C1 (compileInstrument applies W), A5, A6, P1, P2 (consumers of windowed shapes).
