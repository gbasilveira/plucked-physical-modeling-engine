# ADR-0005: nlohmann/json for JSON in C++ tests and tools

- **Status:** Accepted (2026-10-11) — chosen by the planner while briefing P0.3 and accepted by the orchestrator as the recommended default under the owner's instruction to proceed; the owner may reverse it.
- **Date:** 2026-10-11
- **Deciders:** the planner (Opus), under the Software guidelines rule "Every decision not fixed by SPEC or PLAN gets an ADR"
- **Phase:** P0.3 (P0.3-R4, P0.3-R6)
- **References:** phase cards P0.3 (golden-feature JSON, benchmark JSON output) and B1 (non-real-time C++ loader of JSON instrument files); PLAN-STACK (Instrument files row: "Versioned JSON for physical parameters"); ADR-0003

## Decision

C++ code that reads or writes JSON uses **nlohmann/json** (MIT), pinned by CPM.cmake at the peeled commit of its latest release (ADR-0003) and added as a `SYSTEM` dependency. It is linked only into test, tool and non-real-time targets; it is never linked into a target reachable from `Engine::process`.

## Context

- P0.3-R4 requires a JSON golden-feature format and a comparator; golden-feature tests run in CTest on every pull request (Testing policy), so the comparator must be C++. P0.3-R6 requires JSON benchmark output.
- B1-R4 later needs a non-real-time C++ loader for JSON instrument files. One JSON library across tests, tools and the loader avoids two parsers.
- No JSON library is pinned in P0.1 (ADR-0003 lists Highway, doctest, nanobind, CLAP, clap-wrapper, VST 3 SDK, LV2). A new dependency needs a pin and a licence record.
- Real-time rule 1 forbids allocation and exceptions on the audio path; JSON parsing allocates, so it can never run there regardless of library.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **nlohmann/json** (chosen) | MIT, header-only, the most widely used C++ JSON library; readable code; exact round-trip of doubles. | Slower and more allocation-heavy than alternatives; irrelevant for files of kilobytes to a few megabytes read off the audio thread. |
| simdjson | Fastest parser; Apache-2.0. | Read-only (writing needs a second library); more complex API. |
| Glaze | Fast, reflection-based read and write; MIT. | Requires newer compilers than our C++20 baseline guarantees on all three systems; heavier templates. |
| Hand-written minimal parser | No dependency. | Untested code in the verification path; every phase that touches JSON would extend it. |

## Consequences

- P0.3 adds the pin to `cmake/dependencies.cmake` (upstream https://github.com/nlohmann/json, latest release tag, peeled commit from `git ls-remote`, SPDX `MIT`) and one row to the dependency table in docs/dev/build.md. It is fetched in every preset (tests use it).
- B1 uses the same library for its loader unless it files a new ADR.
- `pme_engine` and its module libraries do not link it. P0.3 adds a configure-time check that fails if any `pme_<module>` target links `nlohmann_json` or the analysis/WAV library.
- H1's software bill of materials lists it.

## Verification

P0.3 code review: the pin is a peeled 40-hex commit that `git ls-remote` reproduces; the configure-time link check exists and fails when a module links the library in a scratch build.

## Revisiting

Reopen if JSON parsing time becomes measurable in instrument loading (B1, D5) or a compiler on the matrix cannot build the pinned release.
