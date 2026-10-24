# D-3 Brand, product name and plug-in identifiers

Status: **partly answered.** Brand answered by the owner on 2026-10-12: **Sympathon** ("Sympathon is the brand; this engine is just one product out of many to come"). **Product name: open**; candidates in section 3, awaiting the owner's choice. · Written by: planner (Opus) · First proposal 2026-10-12, revised the same day after the owner's answer
Decision: D-3 (phasing plan, "Decisions and external prerequisites"; phases.yaml `decisions.D-3`). Blocks: D2 (VST3, AUv2 and standalone) until the product name is chosen. Affects: D1, D3, D5 identifiers (provisional today), H1-R6 (trademark search), H2 packaging, H3 website.

Once the product name is chosen, the identifiers below are frozen for the life of the product: hosts store them in projects, so changing them later breaks every saved session. The brand-level parts (vendor name, AU manufacturer code, VST3 namespace, data-directory root) are frozen for every Sympathon product.

## 1. What changed with the owner's answer

The first proposal treated "Sympathon" as the product name. Sympathon is now the **vendor and brand** under which several products will ship, so every identifier is two-level: a brand part shared by all products and a product part unique to each. Section 2 fixes the brand part and the rules for the product part; section 3 proposes product names; section 4 shows the identifiers for the recommended candidate so the scheme can be checked.

## 2. Identifier scheme (brand fixed, product part per product)

Write `<product>` for the product's lower-case ASCII name (letters and digits, starting with a letter) and `<Product>` for its display form.

| Identifier | Brand part (fixed) | Product part | Rule |
| --- | --- | --- | --- |
| Vendor name (CLAP `vendor`, VST3 vendor, AU manufacturer name, LV2 `doap:maintainer` name, installer publisher) | `Sympathon` | — | Display string; until a legal entity exists the installers' publisher also reads "Sympathon". |
| Display name | — | `<Product>` | Hosts show "Sympathon <Product>" where they list vendor and name. |
| Reverse-domain root | `com.sympathon` if the owner registers sympathon.com before the first public release (G5); otherwise `io.github.gbasilveira.sympathon` | — | See "Domain" below. Chosen once, before G5, then frozen. |
| CLAP plug-in ID | `<root>` | `.<product>` | One ID per plug-in: `com.sympathon.<product>`; a later effect of the same product: `com.sympathon.<product>.<effect>`. |
| VST3 class IDs | Vendor namespace UUID `22fe81e6-8e41-5c4b-abcb-127aab2bb7fa` = UUIDv5(NAMESPACE\_DNS, "sympathon.com") | Processor: UUIDv5(vendor namespace, "`<product>`/vst3/processor"); controller: UUIDv5(vendor namespace, "`<product>`/vst3/controller") | Deterministic and reproducible with any UUID library (RFC 9562 version 5, SHA-1). The namespace is derived from the name "sympathon.com" whether or not the domain is registered: it is only a name. Recorded in `plugin/common/ids.hpp` and the D2 ADR. |
| AU type | — | `aumu` (music device) | Fixed by Apple for instruments. |
| AU manufacturer code | `Symp` | — | Four characters, at least one upper-case letter (Apple reserves all-lower-case codes). Apple no longer maintains a public registry; no plug-in using `Symp` was found (web search 2026-10-12). Check once with `auval -a` on a machine with many plug-ins before freezing. |
| AU subtype | — | four lower-case letters from `<product>` | Must be unique only among Sympathon's own products of the same type. |
| LV2 plug-in URI | `https://sympathon.com/lv2/` if the domain is registered before G5; otherwise `https://gbasilveira.github.io/sympathon/lv2/` | `<product>` | A URI the project controls; it need not resolve. Frozen at first release. |
| LV2 bundle and binary | — | `sympathon-<product>.lv2`, `sympathon_<product>_lv2` | Bundle names must not collide between Sympathon products installed side by side. |
| Data directory | `Sympathon` (macOS, Windows), `sympathon` (Linux, lower case by XDG convention) | `/<Product>` (macOS, Windows), `/<product>` (Linux) | macOS `~/Library/Application Support/Sympathon/<Product>`; Windows `%APPDATA%\Sympathon\<Product>` (user presets under `Documents\Sympathon\<Product>` if D5 uses Documents); Linux `$XDG_DATA_HOME/sympathon/<product>` (default `~/.local/share/sympathon/<product>`). Products coexist under one brand folder. |
| Preset file extension | `.sympreset` | — | **Recommended: one brand-wide preset container** (see below). |
| Preset format string | `sympathon-preset` | plus a mandatory `product` field holding the product's CLAP ID | A product loads only presets whose `product` equals its own CLAP ID and reports a clear error otherwise. |

