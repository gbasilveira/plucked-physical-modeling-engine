// SPDX-License-Identifier: MIT
#pragma once

/// @file lv2_shell.hpp
/// The LV2 adapter (D3-R1, D3-R2): translates LV2 structures to and from the format-independent
/// controller layer (plugin/common). It contains no event-translation, clamping or state logic
/// of its own.
///
/// SPEC sections used: SPEC-API "Events", "Parameter classes"; SPEC-EXEC "Threading contract",
/// "Instrument lifetime".
///
/// Threads (LV2 threading classes plus SPEC-EXEC). Non-audio: construction, `activate`,
/// `deactivate`, destruction, `save`, `restore`, `work`. Audio: `run`, `workResponse`. Every call
/// into the controller's main-thread API happens with `mutex_` held, and only non-audio threads
/// take that mutex: `run` and `workResponse` never lock.

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/log/log.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <vector>

#include "pme/plugin/controller.hpp"
#include "pme/plugin/engine_adapter.hpp"
#include "ports.hpp"

namespace pme::lv2 {

/// Longest instrument path that travels in a worker job, including the terminating NUL.
inline constexpr std::size_t kMaxPath = 1024;
/// Block length assumed when the host gives no `buf-size:maxBlockLength`. `Controller::render`
/// splits larger host blocks, so the value only sizes the engine's buffers.
inline constexpr std::uint32_t kDefaultMaxBlock = 4096;
/// A worker tick is scheduled about every 100 ms of rendered frames.
inline constexpr double kTickSeconds = 0.1;

/// What the worker thread is asked to do.
enum class JobKind : std::uint32_t { Instrument = 1, Tick = 2 };

/// Worker job: fixed-size, trivially copyable, no pointers (the host copies it to the worker).
struct Job {
    std::uint32_t kind{0};
    std::uint32_t length{0};  ///< Bytes of `path` used (instrument jobs).
    std::array<char, kMaxPath> path{};
};
static_assert(std::is_trivially_copyable_v<Job>);

/// What the worker answers (delivered to `workResponse` on the audio thread).
struct JobResult {
    std::uint32_t kind{0};
    std::uint32_t length{0};
    std::array<char, kMaxPath> path{};
};
static_assert(std::is_trivially_copyable_v<JobResult>);

/// The URIDs the adapter uses.
struct Urids {
    LV2_URID atomSequence{};
    LV2_URID atomObject{};
    LV2_URID atomUrid{};
    LV2_URID atomPath{};
    LV2_URID atomString{};
    LV2_URID atomDouble{};
    LV2_URID atomFloat{};
    LV2_URID atomInt{};
    LV2_URID atomLong{};
    LV2_URID atomFrameTime{};
    LV2_URID midiEvent{};
    LV2_URID patchSet{};
    LV2_URID patchGet{};
    LV2_URID patchProperty{};
    LV2_URID patchValue{};
    LV2_URID instrument{};
    LV2_URID stateKey{};
    LV2_URID maxBlockLength{};
    LV2_URID logNote{};
    LV2_URID logError{};
};

class Plugin {
public:
    /// Non-audio thread. Allocates. `map` is required. `schedule` and `log` may be null.
    /// `maxBlock` 0 means "not given by the host" (kDefaultMaxBlock).
    Plugin(std::unique_ptr<plugin::EngineAdapter> engine, double sampleRate,
           LV2_URID_Map* map, const LV2_Worker_Schedule* schedule, const LV2_Log_Log* log,
           std::uint32_t maxBlock);

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    Plugin(Plugin&&) = delete;
    Plugin& operator=(Plugin&&) = delete;
    ~Plugin() = default;

    /// Any thread before `run` (the host connects ports while the plug-in is not running).
    void connectPort(std::uint32_t port, void* data) noexcept;

    /// Non-audio. False when the controller refuses (`InvalidArgument`); `NotImplemented` from
    /// the stub engine means "engine unavailable": activation succeeds and `run` writes silence.
    [[nodiscard]] bool activate();
    void deactivate() noexcept;

    /// Audio thread. Allocates: no. Locks: no. Throws: no.
    void run(std::uint32_t frames) noexcept;

