// SPDX-License-Identifier: MIT
#pragma once

/// @file controller.hpp
/// Format-independent plug-in controller (D1 "Architecture"; D3 reuses it for LV2). It owns one
/// engine behind `EngineAdapter`, the parameter model, the event translator, the parameter queue,
/// the state conversion and the task runner. No CLAP types appear here.
///
/// SPEC sections used: SPEC-API "Real-time interface", "Events", "Parameter classes",
/// "Diagnostics"; SPEC-EXEC "Threading contract" and "Instrument lifetime".
///
/// Threads. Main thread: `activate`, `deactivate`, `applyState`, `captureState`, `mainThreadTick`,
/// `setChannels`, `setThreadsSetting`, `setHostPool`, `setRenderMode`, `setInstrumentReference`.
/// Audio thread: `beginBlock`, `events`, `paramEvent`, `flushParamEvent`, `render`, `reset`.
/// Any thread: the atomics-backed getters (`tailSamples`, `params().value`).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pme/plugin/engine_adapter.hpp"
#include "pme/plugin/event_translator.hpp"
#include "pme/plugin/param_model.hpp"
#include "pme/plugin/spsc_queue.hpp"
#include "pme/plugin/state.hpp"
#include "pme/plugin/task_runner.hpp"

namespace pme::plugin {

/// Host render mode (D1-R7). Recorded only; the engine receives the same events, offsets and block
/// lengths in both modes.
enum class RenderMode : std::uint8_t { Realtime, Offline };

class Controller {
public:
    static constexpr int kMaxChannels = 8;
    /// Parameter changes from the main thread to the audio thread, queued before they are sent.
    static constexpr std::size_t kQueueCapacity = 1024;

    /// Non-audio thread. Allocates.
    explicit Controller(std::unique_ptr<EngineAdapter> engine);

    // ---- main thread ------------------------------------------------------------------------

    /// Prepare the engine (D1 "Stub engine"). `Ok` also when the engine reports `NotImplemented`
    /// (engine unavailable: `process` then outputs silence); `InvalidArgument` is an activation
    /// failure and leaves the controller inactive.
    [[nodiscard]] Status activate(double sampleRate, std::uint32_t maxFrames);
    void deactivate() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool engineAvailable() const noexcept { return engineReady_; }

    /// Number of output channels used by the next `activate` (1..kMaxChannels; others rejected).
    bool setChannels(int channels) noexcept;
    [[nodiscard]] int channels() const noexcept { return channels_; }

    /// `EngineConfig::threads` used when the host has no thread pool (D1-R6).
    void setThreadsSetting(int threads) noexcept;
    [[nodiscard]] int threadsSetting() const noexcept { return threadsSetting_; }
    /// The host's thread pool, or nullptr.
    void setHostPool(TaskRunner::HostPool* pool) noexcept { tasks_.setHostPool(pool); }

    void setRenderMode(RenderMode mode) noexcept { renderMode_.store(mode); }
    [[nodiscard]] RenderMode renderMode() const noexcept { return renderMode_.load(); }

    /// Instrument reference stored in the state (path or id; empty until B1 provides loading).
    void setInstrumentReference(std::string reference) { instrument_ = std::move(reference); }
    [[nodiscard]] const std::string& instrumentReference() const noexcept { return instrument_; }

    /// Values that differ from the registry default, plus version fields (D1-R4).
    [[nodiscard]] StateData captureState() const;
    struct ApplyReport {
        std::size_t applied{0};
        std::size_t unknown{0};  ///< Ids not in the registry; ignored.
    };
    /// Make the model equal to `state`: listed values are clamped and stored, every other
    /// parameter returns to its default. Changes reach the audio thread through the queue.
    ApplyReport applyState(const StateData& state);

    /// Release retired instruments and drain diagnostics (ICR-0001: main thread or a host timer).
    void mainThreadTick() noexcept;
    [[nodiscard]] std::uint64_t diagnosticCount() const noexcept { return diagnostics_; }

    /// Tail: `UINT32_MAX` (infinite) while any string sounds, else 0 (D1-R5). With the stub
    /// engine, "sounds" means a note is held or the sustain pedal value is > 0; D6 replaces this
    /// with the engine's group-energy diagnostics.
    [[nodiscard]] std::uint32_t tailSamples() const noexcept;
    /// Voice count and capacity: one per key (D1-R5).
    [[nodiscard]] static std::uint32_t voiceCount() noexcept { return 128; }

    [[nodiscard]] ParamModel& params() noexcept { return params_; }
    [[nodiscard]] const ParamModel& params() const noexcept { return params_; }
    [[nodiscard]] TaskRunner& taskRunner() noexcept { return tasks_; }

    // ---- audio thread -----------------------------------------------------------------------

    /// Start a host block: clear the event buffer and enqueue the pending parameter changes at
    /// offset 0.
    void beginBlock(std::uint32_t frames) noexcept;
    [[nodiscard]] EventTranslator& events() noexcept { return translator_; }
    /// Host parameter value at `offset`: clamped, stored in the model, delivered as `Param`.
    /// Unknown ids are ignored.
    void paramEvent(std::uint32_t offset, ParamId id, double value) noexcept;
    /// Host parameter value outside a block (CLAP `flush`): stored now, delivered at offset 0 of
    /// the next block.
    void flushParamEvent(ParamId id, double value) noexcept;
    /// Sort the block's events and render `frames` samples into `hostChannels`. Blocks larger than
    /// the activated maximum are rendered in chunks with re-based offsets. Never allocates.
    void render(std::uint32_t frames, std::span<float* const> hostChannels) noexcept;
    /// Silence all voices (CLAP `reset`).
    void reset() noexcept;

private:
    struct ParamChange {
        ParamId id{};
        double value{};
    };
    void track(const Event& e) noexcept;
    void renderChunk(std::span<const Event> events, std::uint32_t start, std::uint32_t frames,
                     std::span<float* const> hostChannels) noexcept;

    std::unique_ptr<EngineAdapter> engine_;
    ParamModel params_;
    EventTranslator translator_;
    SpscQueue<ParamChange, kQueueCapacity> queue_;
    TaskRunner tasks_;

    // main-thread state
    bool active_{false};
    bool engineReady_{false};
    int channels_{2};
    int threadsSetting_{TaskRunner::defaultThreads()};
    std::uint32_t maxBlock_{0};
    std::string instrument_;
    std::uint64_t diagnostics_{0};
    std::atomic<RenderMode> renderMode_{RenderMode::Realtime};

    // audio-thread state
    std::array<ParamChange, 256> carry_{};
    std::size_t carryCount_{0};
    std::array<float*, kMaxChannels> pointers_{};
    std::vector<float> scratch_;
    std::array<bool, 128> held_{};
    int heldCount_{0};
    float sustain_{0.0F};
    std::atomic<bool> sounding_{false};
};

}  // namespace pme::plugin
