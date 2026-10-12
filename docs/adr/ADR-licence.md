# ADR-licence: the project is licensed AGPL-3.0-or-later

- **Status:** Accepted (owner decision D-1, licence half, 2026-10-12); takes effect in the repository when the relicensing task H1-agpl lands
- **Date:** 2026-10-12
- **Deciders:** the human owner (decision D-1, relayed by the orchestrator); recorded by the planner (Opus) as H1-R1's licence record
- **Phase:** H1 (H1-R1); scheduled implementation H1-agpl
- **References:** docs/plan/decisions/D-1-licence-analysis.md; phasing plan, Software guidelines "Licensing", Decisions D-1 and D-8, card H1

## Decision

Every file the project authors (engine, compiler, plug-ins, bindings, tools, tests, build scripts) is licensed **AGPL-3.0-or-later** and carries `SPDX-License-Identifier: AGPL-3.0-or-later`. LICENSE holds the verbatim GNU AGPL version 3 text from the FSF. Third-party and vendored files keep their own licences.

## Context

The owner chose a copyleft licence that also covers network use: the engine is built on public research, and modifications offered to users, including as a rendering service, should stay open. The analysis (docs/plan/decisions/D-1-licence-analysis.md, sections 2 and 4) found every dependency compatible with AGPL-3.0-or-later as used: CLAP, clap-wrapper, VST 3 SDK 3.8 or later (MIT), AudioUnitSDK (Apache-2.0), LV2 (ISC), Highway, nlohmann/json, nanobind, NumPy, SciPy, scikit-fem, JAX (permissive), and Gmsh (GPL-2.0-or-later, used only by the offline compiler, compatible through GPL-3.0). Until now the repository was MIT (the D-1 default).

## Options considered

| Option | For | Against |
| --- | --- | --- |
| AGPL-3.0-or-later (chosen) | Derivatives stay open, including network services built from the offline renderer and Python bindings; explicit patent grant from contributors; compatible with all dependencies | No Mac App Store distribution; closed-source companies cannot embed the engine; returning to a permissive licence later needs every copyright holder's consent |
| GPL-3.0-or-later | Same as AGPL for distributed copies; more familiar | Leaves the network-service case open, which the owner wants closed |
| MIT (previous default) | Widest reuse | Allows closed derivatives, which the owner does not want |

## Consequences

- **H1-agpl** (implementer, Sonnet; brief docs/agents/briefs/H1-agpl-implementer.md) does the mechanical change after A5, A7, A8 and A9 merge: LICENSE text, SPDX headers, `tools/check_spdx.py`, package and plug-in metadata, README and user documentation, the section-13 note. Until then new files may carry the MIT header; the bulk pass converts them.
- Code already published under MIT stays available under MIT to those who received it.
- The standalone app is not offered through the Mac App Store (accepted by the owner); direct notarised downloads (D-9) are unaffected.
- Anyone running a modified version as a network service must offer its users the source (AGPL section 13); README and the user documentation say so.
- H1-R4 THIRD\_PARTY\_NOTICES and H1-R3 SBOM list each dependency's own licence; a bundle that ships Gmsh (for example a frozen compiler executable) is distributed under terms compatible with GPL-3.0 for the whole bundle, which AGPL-3.0-or-later satisfies.
- H1-R8 (DCO, clean-room sentence) lands before the first outside contribution. A CLA is needed only if the owner wants to keep dual licensing possible.
- D-8 (prior-art and patent review) is unaffected and still blocks G5.

## Verification

- `tools/check_spdx.py` (H1-T2) requires `SPDX-License-Identifier: AGPL-3.0-or-later` on every project file after H1-agpl, and each vendored file's own licence text.
- LICENSE is byte-identical to https://www.gnu.org/licenses/agpl-3.0.txt.
