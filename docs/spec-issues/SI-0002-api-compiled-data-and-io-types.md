# SI-0002 SPEC-API compiled-data and I/O types have no members; buildLocalBridge has no fs; EngineConfig.threads undefined

Status: resolved · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-API "Descriptions", "Compile functions", "Real-time interface", "Diagnostics"; SPEC-BODY "Contracts"

## Failing test
None. Found in the P0.4 code review. P0.4-R1 requires every SPEC-API type to be declared; SPEC-API names these types without members, so P0.4 had to choose layouts.

## Evidence
| Type or item | Gap | Provisional choice in interfaces-v1 (2daee85) |
| --- | --- | --- |
| StringModes | Contents listed in SPEC-STR, no members | fs, L, tension, rhoA, EI, inharmonicity; k, omega, sigma (double); oscV, oscH; phiBar (float) |
| SegmentBridgeTerms | Contents listed in SPEC-BRG, no members | end, g, a (float), tOverL |
| BodyModes | Contents spread over SPEC-BODY "Modal data" and "Radiation weights" | numModes, numPoints, numChannels; omega, sigma, phi, alpha, beta, channelDelay, residualMobility (double) |
| LocalBridge | Contents in SPEC-BODY "Local bridge model" | point, modeIndex, omega, sigma, phi, conductanceHf, fSplit |
| buildLocalBridge | No fs argument, so it cannot produce oscillator coefficients | LocalBridge stores no coefficients; compileInstrument derives them |
| AudioOut | Named only | `std::span<float* const> channels`, non-interleaved float32, host-owned |
| Diagnostic | SPEC-API lists the quantities, not the record | kind, group, sample, value; `group` documented as "note group (key) index" |
| EngineConfig.threads | Meaning not given | "Audio worker threads, including the audio thread; >= 1" |

## Proposed correction
None of these changes behaviour; they record the layout P0.4 chose and close two ambiguities.

1. SPEC-API: a "Compiled-data and I/O types" table stating each type's contents and that the member layout is fixed by the frozen headers.
2. SPEC-BODY "Contracts": buildLocalBridge is sample-rate independent; compileInstrument derives the kept modes' oscillator coefficients at fs. This matches SPEC-INST ("sample-rate-dependent data is compiled on load").
3. Diagnostic.group is the index into InstrumentDesc::groups, not the key. Groups without a key (sympathetic-only strings) and plucked instruments would make a key ambiguous.
4. EngineConfig.threads is the total number of threads that run Phase A, the audio thread included; 1 means no workers; output does not depend on it.

## Resolution
Resolved by the planner on 2026-10-11 in SPEC-API (new "Compiled-data and I/O types" paragraph and table, EngineConfig.threads) and SPEC-BODY "Contracts" (commit "PLAN: file spec issues SI-0001 to SI-0007"). No human review needed.

Follow-up (documentation-only ICR, interface agent, bundled with SI-0001): in `diagnostics.hpp`, change "Note group (key) index" to "Index into InstrumentDesc::groups". No member, type or signature changes.

Affected phases: A2, A4, B2, B3, C1, C2, C4, C5 (no rework; they read the clarified text).
