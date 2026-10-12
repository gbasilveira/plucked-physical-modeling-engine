# Licence analysis for the owner: MIT, GPL-3.0 or AGPL-3.0

Status: **analysis for decision D-1 (licence half); the choice is the owner's.** The repository stays MIT (the D-1 default) until the owner decides.
Written by: planner (Opus) · Date: 2026-10-12 · Related: D-1, D-8, H1 (H1-R1 licence ADR, H1-R4 notices, H1-R7 prior art, H1-R8 contribution policy)

This is a planning analysis, not legal advice. Before a public release, H1 should have a lawyer, or at least the FSF/OSI guidance cited below, confirm the conclusions that matter for the chosen licence.

## 1. Summary

- **All three licences are legally possible.** Every dependency we use or plan to use is compatible with MIT, GPL-3.0-or-later and AGPL-3.0-or-later for the way we use it (section 4). The VST 3 SDK, once the main obstacle to GPL-free or GPL plug-ins, has been MIT since version 3.8 (October 2025).
- **The difference is what other people may do with our code:**
  - MIT lets anyone, including commercial plug-in makers, take the engine into closed products.
  - GPL-3.0 requires anyone who distributes a modified or derived program to publish its source under the GPL.
  - AGPL-3.0 adds the same duty for people who let others use a modified version over a network, for example a rendering web service.
- **Switching later is one-sided.** Moving from MIT to GPL or AGPL is easy, because MIT allows relicensing. Moving from GPL or AGPL back to MIT needs the consent of every copyright holder. Today the owner is the only human author, so any switch is easiest now, before outside contributions arrive (section 6).
- **Recommendation (section 7):**
  - If the owner wants derivatives to stay open, choose **GPL-3.0-or-later**.
  - Choose **AGPL-3.0-or-later** only if network use matters to the owner. It adds little for desktop plug-ins and is compatible with everything we use.
  - Choose MIT if the aim is the widest possible reuse, including closed commercial products.
  - Decide before the first outside contribution, and in any case before G5.

## 2. What each licence means for this project

| Question | MIT (current) | GPL-3.0-or-later | AGPL-3.0-or-later |
| --- | --- | --- | --- |
| Can a musician use the plug-ins, commercially, in any DAW? | Yes | Yes (the GPL does not restrict running the program or the music it produces) | Yes |
| Can a closed-source DAW maker bundle our plug-in or engine in its product? | Yes, with the copyright notice | Only if the combined program is released under the GPL. Shipping our unmodified plug-in as a separate file next to their product is generally "mere aggregation" and allowed. | Same as GPL |
| Can a plug-in company embed our engine in a closed plug-in? | Yes | No, unless their plug-in is GPL | No, unless their plug-in is AGPL |
| Must someone who modifies and distributes it publish the changes? | No | Yes, complete corresponding source under GPL | Yes |
| Must someone who runs a modified version as a web service (for example "render your MIDI with this piano") publish their changes? | No | No | **Yes** (AGPL section 13) |
| Patent grant from contributors | None explicit | Explicit (GPL-3 section 11) | Explicit |
| Mac App Store distribution of the standalone app | Possible | Apple's App Store terms are widely considered incompatible with GPL (precedent: VLC was removed in 2011). Direct downloads are unaffected. | Same as GPL |
| Can the project itself sell copies or offer paid support? | Yes | Yes (the GPL allows selling; buyers get the GPL freedoms) | Yes |
| Can the owner later offer a separate commercial licence (dual licensing)? | Not needed | Only for code the owner holds copyright in, or where contributors agreed to it (CLA) | Same as GPL |

**Plug-ins and hosts.** A plug-in is loaded into the host's process. The FSF's view is that a plug-in and host exchanging complex data structures may form one combined work. The GPL, however, only governs distribution: a user who loads a GPL plug-in into a proprietary DAW on their own machine distributes nothing and breaks no rule. The obligations fall on whoever distributes binaries, which for us means our own release pipeline (H4), Linux distributions, and anyone who forks.

**AGPL in a desktop instrument.** Section 13 triggers only when a *modified* version is offered to users *over a network*. A desktop plug-in rarely is. AGPL therefore behaves like GPL-3 for nearly all users. It would matter if someone built an online rendering service from a modified engine, a realistic case given the offline render CLI and Python bindings (C8). The owner's reason ("built on public research; morally AGPL is adequate") is a legitimate basis for choosing it.

## 3. The owner's input on prior art (D-8), recorded as input only

