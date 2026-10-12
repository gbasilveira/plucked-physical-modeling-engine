# D-3 Product name and plug-in identifiers — proposal

Status: **proposal ready, awaiting the owner's choice** · Written by: planner (Opus) · Date: 2026-10-12
Decision: D-3 (phasing plan, "Decisions and external prerequisites"; phases.yaml `decisions.D-3`). Blocks: D2 (VST3, AUv2 and standalone). Affects: D1, D3, D5 identifiers (provisional today), H2 packaging, H3 website.

The owner asked us to suggest; the owner decides. Nothing below is frozen until the owner picks a name. Once picked, the identifiers are frozen for the life of the product: hosts store them in projects, so changing them later breaks every saved session.

## 1. What has to be chosen

| Item | Where it lives today (provisional) | Must it ever change after release? |
| --- | --- | --- |
| Product name (shown in hosts, installers, website) | `kPluginName = "pme"` in `plugin/common/ids.hpp` | Can change (display only), but avoid it |
| Vendor name | `kPluginVendor = "pme project"` | Can change (display only) |
| CLAP plug-in ID (reverse-DNS string) | `kPluginId = "pme.instrument"` | **Never** (hosts key saved state on it) |
| VST3 class IDs (two 128-bit GUIDs: processor and controller; clap-wrapper derives or takes them) | not set yet (D2) | **Never** |
| AU manufacturer code and subtype (four characters each), type `aumu` | not set yet (D2) | **Never** |
| LV2 plug-in URI | `kLv2PluginUri = "https://pme.invalid/lv2/instrument"` | **Never** |
| Data directory name | `kDataDirName = "pme"` (D5) | Only with a migration |
| Preset file extension and format string | `kPresetExtension = ".pmepreset"`, `kPresetFormat = "pme-preset"` (D5) | Only with a migration |

## 2. Name candidates

Rule from the phasing plan (Software guidelines, Licensing; H1-R6): names contain no trademarks. The checks below were web searches on 2026-10-12 for audio plug-ins, virtual instruments and music software with the same or a confusingly similar name. **They are not a trademark clearance.** Before the name is published, H1-R6 needs a search of the USPTO, EUIPO and WIPO databases in classes 9 (software) and 15 (musical instruments); a paid clearance is advisable if the product will be sold or heavily promoted.

| # | Candidate | Meaning and fit | Conflicts found (web search) | Assessment |
| --- | --- | --- | --- | --- |
| 1 | **Sympathon** | From "sympathetic" (sympathetic resonance, the engine's distinguishing feature across piano, sitar and tanpura) plus a Greek-style ending. Easy to say in English, Portuguese and Hindi. | No product named Sympathon found. Nearby names: *Sympathizer* (SoundBytes, a comb-filter resonance effect) and *Symptohm* (Ohm Force). Different words, but Sympathizer is in the same niche. | Recommended. Distinctive, describes the engine, no exact match found. |
| 2 | **Cordyne** | "Corda" (string, in Italian and Portuguese) plus "dyne" (the unit of force): strings driven by physics. | No product or company found under this spelling. | Strong alternative. Neutral across piano and plucked instruments. Spelling may need saying twice. |
| 3 | **Tensora** | From "tension", the quantity that sets pitch and drives the geometric nonlinearity. | No audio product found. | Good. Less evocative of sound than 1 or 2. |
| 4 | **Aftersound** | The piano's "aftersound" (Weinreich's two-stage decay), which the engine reproduces through bridge coupling. | No product with that name found; *Aftertone* (a browser audio extension) and *Afterimage* (a reverb plug-in) are close. | Descriptive and memorable, but closer to existing names and is an ordinary English word, so weaker as a mark. |

Rejected during the search, kept here so they are not proposed again:

