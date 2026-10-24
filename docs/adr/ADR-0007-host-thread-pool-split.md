# ADR-0007: CLAP host thread pool — shell side in D1, engine routing in D6

- **Status:** Accepted (2026-10-11; accepted by the orchestrator as the recommended default under the owner's proceed instruction; the owner may reverse it)
- **Date:** 2026-10-11
- **Deciders:** the planner (Opus), while briefing D1, under the Software guidelines rule "Every decision not fixed by SPEC or PLAN gets an ADR" and the definition of done ("explicitly deferred by an approved ADR")
- **Phase:** D1 (D1-R6)
- **References:** phase cards D1 (D1-R6), C5 (C5-R5, C5-T3), D6; SPEC-EXEC "Threading contract"; SPEC-API "Real-time interface"; PLAN-ARCH ("Worker pool or CLAP host threads"); ADR-0006

## Decision

D1 implements the shell half of D1-R6: it detects the host's `clap.thread-pool` extension, implements the plug-in's `clap_plugin_thread_pool.exec` callback, and wraps both in a shell-side task-runner interface in `plugin/common/` with a mock-host test; with no host pool it selects the engine's own pool through `EngineConfig::threads`. The other half, routing the engine's per-sub-block parallel work through that runner ("routed to the C5 host-pool adapter"), is deferred to D6, after C5-R5 has defined the engine-side adapter and an ICR has added it to the public interface.

## Context

- D1-R6: "Thread-pool extension routed to the C5 host-pool adapter; fallback to the engine's own pool." D1 depends only on G0; C5 depends on C1 and C2 and has not started.
- C5-R5: "Host thread-pool adapter interface, used by the CLAP thread-pool extension in D1." C5-T3: "the host-pool adapter runs all tasks correctly with a mock host."
- The public interface at interfaces-v2 (`engine/include/pme/engine.hpp`) has no entry point through which a shell can hand the engine an external task runner: `Engine` takes `EngineConfig::threads` only. Adding one is an interface change (ICR), which belongs with C5, the phase that knows what the engine's tasks look like.
- D1 runs against the stub engine, which has no parallel work to route.

## Options considered

| Option | For | Against |
| --- | --- | --- |
| **Split: shell side in D1, routing in D6** (chosen) | D1 can finish and be tested now; the CLAP side is verified with a mock host; C5 designs the engine side with full knowledge; D6 already integrates the real engine. | D1-R6 is not fully met at D1's completion; one requirement spans two phases. |
| D1 defines the engine-side adapter now (ICR from D1) | D1-R6 met in one phase. | Freezes an engine scheduling interface before C1, C2 and C5 exist; likely to need a second ICR. |
| Block D1 on C5 | No split. | D1 would wait for most of Track C, against the card's "starts at G0 against the stub engine, so the shell is ready when the engine is". |

## Consequences

- D1 reports D1-R6 as `deferred` (engine routing) with this ADR, and lists the shell-side part as delivered in `notes`. The shell's task-runner interface is documented in docs/dev/clap-shell.md so that C5-R5 can implement the engine side against it.
- C5's brief must reference this ADR: C5-R5 defines the engine-side adapter and raises the ICR that exposes it; C5-T3 uses a mock host.
- The D6 card's description records the inherited routing clause. D6's brief must include connecting the runner to the engine and a test that the CLAP host pool executes the engine's tasks with bit-identical output for any thread count (SPEC-EXEC).
- G3 requires D1–D6 complete, so the deferral is closed before G3.

## Verification

D1: a test with a mock `clap_host_thread_pool` shows `exec` runs every task index exactly once and that the fallback path sets `EngineConfig::threads` when the host has no pool. D6: the routing test above. The V1 matrix shows D1-R6 as deferred until D6 closes it.

## Revisiting

If C5 finds that the engine needs no external runner (for example, it decides to always use its own pool and ignore host threads), a new ADR supersedes this one and D1-R6's routing clause is closed by the planner with the orchestrator's approval.
