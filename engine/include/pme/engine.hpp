// SPDX-License-Identifier: MIT
#pragma once

/// @file engine.hpp
/// Engine configuration and the real-time interface (SPEC-API "Real-time interface";
/// SPEC-EXEC; ADR-0006).
///
/// Threading contract (SPEC-EXEC): `prepare`, `setInstrument`, `collectRetired` and
/// `popDiagnostic` run on non-audio threads, one at a time (the caller serialises them);
/// `process` and `reset` only on the audio thread. The audio thread never touches a reference
/// count (SPEC-EXEC "Instrument lifetime"). Nothing reachable from `process` allocates, locks,
/// throws, does I/O or makes system calls.

#include <memory>
#include <span>

#include "pme/diagnostics.hpp"
#include "pme/events.hpp"
#include "pme/instrument.hpp"
#include "pme/status.hpp"

namespace pme {

/// Engine configuration (SPEC-API `EngineConfig`). Value-initialised: an all-zero config is
/// invalid and `Engine::prepare` rejects it.
struct EngineConfig {
    double fs{};           ///< Sample rate fs in Hz; must be > 0 (SPEC-CONV).
    int maxBlock{};        ///< Largest host block B in samples; must be >= 1.
    int subBlock{};        ///< Sub-block length S in samples; must be 16 or 32 (SPEC-CONV).
    int channels{};        ///< Number of output channels; must be >= 1.
    int threads{};         ///< Audio worker threads, including the audio thread; must be >= 1.
    double fMaxStrings{};  ///< Highest kept string frequency f_max in Hz (SPEC-INST: 12 to 20 kHz,
                           ///< default 16 kHz). Authoritative at `prepare`; a `strings.fMax`
                           ///< Param event recompiles the instrument and updates this copy
                           ///< (SPEC-API "Parameter classes", SI-0007).
    bool referenceMode{};  ///< Offline reference mode: every bridge point couples to all modes
                           ///< every sample on one thread (SPEC-BODY "Reference mode").
};

/// Output buffers of one `process` call (SPEC-API `AudioOut`). Provisional layout: SPEC-API
/// names the type only. Non-interleaved float32; the buffers are owned by the host.
struct AudioOut {
    std::span<float* const>
        channels;  ///< One pointer per output channel, each to >= frames floats.
};

/// The synthesis engine (SPEC-API `Engine`). Holds its state behind a private pointer; the
/// state is created in the constructor and in `prepare`, never in `process`. Copy is deleted,
/// move is allowed.
class Engine {
public:
    /// Create an unprepared engine. Thread: non-audio. Allocates: yes. May throw `std::bad_alloc`.
    Engine();
    ~Engine();  ///< Releases the current, pending and retired instruments. Thread: non-audio.
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    /// Configure the engine and allocate everything `process` needs (SPEC-API).
    /// Thread: non-audio; must not run concurrently with `process`. Allocates: yes (including
    /// the 4-entry retire queue). Drains the retire queue first (SPEC-EXEC "Instrument lifetime").
    /// Failures: `InvalidArgument` if fs <= 0, subBlock is not 16 or 32, maxBlock < 1,
    /// channels < 1 or threads < 1; `NotImplemented` in the P0.4 stub (after a valid check).
    [[nodiscard]] Status prepare(const EngineConfig& config);

    /// Offer a compiled instrument; the audio thread swaps it in with one atomic exchange at the
    /// start of the next block (SPEC-EXEC step 1, "Instrument lifetime"). The engine keeps the
    /// shared_ptr; the audio thread sees only the raw pointer. A null argument removes the
    /// instrument at the next block (silence). Drains the retire queue first; if a previously
    /// offered instrument was not yet taken, it is retired by this call.
    /// Thread: non-audio only. Allocates: no; may free retired instruments. Never throws.
    /// The P0.4 stub accepts and releases the argument on the calling thread and never swaps.
    void setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept;

    /// Release every instrument the audio thread has retired (SPEC-EXEC "Instrument lifetime").
    /// Call it periodically from a non-audio thread (plug-in shells: from the main or a timer
    /// thread). Thread: non-audio only. Allocates: no; frees retired instruments. Lock-free.
    /// The P0.4 stub does nothing.
    void collectRetired() noexcept;

    /// Render `frames` samples (SPEC-API, SPEC-EXEC). `events` must be sorted by offset and every
    /// offset must lie in [0, frames). Writes exactly `frames` samples to every channel of `out`.
    /// Thread: audio only. Allocates: no. Never throws. The P0.4 stub writes exact zeros, even
    /// before a successful `prepare`.
    void process(const EventList& events, AudioOut& out, int frames) noexcept;

    /// Silence all voices and reset states (SPEC-API). Thread: audio only. Allocates: no.
    void reset() noexcept;

    /// Take the oldest pending diagnostic (SPEC-API "Diagnostics"). Lock-free. Also drains the
    /// retire queue (SPEC-EXEC "Instrument lifetime").
    /// Thread: non-audio. Allocates: no; may free retired instruments. Returns false and leaves `d` unchanged if none is pending.
    bool popDiagnostic(Diagnostic& d) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pme