The owner reports checking patents with another AI assistant (Gemini). The reply said the documentation is open and free and that some of it is GPL. **This is recorded as owner-provided input. It is not a prior-art or patent review, and D-8 is not answered by it.** Reasons:

- "Open and free documentation" and "GPL" are copyright statements about *texts and code*. Patents are a separate legal right, and an openly published paper can still describe a patented method. A patent review searches patent databases (claims, priority dates, jurisdictions, expiry) for the techniques the engine uses (modal synthesis with bridge coupling, SAV contact schemes, sympathetic-resonance approximations, longitudinal and phantom-partial models, jawari/sawari models). No such search is evidenced.
- AI-assistant answers are not verifiable sources. H1-R7 needs a review with citations (patent numbers, status, claim analysis) and a human sign-off.

**Residual risk:** unknown. Physical modelling of pianos and plucked strings is an active commercial field (for example Modartt Pianoteq, and the digital-piano divisions of Roland, Yamaha and Kawai). We make **no claim here that any specific patent exists or applies**. D-8 stays open and still blocks G5. H1-R7 plans the actual review (docs/dev/prior-art.md).

## 4. Dependency and SDK compatibility

"Compatible" means our code under that licence can be combined and distributed with the dependency in the way we use it.

| Component | Use | Licence | MIT | GPL-3.0+ | AGPL-3.0+ | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| CLAP SDK | plug-in API headers | MIT | yes | yes | yes | |
| clap-wrapper | VST3, AUv2, standalone builds | MIT | yes | yes | yes | It can download other SDKs, which keep their own licences (rows below). |
| VST 3 SDK (3.8 and later) | VST3 format | MIT (since 3.8, Oct 2025; before that proprietary or GPL-3) | yes | yes | yes | Pin at 3.8 or later (cmake/dependencies.cmake already says "MIT (3.8 and later)"). The VST trademark and logo have separate usage terms. |
| ASIO SDK | not used | GPL-3 or proprietary (since Oct 2025) | n/a | n/a | n/a | If a Windows standalone ever needs ASIO, under MIT we could only ship ASIO support in a GPL-licensed build. Not planned. |
| Apple AudioUnitSDK (through clap-wrapper) | AUv2 | Apache-2.0 (per the GitHub repository; H1 confirms the LICENSE file) | yes | yes (Apache-2.0 is GPL-3 compatible) | yes | |
| LV2 (and lilv, serd, sord in tests) | LV2 format | ISC (some files 0BSD, MIT) | yes | yes | yes | |
| Google Highway | SIMD | Apache-2.0 OR BSD-3-Clause | yes | yes | yes | We may pick BSD-3-Clause to avoid Apache's NOTICE duty (ADR-0001). |
| doctest | tests only | MIT | yes | yes | yes | Not distributed in binaries. |
| nlohmann/json | JSON | MIT | yes | yes | yes | |
| nanobind | Python bindings | BSD-3-Clause | yes | yes | yes | |
| NumPy, SciPy, jsonschema, meshio | compiler (Python) | BSD-3-Clause / MIT | yes | yes | yes | |
| scikit-fem | plate FEM (compiler) | BSD-3-Clause | yes | yes | yes | |
| JAX | fitting (compiler) | Apache-2.0 | yes | yes | yes | |
| **Gmsh** | meshing in the offline compiler only | **GPL-2.0-or-later** with a linking exception for named libraries | see note | yes | yes | Never linked into the engine or plug-ins (B2 handoff, H1). The `pme_compiler` Python package imports `gmsh` at run time. Our compiler *source* can stay MIT. A distributed bundle containing both the compiler and Gmsh (for example a frozen executable) must meet the GPL for the whole bundle. Under GPL-3/AGPL-3 this is automatic ("or later"). |
| Web UI in a native WebView (D-1 default) | GUI | Depends on the library chosen in D4. System WebViews (WKWebView, WebView2 runtime, WebKitGTK LGPL-2.1, dynamically linked) are used, not shipped. Candidate wrappers (webview/webview MIT; choc ISC) are permissive. | yes | yes | yes | D4 records the exact library and licence; H1 checks the WebView2 SDK loader's licence. |
| USDA FPL Wood Handbook data (FPL-GTR-282) | tonewood constants | US federal government work: public domain in the United States | yes | yes | yes | Cite the source (scholarly practice; H1-R5 provenance). Public-domain status outside the US is usually treated the same in practice; H1 records it. |
| Published papers (SPEC references) | ideas, formulas | copyright on the text only | n/a | n/a | n/a | See section 5. |

