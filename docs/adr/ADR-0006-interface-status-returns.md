# ADR-0006: Engine interface reports failure through a Status return

- **Status:** Accepted (2026-10-11) — chosen by the planner while briefing P0.4 and accepted by the orchestrator as the recommended default under the owner's instruction to proceed; the owner may reverse it.
- **Date:** 2026-10-11
- **Deciders:** the planner (Opus), under the Software guidelines rule "Every decision not fixed by SPEC or PLAN gets an ADR"
- **Phase:** P0.4 (P0.4-R1, P0.4-R3)
- **References:** SPEC-API ("Declarations are C++-style; their semantics are the contract"); SPEC-OSC Contract ("It MUST fail if the bandwidth rule rejects the mode"); phase card P0.4 (P0.4-R3 "Stubs compile and return a NotImplemented status"); Software guidelines (Real-time rules 1; Code style "Public headers contain declarations and documentation only")

## Decision

In `engine/include/pme/`, every SPEC-API function that runs off the audio thread and can fail returns `[[nodiscard]] pme::Status` and delivers its result through a final reference out-parameter whose type is SPEC-API's return type. Real-time functions keep SPEC-API's signatures, are `noexcept`, and never fail. No public function throws, except that allocation failure in a non-real-time function may propagate as `std::bad_alloc`.

```cpp
namespace pme {
enum class Status : std::uint8_t { Ok, NotImplemented, InvalidArgument, Rejected };
[[nodiscard]] const char* toString(Status s) noexcept;

[[nodiscard]] Status compileOscillator(double omega, double sigma, double fs, OscCoeffs& out);
[[nodiscard]] Status compileInstrument(const InstrumentDesc&, const EngineConfig&, const BodyModes& body,
                                       std::shared_ptr<const CompiledInstrument>& out);

class Engine {
public:
  [[nodiscard]] Status prepare(const EngineConfig&);                         // non-audio thread
  void setInstrument(std::shared_ptr<const CompiledInstrument>) noexcept;    // any thread
  void process(const EventList& events, AudioOut& out, int frames) noexcept; // audio thread only
  void reset() noexcept;                                                     // audio thread only
  bool popDiagnostic(Diagnostic& d) noexcept;                                // non-audio thread
};
}
```

`Rejected` means a SPEC rule refused the input (for example SPEC-OSC's bandwidth rule). New codes are added only by ICR. On any status other than `Ok`, the out-parameter is left unchanged.

## Context

- SPEC-API writes compile functions as returning values (`OscCoeffs compileOscillator(…)`), but SPEC-OSC says the same function "MUST fail" when the bandwidth rule rejects a mode, and P0.4-R3 asks stubs to "return a NotImplemented status". A value return has no failure channel, so the C++ rendering must add one. SPEC-API states that its declarations are C++-style and that the semantics are the contract; the rendering changes form, not semantics.
- C++20 has no `std::expected` (C++23). A project `Result<T>` template would put function bodies into public headers, against the Code style rule that public headers contain declarations and documentation only.
- Exceptions are banned on the audio path (Real-time rule 1). Using them for compile-time failure would make error handling differ between the two faces of the interface and between test code and plug-in code.
- `Engine::prepare` runs on a non-audio thread and receives a configuration that can be invalid (for example a sub-block other than 16 or 32, SPEC-CONV), so it needs a failure channel too.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **`Status` return plus out-parameter** (chosen) | Declarations only; one error model for every non-real-time function; `[[nodiscard]]` forces callers to look; trivially ABI-neutral. | Out-parameters are less idiomatic than value returns; result types need default construction. |
| Project `Result<T>` (expected-like template) | Value semantics; closer to SPEC's notation. | Template bodies in public headers; one more vocabulary type to maintain until C++23. |
| Exceptions from compile functions | Keeps SPEC's signatures literally. | Two error models; exceptions banned on the audio path; plug-in shells must catch at every boundary. |
| Keep SPEC signatures, signal failure inside the returned value (`OscCoeffs::valid`) | No signature change. | Every result type carries its own error flag; easy to ignore; cannot express `NotImplemented` uniformly. |

## Consequences

- P0.4 declares `Status`, `toString` and the signatures above; every compile-function stub returns `Status::NotImplemented`. Every result type in the public headers is default-constructible.
- Phases A1–A7, B3, C1–C8 implement these signatures and return `Rejected` or `InvalidArgument` where SPEC says a function fails. Tests check the status before the value.
- SPEC-API is not edited: its notation stays the semantic contract. If the owner wants SPEC's text to show the rendering, the planner files a spec issue; no behaviour changes either way.

## Verification

P0.4-T1 compiles every header on its own; the P0.4 code reviewer checks that every non-real-time SPEC-API function returns `[[nodiscard]] Status`, every real-time one is `noexcept` with SPEC's signature, and that no public header contains a function body other than `= default` or `= delete`.

## Revisiting

Reopen when the project moves to C++23 (`std::expected`), through an ICR that bumps the interfaces tag.