**Presets: brand-wide container with a product field (recommended)** rather than one extension per product. Reasons: one file association and MIME type for the brand; a future shared preset browser can list every Sympathon preset; the `product` field already prevents loading a preset into the wrong product; per-product extensions would multiply registrations and still need a version field. Cost: D5 adds and validates the `product` field (a small code change, below).

**Domain.** A Verisign RDAP query for sympathon.com and a PIR RDAP query for sympathon.org both returned "not found" on 2026-10-12, which indicates neither was registered then; the .io registry's RDAP was rate-limited and not checked. A web search found no site or product named Sympathon. **Recommendation: the owner registers sympathon.com (and, optionally, .org) now**, before anything is published; then the `com.sympathon` root and the `https://sympathon.com/lv2/` URIs apply. If the domain is not registered before G5, the fallback roots apply and stay frozen; switching later would need a migration (new IDs plus a state-conversion path in every format), so the choice must be made before G5. No identifier has shipped yet, so there is nothing to migrate today.

**Trademark.** These checks are web searches, not a clearance. **H1-R6 still applies before release:** a search of the USPTO, EUIPO and WIPO databases for "Sympathon" and for the chosen product name in classes 9 (software) and 15 (musical instruments); a paid clearance is advisable if the products will be sold or promoted.

**Internal names unchanged.** The C++ namespace `pme`, the Python package `pme_compiler` and the repository name stay as they are; they are never shown to users.

## 3. Product-name candidates for this engine

The engine is a physical-modelling piano and plucked-string instrument. Each candidate is checked by web search (2026-10-12) for audio plug-ins, virtual instruments and music software with the same or a confusingly similar name.

| # | Candidate | As shown in hosts | Meaning and fit | Conflicts found | Assessment |
| --- | --- | --- | --- | --- | --- |
| 1 | **Cordyne** | Sympathon Cordyne | "Corda" (string, Italian and Portuguese) plus "dyne" (unit of force): strings driven by physics. Neutral across piano and plucked instruments. | No product or company found under this spelling (searches on 2026-10-12, twice). | **Recommended.** Distinctive, pairs well with the brand, short AU subtype `cord`. |
| 2 | **Tensora** | Sympathon Tensora | From "tension", the quantity that sets pitch and drives the geometric nonlinearity. | No audio product found. | Good; less evocative of sound than 1. |
| 3 | **Aftersound** | Sympathon Aftersound | Weinreich's piano "aftersound" (two-stage decay from bridge coupling), which the engine reproduces. | No product of that name; *Aftertone* and *Afterimage* are close. An ordinary English word. | Memorable but weak as a mark and piano-specific. |
| 4 | **Kordal** | Sympathon Kordal | From "chord(e)" (string) with a neutral ending; reads easily in several languages. | No audio product found; it is also a surname. | Acceptable; plainer than 1 and 2. |

