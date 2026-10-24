# SI-0011 SPEC-NL drift control must act on |ψ|, not on ψ

Status: resolved · Raised by: A3 (implementer and code-reviewer) · Date: 2026-10-11
SPEC section: SPEC-NL "Drift control" (rules 1 and 2)

## Failing test
None. A3 implemented both rules on the magnitude of ψ and kept the sign; the A3 review added a negative-ψ reset test (commit 7961138, engine/tests/nonlinear/). The SPEC text, read literally, says something else.

## Evidence
ψ is a linear state of the scheme: ψ^(n+½) = ψ^(n−½) + ½ (**g**ⁿ)ᵀ(zⁿ⁺¹ − zⁿ⁻¹). Nothing keeps it positive, and it can be negative when a contact separates. Only ½ψ² enters the energy, so ψ and −ψ store the same energy.

SPEC said:

- Rule 1: "When Vⁿ = Vⁿ⁻¹ = 0, set ψ ← √(2ε₀). This can only remove energy."
- Rule 2: "relax ψ towards √(2(V + ε₀)) … only when ψ is above it."

Applied literally to a negative ψ, rule 1 replaces −|ψ| by +√(2ε₀), a sign flip that the "only removes energy" argument does not cover, and rule 2 never fires for a negative ψ however large |ψ| is. Both rules are intended for the magnitude: reset |ψ| to √(2ε₀) and relax |ψ| from above, keeping the sign. A3 does exactly this, which only removes energy.

## Proposed correction
Text fix, no behaviour change for ψ ≥ 0 and no change to A3:

- Introduction: state that ψ is linear and may be negative, and that both rules act on |ψ| and keep the sign.
- Rule 1: ψ ← sign(ψ)·√(2ε₀), with sign(0) = +1.
- Rule 2: relax |ψ| towards √(2(V + ε₀)), keeping the sign, only when |ψ| is above it.

## Resolution
Resolved 2026-10-11 by the orchestrator as a clarifying text fix: it matches the existing A3 implementation and the intent of the rules (energy is only removed), so it changes no physics or numerics and needs no human reviewer. SPEC-NL "Drift control" updated in the commit that files this issue (`PLAN: file SI-0011 …`).

Affected phases: A3 (no change; already conforms), A5, A7, P2 to P4 (consume savStep), V1 (traceability only).
