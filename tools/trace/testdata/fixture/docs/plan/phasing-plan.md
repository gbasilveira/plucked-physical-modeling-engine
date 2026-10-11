# Fixture plan (V1 test data; a small excerpt in the format of docs/plan/phasing-plan.md)

## Track A — Fixture

### A1 Fixture oscillator

- **Mode:** first phase. **Depends on:** nothing.
- **Description:** a phase card in the format the extractor reads.
- **Requirements:**
  1. A1-R1 `compileOscillator` compiles the coefficients.
  2. A1-R2 Bandwidth rule rejects modes above the limit.
  3. A1-R3 `ModeBank` holds the aligned arrays.
- **Outputs:** engine/src/osc/.
- **Tests:** ORC-01; A1-T1 coefficients agree with the reference; A1-T2 bandwidth rejections at three sample rates.
- **References:** SPEC-OSC.

### A2 Fixture string

- **Mode:** after A1.
- **Description:** a second card, still in progress.
- **Requirements:**
  1. A2-R1 The string compiles.
  2. A2-R2 The lint job checks the sources.
- **Outputs:** engine/src/string/.
- **Tests:** A2-T1 wound-string mass unit tests.
- **References:** SPEC-STR.

## Gates

| Gate | Requires complete | Exit criteria | Unblocks |
| --- | --- | --- | --- |
| G1 Fixture gate | A1 | ORC-01 passes | A2 |
