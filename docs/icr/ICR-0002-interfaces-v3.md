# ICR-0002 interfaces-v3: engine quality and numerical options, flexible shank, plate prestress and moisture reference, carried items

Status: approved (orchestrator, 2026-10-12, under owner decision D-13; owner's final review pending) · Raised by: planner, from owner decisions D-13 and the 2026-10-12 answers on SI-0018, SI-0020, SI-0021 and the B2 prestress question · Date: 2026-10-12

## Problem

The owner's decisions of 2026-10-12 need public interface members that interfaces-v2 (300b995) does not have:

1. **D-13 (standing policy):**
   - Alternative schemes are implemented behind settings.
   - There is an engine quality level (Realtime by default, HighQuality for slower or offline use).
   - Settings are non-automatable configuration, persisted by plug-ins.

   `EngineConfig` has no field for any of this, so `compileInstrument` cannot receive the bridge scheme (A9), the unstable-bridge policy (A9), the overdamped bridge-mode policy (A9) or the geometric tier (A7, A10). SPEC-CONV "Quality and numerical options" defines them.
2. **Bridge scheme switch diagnostic** (SPEC-BRG "Stability of the explicit order"): `DiagnosticKind` has no value for "bridge scheme switched".
3. **Flexible shank (A5-R6):** an instrument option per SPEC-HAM "Flexible shank". `HammerParams` has no field for it.
4. **Plate prestress:** the owner wants crown and downbearing prestress as an optional instrument-file field, absent meaning no prestress. B2 takes prestress only as a compiler API argument.
5. **Moisture reference (SI-0013):** `PlateParams::moistureRef`, default 0.12. compileBody uses a constant today.
6. **Items carried from ICR-0001 and A4** (docs/icr/README.md, "Carried to the next ICR"): `Defaults::muteFeltSigma`, `SegmentBridgeTerms::aH`, and four comment-only clean-ups.

## Proposed change

All members are new members with defaults, so the change is source-compatible: code that does not set them keeps today's behaviour. `interfacesVersionMajor` becomes 3.

```cpp
// engine/include/pme/engine.hpp
enum class EngineQuality : std::uint8_t { Realtime, HighQuality };          // SPEC-CONV, D-13
enum class BridgeScheme : std::uint8_t { Auto, Explicit, Implicit };        // SPEC-BRG "Bridge scheme"
enum class UnstableBridgePolicy : std::uint8_t { SwitchToImplicit, Reject };// SPEC-BRG "Stability of the explicit order"
enum class OverdampedBridgeModes : std::uint8_t { Accept, Reject };         // SPEC-OSC "Bandwidth"
enum class GeometricTier : std::uint8_t { Auto, Default, Exact };           // SPEC-GEO "High-quality tier"
struct NumericsOptions {
    BridgeScheme bridgeScheme{BridgeScheme::Auto};
    UnstableBridgePolicy unstableBridge{UnstableBridgePolicy::SwitchToImplicit};
    OverdampedBridgeModes overdampedBridgeModes{OverdampedBridgeModes::Accept};
    GeometricTier geometricTier{GeometricTier::Auto};
};
struct EngineConfig {
    /* existing members unchanged */
    EngineQuality quality{EngineQuality::Realtime};
    NumericsOptions numerics{};
};
/// Resolves Auto against the quality level (SPEC-CONV table). Header-only, constexpr.
constexpr NumericsOptions resolvedNumerics(const EngineConfig&) noexcept;

// engine/include/pme/diagnostics.hpp
enum class DiagnosticKind : std::uint8_t { /* existing */ BridgeSchemeSwitched };  // value = bridge point index

// engine/include/pme/exciters.hpp
struct ShankParams { double mass{}; double frequency{}; double sigma{}; };   // m_S kg, f_S Hz, sigma_S 1/s
struct HammerParams { /* existing members */ std::optional<ShankParams> shank; };  // absent = rigid hammer

// engine/include/pme/body.hpp
struct PlatePrestress { double nx{}; double ny{}; double nxy{}; };           // membrane stress resultants, N/m
struct PlateParams {
    /* existing members */
    double moistureRef{0.12};                   // MC_ref, SI-0013
    std::optional<PlatePrestress> prestress;    // crown and downbearing; absent = none
};

// engine/include/pme/instrument.hpp (Defaults)
double muteFeltSigma{20.0};                     // sigma_felt, 1/s, SI-0010

// engine/include/pme/instrument.hpp (SegmentDesc) — added 2026-10-12 for A7 (SPEC-GEO "their losses use their own b1, b3")
double longitudinalB1{0.0};                     // 1/s
double longitudinalB3{0.0};                     // s

// engine/include/pme/bridge.hpp
struct SegmentBridgeTerms { /* existing */ std::vector<float> aH; };       // horizontal a_n (A4)
```

`compileBridgeTerms` fills `aH` with what A4's `horizontalForceCoefficients` computes today. The matched-coupling terms of the implicit scheme (g̃, ã) stay internal to `engine/src/bridge` (A9). They are a scheme detail, not part of the contract.

**Parameter registry and plug-in state:** `params.json` is unchanged. The new settings are configuration, not automatable parameters (SPEC-API "EngineConfig.quality and EngineConfig.numerics"). Plug-in state persistence is D6-R5.

**Instrument file (B1 schema), done by B6, not by the interface agent:**

- `hammer.shank` {mass, frequency, sigma} (optional);
- `plate.moistureRef` (default 0.12);
- `plate.prestress` {nx, ny, nxy} (optional).

These are new optional fields, so no schema-version migration is needed.

**Documentation-only items carried from ICR-0001:**

- the stale "interfaces-v1" comments in nonlinear.hpp and pme.hpp;
- the redundant `pedal.soft` (1003) description;
- docs/dev/interfaces.md listing non-audio functions under "Real-time functions";
- the nonlinear.hpp comment that says A5 declares the SAV types (A3 does).

## Phases affected

| Phase | Impact | Must rebase |
| --- | --- | --- |
| A5 (in flight) | Implements the shank internally (A5-R6) against an internal `ShankParams` {mass, frequency, sigma} mirror (confirmed by the A5 implementer, 2026-10-12). Switches to the public type when v3 lands. | yes, once v3 is applied |
| A7 (in flight) | Exposes `GeometricTier` (Auto, Default, Exact) and `EngineQuality` (Realtime, HighQuality) as internal enums with the v3 names and values (`Exact` returns `NotImplemented` until A10); uses internal longitudinal loss parameters until `SegmentDesc::longitudinalB1/B3` (default 0) land; switches to the public types when v3 is applied (confirmed by the A7 implementer, 2026-10-12); B6 adds the matching schema fields. | yes |
| A9 (new) | Implements the bridge options internally first, then reads `EngineConfig` through C1. | yes |
| A10 (new) | Uses `GeometricTier::Exact`. | starts after v3 |
| B6 (new) | Adds the schema fields and loader mapping for `moistureRef`, `prestress` and `shank`. | starts after v3 |
| B2 (in review) | No code change. The `prestress` API argument stays; B6 wires the field to it. | no |
| C1, C4, C7, C8, D6 (not started) | C1 resolves `numerics` per bridge point and segment and emits `BridgeSchemeSwitched`. C4 treats a settings change as structural. C8 exposes the settings as CLI flags and Python arguments. D6 persists them in plug-in state. | brief after v3 |
| A4, A6 (complete) | None. A4's `horizontalForceCoefficients` stays as the implementation of `aH`. | no |

## Tests that must change

Test names follow docs/icr/README.md: the owning phase's ID, then the ICR ID.

- `P0.4-T3 ICR-0002 interfacesVersionMajor is 3`.
- `P0.4-T1 ICR-0002 EngineConfig defaults are Realtime and Auto`.
- `P0.4-T1 ICR-0002 resolvedNumerics follows the SPEC-CONV table` (all 2 × 3 × 2 × 2 × 3 combinations).
- The `compileBridgeTerms` SUBCASE in engine/tests/interfaces/test_stubs.cpp is A4's, so A4's tests check `aH` (`A4-R1 ICR-0002 compileBridgeTerms fills aH from oscH`).

## Decision

**Approved by the orchestrator, 2026-10-12,** under owner decision D-13. The owner stated (orchestrator's paraphrase) that options and settings are preferable to leaving alternatives unimplemented, and that the owner will approve everything at the end given detailed documentation. The ICR protocol (docs/icr/README.md, step 2) gives the approval to the orchestrator, so no separate owner approval is required to apply it; the change is listed for the owner's final review. Next: one interface agent (implementer, Sonnet) applies the change and the interfaces-v3 tag, **after A5, A7, A8 and A9 have merged** (orchestrator scheduling decision, 2026-10-12); the planner updates SPEC-API where it changes; A5, A7, A9 switch from their internal mirrors to the public types in follow-ups; B6 and A10 are briefed against v3.

Earlier note: Physics review: the prestress field changes body modes and was approved in principle by the owner (2026-10-12). The B2 reviewer checks the prestress computation (B2-R4). Interfaces tag after applying: interfaces-v3.

## Amendment proposed 2026-10-13 (SI-0032 item 2; pending the orchestrator's approval)

```cpp
// engine/include/pme/string.hpp
struct StringModes { /* existing */ std::vector<double> omegaH; };  // horizontal undamped omega_n (rad/s), detuneH applied
```

Compatible (new member, empty by default). `compileString` fills it with what `horizontalOmega(omega, detuneH)` computes today; `applyMute` leaves it unchanged. Reason: the implicit bridge's matched coupling g̃\_n = g\_n ω\_n/ω̂\_n needs the horizontal ω\_n, which `StringModes` does not carry (A9 uses the vertical one; exact only at detuneH = 0). Phases affected: A2 (fills it; test `A2-R5 ICR-0002 compileString fills omegaH`), A9 follow-up (uses it; test at detuneH ≠ 0). Not yet part of the approved change above until the orchestrator approves this amendment.
