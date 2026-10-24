# SI-0015 SPEC-GEO one-way longitudinal coupling does not conserve energy

Status: resolved · Raised by: P0.5, implementer and code-reviewer · Date: 2026-10-11
SPEC section: SPEC-GEO "Longitudinal modes", "Neglected in the default tier"; SPEC-EXEC "Sleep and wake"; SPEC-BODY "Sympathetic path" (guard)

## Failing test
None. The P0.5 reference energy trace covers the transverse subsystem only (reference/src/geometric.cpp header) for this reason; the P0.5 reviewer measured the longitudinal energy in the large-amplitude P0.5-R6 case at about 3 % of the transverse energy, scaling as amplitude⁴ in absolute terms (amplitude² relative to the transverse energy).

## Evidence
- SPEC-GEO's default tier drives the longitudinal modes with the explicit pair-sum force f\_m^L from the transverse slopes and lists "back-coupling from longitudinal motion to the transverse modes" as neglected. The work done on the longitudinal modes therefore has no counterpart taken from the transverse modes: total energy (transverse + ψ + longitudinal) is not conserved, by construction.
- f\_m^L ∝ q², so the longitudinal energy is ∝ q⁴ while the transverse energy is ∝ q².
- SPEC does not say this. An energy monitor that sums all energies (ORC "Hammer–string energy" style checks, C1's per-group energy, the sympathetic guard "energy rises for 4 consecutive sub-blocks") would flag a correct implementation, and a large pluck could halve κ\_s for no reason.

## Proposed correction
SPEC-GEO, after "Neglected in the default tier": state that the default tier does not conserve total energy, that the transverse energy (modes, bridge and ψ including V\_KC) obeys the usual balance, and that the longitudinal energy E\_L = Σ\_m ρA H\_m^L is extra, ∝ amplitude⁴, about 3 % of the transverse energy in the P0.5 large-amplitude case. Energy monitors MUST check conservation and passivity on the transverse energy only and report E\_L separately; the sympathetic guard tests the transverse energy only; sleep decisions use the transverse energy plus E\_L. The high-quality tier conserves total energy.

## Resolution
Resolved 2026-10-11 by the orchestrator under the owner's proceed instruction, as proposed. It states a property of the existing scheme and which energy each monitor uses; the scheme is unchanged, so no human reviewer is needed. SPEC-GEO updated in the `PLAN: file SI-0012 to SI-0015 …` commit. Affected phases: A7 (energy tests on the transverse subsystem; E\_L reported separately), C1 (per-group energy with E\_L as a separate term), C3 (guard on transverse energy), C6 (sleep on the sum). P0.5 already conforms.

Related deviation note (P0.5-R6): the card says "Tension modulation and longitudinal waves (iterative scheme)". The reference iterates (scalar Newton) on the tension-modulation part only; the longitudinal part follows SPEC-GEO's default tier (one-way, explicit forcing at step n, non-iterative). Accepted by the orchestrator: the card's purpose is an independent check of the engine's default tier, and a two-way iterative longitudinal scheme would model a different system.

## Amendment 2026-10-12 (A7 implementer)
The "about 3 %" figure belonged to one amplitude. A7 measured the time mean of E\_L / E\_T on its test string at 8.2 × 10⁻³ (1 mm initial displacement), 3.3 × 10⁻² (2 mm) and 1.4 × 10⁻¹ (4 mm), the square law SPEC states. SPEC-GEO "Energy balance of the default tier" now gives these values instead of a single percentage. Documentation only; no change to behaviour or tests.
