# Spec issues (SI)

Agents never deviate from [../plan/engine-specification.md](../plan/engine-specification.md) silently. When a test cannot pass because SPEC is wrong or ambiguous:

1. The agent stops work on the affected requirement and describes the defect in its handoff report, with the failing test and evidence.
2. The `planner` (Opus) files `SI-NNNN-<slug>.md` from the template below.
3. The orchestrator resolves it. Any change to physics or numerics goes to a human reviewer before SPEC is changed.
4. The planner updates SPEC, links the change here, closes the issue, and the agent resumes.

## Template

```markdown
# SI-NNNN <title>

Status: open | accepted | rejected | resolved · Raised by: <phase ID, role> · Date: <YYYY-MM-DD>
SPEC section: <key and heading>

## Failing test
Test or oracle ID, CI run link, observed versus expected values.

## Evidence
Derivation, reference-solver result or literature that shows the defect.

## Proposed correction
The exact SPEC text and formula change.

## Resolution
Decision, human reviewer (for physics or numerics), SPEC commit, affected phases.
```

## Index

| ID | Title | Status | Blocks |
| --- | --- | --- | --- |
| [SI-0001](SI-0001-compile-string-registered-points.md) | compileString contract omits registeredPoints | resolved | |
| [SI-0002](SI-0002-api-compiled-data-and-io-types.md) | Compiled-data and I/O types; buildLocalBridge fs; EngineConfig.threads | resolved | |
| [SI-0003](SI-0003-body-description-types.md) | Body and bridge-point description types | accepted; route ICR-0001 | B1, B2 (until interfaces-v2) |
| [SI-0004](SI-0004-tuning-keys-and-segment-addressing.md) | Tuning curve, key numbering, unison detune, segment addressing | accepted; route ICR-0001 | B1, C1 (until interfaces-v2) |
| [SI-0005](SI-0005-plucked-exciter-interfaces.md) | Plucked exciters: Pluck event, finger, initial displacement | accepted; route ICR-0001 | P1 (until interfaces-v2) |
| [SI-0006](SI-0006-compiled-instrument-retirement.md) | No retire mechanism for the previous CompiledInstrument | accepted; route ICR-0001 | C1, C4, C6 (until interfaces-v2) |
| [SI-0007](SI-0007-parameter-registry.md) | Parameter registry ranges, soft-pedal duplication, output gains, f_max | accepted; route ICR-0001 | C4 (until interfaces-v2) |
| [SI-0008](SI-0008-oscillator-coefficient-cancellation.md) | SPEC-OSC ω̂²Δt² formula loses digits at small ωΔt; exact sech form proposed | open (recommended: accept; needs human review) | A1 merge (A1-T1) |