    /// Non-audio (LV2 state extension).
    [[nodiscard]] LV2_State_Status save(LV2_State_Store_Function store, LV2_State_Handle handle,
                                        std::uint32_t flags) noexcept;
    [[nodiscard]] LV2_State_Status restore(LV2_State_Retrieve_Function retrieve,
                                           LV2_State_Handle handle) noexcept;

    /// Worker thread (LV2 worker extension).
    [[nodiscard]] LV2_Worker_Status work(LV2_Worker_Respond_Function respond,
                                         LV2_Worker_Respond_Handle handle, std::uint32_t size,
                                         const void* data) noexcept;
    /// Audio thread, before `run` of the same cycle.
    [[nodiscard]] LV2_Worker_Status workResponse(std::uint32_t size, const void* data) noexcept;

    /// The controller (tests and diagnostics; main-thread API only under the rules above).
    [[nodiscard]] plugin::Controller& controller() noexcept { return controller_; }
    /// Instrument changes ignored because the host gave no `work:schedule`, or the path was too
    /// long, or the job could not be queued.
    [[nodiscard]] std::uint64_t droppedInstrumentJobs() const noexcept {
        return droppedInstrument_.load();
    }
    [[nodiscard]] const Urids& urids() const noexcept { return urids_; }
    [[nodiscard]] std::uint32_t maxBlock() const noexcept { return maxBlock_; }
    [[nodiscard]] bool hasWorker() const noexcept { return schedule_ != nullptr; }

private:
    struct StructuralParam {
        LV2_URID property{};
        ParamId id{};
    };

    void log(LV2_URID type, const char* message) const noexcept;
    void walkInput(std::uint32_t frames) noexcept;
    void handleObject(std::uint32_t frame, const LV2_Atom_Object* object) noexcept;
    void handleSet(std::uint32_t frame, const LV2_Atom_Object* object) noexcept;
    void handleGet(std::uint32_t frame, const LV2_Atom_Object* object) noexcept;
    void scheduleInstrument(const char* path, std::uint32_t length) noexcept;
    void scheduleTick(std::uint32_t frames) noexcept;
    void sendControlPorts() noexcept;
    void consumeHandoff() noexcept;
    void beginNotify() noexcept;
    void endNotify() noexcept;
    void notifyParam(std::uint32_t frame, const StructuralParam& param) noexcept;
    void notifyInstrument(std::uint32_t frame) noexcept;
    void notifyAllProperties(std::uint32_t frame) noexcept;
    void publishRestoredInstrument(std::string_view instrument) noexcept;

    plugin::Controller controller_;
    std::mutex mutex_;  ///< Taken by non-audio threads only.

    LV2_URID_Map* map_;
    const LV2_Worker_Schedule* schedule_;
    const LV2_Log_Log* log_;
    Urids urids_{};
    double sampleRate_;
    std::uint32_t maxBlock_;
    std::vector<StructuralParam> structural_;

    // ports (set by connectPort)
    const LV2_Atom_Sequence* in_{nullptr};
    LV2_Atom_Sequence* notify_{nullptr};
    float* left_{nullptr};
    float* right_{nullptr};
    std::array<const float*, kControlPortCount> controls_{};

    // audio-thread state
    std::array<std::uint32_t, kControlPortCount> lastBits_{};
    std::array<double, kControlPortCount> controlDefaults_{};
    bool resendAll_{true};
    bool notifyAll_{false};
    bool notifyInstrumentPending_{false};
    bool tickOutstanding_{false};
    std::uint64_t framesSinceTick_{0};
    std::uint64_t tickInterval_;
    Job job_{};
    LV2_Atom_Forge forge_{};
    LV2_Atom_Forge_Frame sequenceFrame_{};
    bool notifyActive_{false};
    std::array<char, kMaxPath> instrumentCache_{};
    std::uint32_t instrumentCacheLength_{0};

    // restore (non-audio) to run (audio) hand-off: 0 idle, 1 writing, 2 ready, 3 reading
    std::atomic<int> handoff_{0};
    std::array<char, kMaxPath> handoffPath_{};
    std::uint32_t handoffLength_{0};

    std::atomic<std::uint64_t> droppedInstrument_{0};
};

}  // namespace pme::lv2
