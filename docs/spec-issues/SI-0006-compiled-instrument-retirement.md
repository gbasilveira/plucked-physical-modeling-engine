# SI-0006 No retire mechanism for the previous CompiledInstrument

Status: resolved · Priority: must be resolved before C1, C4 and C6 start · Raised by: P0.4, code-reviewer · Date: 2026-10-11
SPEC section: SPEC-EXEC "Host block" step 1, "Threading contract"; SPEC-API "Real-time interface" (`setInstrument`)

## Failing test
None yet. C1-T3 and C4-T3 ("1,000 random structural edits while playing cause no RT-safety violation") will fail under the rt-safety build if the design follows SPEC as written.

## Evidence
- SPEC-EXEC step 1 swaps a waiting instrument "with one atomic pointer exchange" at block start. SPEC-API declares `setInstrument(std::shared_ptr<const CompiledInstrument>)` callable from any thread.
- Real-time rule 4 of the phasing plan: "the old one is freed on a non-audio thread". SPEC names no thread, call or queue that performs that free.
- If the audio thread drops the last `shared_ptr` reference to the outgoing instrument, its destructor and every deallocation run on the audio thread. That breaks SPEC-EXEC ("Audio threads MUST NOT allocate ...") and real-time rule 1.
- `std::atomic<std::shared_ptr<T>>` is not lock-free in libstdc++ or MSVC, so it cannot carry the handoff either.
- `setInstrument` "from any thread" lets the audio thread release a replaced pending instrument too.
- P0.4's stub "accepts and releases its argument", which is safe only because it is called off the audio thread in tests.

## Proposed correction (SPEC-EXEC, new paragraph "Instrument lifetime")
1. `setInstrument` runs on a non-audio thread. The engine keeps every instrument it was given in a list owned by non-audio threads; the audio thread sees only raw pointers.
2. Handoff: `setInstrument` publishes the new raw pointer in an atomic `pending` slot. At block start the audio thread exchanges `pending` with `current` and pushes the outgoing raw pointer into a lock-free single-producer single-consumer retire queue. It never touches a reference count.
3. Reclamation: every non-audio call into the engine (`setInstrument`, `popDiagnostic`, `prepare`, and a new `collectRetired()`) drains the retire queue and drops the engine's `shared_ptr` for each retired pointer. Engine destruction releases everything. If a second `setInstrument` arrives before the audio thread took the first, the first is retired directly by the calling thread.
4. The retire queue is allocated in `prepare` with 4 entries. It cannot overflow: the audio thread pushes at most one pointer per block, and only after a `setInstrument` call, which drains the queue first.

This is an execution-contract change, not physics or numerics; no human reviewer is needed.

## Interface impact
ICR, breaking under the interfaces-v1 compatibility policy (a threading guarantee changes): `setInstrument` becomes "non-audio thread only" (signature unchanged); new `void Engine::collectRetired() noexcept` (non-audio). Both land in interfaces-v2. Batch with SI-0005's ICR if both are accepted, so there is only one major bump.

## Recommended resolution
Accept as proposed. Planner updates SPEC-EXEC and SPEC-API; one interface agent applies the ICR and tags interfaces-v2 before C1 starts. Until then C1, C4 and C6 must not be briefed.

## Resolution
Accepted by the orchestrator on 2026-10-11 as proposed: `setInstrument` becomes non-audio-thread only, plus `Engine::collectRetired()`. Execution-contract change; no human reviewer needed.

- SPEC-EXEC "Host block" step 1 and new "Instrument lifetime" paragraph; SPEC-API "Real-time interface".
- SPEC commit: "PLAN: ICR-0001 interfaces-v2 and SPEC for SI-0003 to SI-0007".
- Route: [ICR-0001](../icr/ICR-0001-interfaces-v2.md) (interfaces-v2). C1, C4 and C6 are briefed only after interfaces-v2 is tagged. Status becomes resolved then.

Affected phases: C1, C4, C6, C8, D1, D6.

Resolved on 2026-10-11: ICR-0001 applied at 300b995 (interfaces-v2; tag created locally, push pending the owner). Code reviewer approved; pr.yml run 38149794842 green on 12/12 jobs.