Rejected during the searches, so they are not proposed again: *Plectra* (Impact Soundworks "Plectra Series"); *Corda* ("Una Corda", Native Instruments); *Modalis* and similar (too close to IRCAM's *Modalys*); *Lyrion* (Lyrion Music Server, formerly Logitech Media Server); *Chordis* (close to Syntheway *Chordophonet*); instrument words on their own (*Jawari*, *Tanpura*, *Sawari*: generic and misleading for a multi-instrument engine).

## 4. Worked example: identifiers for "Cordyne" (if chosen, domain registered)

| Identifier | Value |
| --- | --- |
| CLAP ID | `com.sympathon.cordyne` (fallback `io.github.gbasilveira.sympathon.cordyne`) |
| VST3 processor class ID | `1827310f-8b11-5f05-8bf6-106f7c665c60` = UUIDv5(`22fe81e6-8e41-5c4b-abcb-127aab2bb7fa`, "cordyne/vst3/processor") |
| VST3 controller class ID | `86ae7c05-4712-50d7-933e-ec2422d34e35` = UUIDv5(same namespace, "cordyne/vst3/controller") |
| AU | type `aumu`, subtype `cord`, manufacturer `Symp` |
| LV2 URI | `https://sympathon.com/lv2/cordyne` (fallback `https://gbasilveira.github.io/sympathon/lv2/cordyne`) |
| LV2 bundle | `sympathon-cordyne.lv2` |
| Data directory | `Sympathon/Cordyne` (macOS, Windows), `sympathon/cordyne` (Linux) |
| Preset extension, format | `.sympreset`, `sympathon-preset`, `product` = `com.sympathon.cordyne` |

Reproduce the UUIDs with Python: `uuid.uuid5(uuid.uuid5(uuid.NAMESPACE_DNS, "sympathon.com"), "cordyne/vst3/processor")`.

## 5. Consequences for phases

- **D2** (VST3, AUv2, standalone) depends on D1 (complete) and D-3. It stays blocked until the product name is chosen, because the class IDs, subtype, CLAP ID and LV2 URI contain it; the brand parts are settled.
- **D5 follow-up (code, implementer)**, in the post-merge window together with ICR-0002 and the relicensing pass: set `kDataDirName` to the two-level brand/product directory (the code must accept a two-component relative path on all three systems), `kPresetExtension = ".sympreset"`, `kPresetFormat = "sympathon-preset"`, and add the `product` field to preset files (write it; on load, reject a mismatching product with a clear error). The product component waits for the product name; until then the follow-up uses the brand part and a placeholder product component that D2 replaces.
- **D1 and D3** follow-ups replace `kPluginVendor`, `kPluginId` and `kLv2PluginUri` with the values above once the product name is chosen (no migration: nothing shipped).
- **H1-R6** trademark search before G5; **H3** website under the chosen domain.

## 6. What the owner still needs to answer

1. The product name for this engine: one of section 3, or another (we then run the same checks).
2. Whether to register sympathon.com (recommended) so the `com.sympathon` root and LV2 URIs apply; otherwise the fallback roots are used.
3. Whether a formal trademark search is wanted before release (recommended if the products will be sold or promoted).

## Sources (web searches and registry queries, 2026-10-12)

- Registry queries: `https://rdap.verisign.com/com/v1/domain/sympathon.com` (HTTP 404, not found); `https://rdap.publicinterestregistry.org/rdap/domain/sympathon.org` (404, object not found); the .io RDAP endpoint returned HTTP 429 (not checked).
- Web search "Sympathon": no product, company or site found (results were about the sympathetic nervous system, for example https://en.wikipedia.org/wiki/Sympathetic_nervous_system).
- AU manufacturer codes: Apple developer forum, "Cannot register a manufacturer code", https://developer.apple.com/forums/thread/38058 (no maintained public registry); JUCE forum, https://forum.juce.com/t/resolved-how-does-one-obtain-manufacturer-and-plugin-codes/24544.
- Searches for "Cordyne", "Tensora", "Aftersound", "Kordal" found no audio product of that name; *Chordophonet*: https://syntheway.net/Chordophonet_Virtual_Harp_And_Hammered_Dulcimer.htm.
- From the first proposal: SoundBytes Sympathizer https://www.kvraudio.com/product/sympathizer_by_soundbytes ; Impact Soundworks Plectra Series https://www.adorama.com/impact-soundworks-plectra-series-3-kazakh-dombra-virtual-instrument/p/is122447 ; Native Instruments Una Corda https://www.kvraudio.com/news/native-instruments-releases-una-corda-for-kontakt-player-32113.