**Result:** no dependency forces a choice. MIT has one practical wrinkle, the Gmsh bundle. GPL-3/AGPL-3 has one practical wrinkle, the Mac App Store.

## 5. Does reading GPL papers, documentation or code affect our licence?

- **Ideas and formulas are not covered by copyright.** Implementing an algorithm described in a paper, a thesis or GPL-licensed documentation does not make our code a derivative of that text. The licence of a document governs copying *its text*; SPEC is written in our own words and cites its sources.
- **Code is covered.** Copying, translating or closely paraphrasing GPL source code into our files would make those files derivative works and bind them to the GPL. Our working rules already prevent this:
  - agents implement from SPEC, not from third-party code;
  - the P0.5 reference solver is written independently and shares no numerical code with the engine;
  - third-party code enters the repository only through pinned dependencies with a licence record (Software guidelines, "Languages and dependencies").
- **Recommendation for H1:** add one sentence to CONTRIBUTING.md (H1-R8) stating the clean-room rule: do not copy, translate or closely paraphrase code whose licence is incompatible with the project's; cite papers for algorithms.
- **Patents are separate** (section 3). Neither licence choice protects against a third-party patent. GPL-3 and AGPL-3 only bind *contributors'* patents.

## 6. Changing the licence later

- **Who holds copyright today:**
  - LICENSE says "Copyright (c) 2026 Guilherme Silveira".
  - The git history has one human author, the owner. The other 261 commits are authored by AI agents working for the owner.
  - The copyright status of AI-generated code is unsettled in several jurisdictions, but there is no other human claimant either way.
  - CONTRIBUTING.md has no contributor terms yet (no DCO or CLA). H1-R8 plans a DCO sign-off.
- **MIT to GPL or AGPL:**
  - The owner can relicense future versions at any time.
  - Code already released under MIT stays available under MIT to whoever received it.
  - Contributions received under MIT can be included in a GPL release, because MIT permits it, as long as their notices are kept.
- **GPL or AGPL to MIT:** needs the agreement of every copyright holder of the GPL-only code. A DCO alone does not give that agreement. A CLA, or the owner remaining the only contributor, does.
- **GPL-3 and AGPL-3 interconvert** only in the direction the "or later" and section-13 terms allow. GPL-3 and AGPL-3 code may be combined (each licence's section 13 permits it), and the combined work carries the AGPL network duty.
- **Practical consequence:**
  - Decide before accepting outside contributions.
  - If the owner wants to keep the option of dual licensing, adopt a CLA (or a DCO plus an explicit inbound licence grant) at the same time as the licence change.

## 7. Recommendation (for the owner to accept or change)

1. **Decide now, before G5 and before outside contributions.** The cost of switching only grows.
2. **If the goal is that improvements stay open:** choose **GPL-3.0-or-later** for the whole repository (engine, plug-ins, compiler, bindings). It protects the plug-in and app distribution model fully, is compatible with every dependency, and is the most common copyleft licence in open audio software.
3. **Choose AGPL-3.0-or-later instead** if closing the network-service gap matters to the owner, for example someone running a modified engine as a paid rendering service. It costs our users nothing extra.
4. **Stay MIT** if the goal is the widest adoption, including inside commercial closed products.
5. Whatever the choice:
   - H1 writes the licence ADR (H1-R1) and THIRD_PARTY_NOTICES (H1-R4);
   - the SPDX headers change in one mechanical commit (`tools/check_spdx.py` enforces them);
   - CONTRIBUTING.md gets the clean-room sentence and the contributor terms (H1-R8);
   - the README states the licence and the App Store limitation if copyleft is chosen.

**What happens until the owner decides:** D-1 stays at its default (MIT). No work is blocked: H1 needs the answer before G5. The SPDX change is mechanical whenever it comes.

## Sources

- Steinberg, VST 3.8 under MIT (press release 2025-10-29): https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf ; coverage: https://sonicstate.com/news/2025/10/30/vst-3-now-available-under-mit-license ; https://cdm.link/open-steinberg-vst3-and-asio/
- Gmsh licence and exception: https://spdx.org/licenses/Gmsh-exception.html
- Apple AudioUnitSDK repository: https://github.com/apple/AudioUnitSDK
- Licence texts and compatibility: https://www.gnu.org/licenses/gpl-3.0.html, https://www.gnu.org/licenses/agpl-3.0.html, https://www.gnu.org/licenses/license-list.html, https://www.gnu.org/licenses/gpl-faq.html (plug-ins, mere aggregation)