- *Plectra*: used by Impact Soundworks for its "Plectra Series" of plucked-instrument libraries.
- *Corda*: "Una Corda" is a Native Instruments product (also the piano pedal name).
- *Modalis* or similar: too close to IRCAM's *Modalys* physical-modelling software.
- Instrument terms on their own (*Jawari*, *Tanpura*, *Sawari*): generic words, cannot be protected, and would be misleading for a multi-instrument engine.

## 3. Identifier scheme (applies to whichever name is chosen)

Write `<name>` for the lower-case ASCII product name (for example `sympathon`) and `<Name>` for its display form. The scheme does not depend on owning a domain, but works better with one (see the LV2 note).

| Identifier | Proposed scheme | Example for "Sympathon" |
| --- | --- | --- |
| Display name | `<Name>` | Sympathon |
| Vendor | `<Name> Project` until a legal entity exists | Sympathon Project |
| CLAP ID | `io.github.gbasilveira.<name>` (reverse of the repository host, which the owner controls; one ID per plug-in, so a future effect would be `io.github.gbasilveira.<name>.<effect>`) | `io.github.gbasilveira.sympathon` |
| VST3 processor and controller class IDs | Two version-5 UUIDs (name-based, SHA-1) from the CLAP ID with fixed suffixes `#processor` and `#controller`, under a project namespace UUID generated once and recorded in `plugin/common/ids.hpp` and an ADR. Deterministic and reproducible from the CLAP ID. | computed when the name is fixed |
| AU type, subtype, manufacturer | type `aumu` (instrument); subtype: four lower-case letters from the name; manufacturer: four characters with at least one upper-case letter (Apple reserves all-lower-case codes), unique among registered codes (Apple's registration is free) | subtype `symp`, manufacturer `Symp` (check Apple's list before freezing) |
| LV2 URI | `https://gbasilveira.github.io/<repository>/lv2/<name>` (a URL the project controls, so it never collides; it need not resolve) | `https://gbasilveira.github.io/plucked-physical-modeling-engine/lv2/sympathon` |
| LV2 bundle and binary | `<name>.lv2`, `<name>_lv2` | `sympathon.lv2` |
| Data directory (`kDataDirName`) | `<Name>` on macOS and Windows (Application Support, Documents), `<name>` on Linux (XDG) | `Sympathon` / `sympathon` |
| Preset extension (`kPresetExtension`) | `.<short>preset` where `<short>` is 3 to 5 letters from the name, checked for clashes with registered file extensions | `.sympreset` |
| Preset format string (`kPresetFormat`) | `<name>-preset` | `sympathon-preset` |

Notes:

- **Today's provisional values** (`pme`, `pme.instrument`, `https://pme.invalid/...`) were never shipped. D2 and D6 replace them, D5 changes three constants in `ids.hpp`, and no migration is needed.
- **Codename.** The C++ namespace `pme`, the Python package `pme_compiler` and the repository name stay as they are. They are internal and are never shown to users.
- **If the owner later registers a domain**, only the LV2 URI and the CLAP ID would read better with it. Both are frozen at first release, so a domain should be chosen before G5 if at all.

## 4. What the owner needs to answer

1. The product name: one of the four candidates, or another of the owner's choice (we then run the same checks).
2. Confirmation of the identifier scheme in section 3, or changes to it.
3. Whether a formal trademark search is wanted before release (recommended if the product will be sold or promoted under the name).

On the answer, the planner records D-3 as answered in the phasing plan and phases.yaml, writes the ADR with the frozen identifiers, and D2 becomes ready once D1 is complete.

## Sources (web searches, 2026-10-12)

- SoundBytes Sympathizer: https://www.kvraudio.com/product/sympathizer_by_soundbytes
- Impact Soundworks Plectra Series: https://www.adorama.com/impact-soundworks-plectra-series-3-kazakh-dombra-virtual-instrument/p/is122447
- Native Instruments Una Corda: https://www.kvraudio.com/news/native-instruments-releases-una-corda-for-kontakt-player-32113
- Searches for "Cordyne", "Tensora" and "Aftersound" returned no audio product of that name.
