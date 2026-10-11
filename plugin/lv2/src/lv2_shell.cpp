// SPDX-License-Identifier: MIT
// LV2 adapter over the controller layer (D3-R1, D3-R2). SPEC sections used: SPEC-API "Events" and
// "Parameter classes"; SPEC-EXEC "Threading contract" and "Instrument lifetime".
#include "lv2_shell.hpp"

#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#include "pme/plugin/ids.hpp"
#include "pme/plugin/state.hpp"
#include "pme/rtcheck/rtcheck.hpp"
#include "turtle.hpp"

namespace pme::lv2 {

namespace {

LV2_URID mapUri(LV2_URID_Map* map, const char* uri) { return map->map(map->handle, uri); }

/// A numeric atom as a double; false for other types.
bool atomToDouble(const Urids& u, const LV2_Atom* atom, double& out) noexcept {
    if (atom->type == u.atomDouble && atom->size >= sizeof(double)) {
        out = reinterpret_cast<const LV2_Atom_Double*>(atom)->body;
        return true;
    }
    if (atom->type == u.atomFloat && atom->size >= sizeof(float)) {
        out = static_cast<double>(reinterpret_cast<const LV2_Atom_Float*>(atom)->body);
        return true;
    }
    if (atom->type == u.atomInt && atom->size >= sizeof(std::int32_t)) {
        out = static_cast<double>(reinterpret_cast<const LV2_Atom_Int*>(atom)->body);
        return true;
    }
    if (atom->type == u.atomLong && atom->size >= sizeof(std::int64_t)) {
        out = static_cast<double>(reinterpret_cast<const LV2_Atom_Long*>(atom)->body);
        return true;
    }
    return false;
}

}  // namespace

Plugin::Plugin(std::unique_ptr<plugin::EngineAdapter> engine, double sampleRate,
               LV2_URID_Map* map, const LV2_Worker_Schedule* schedule, const LV2_Log_Log* log,
               std::uint32_t maxBlock)
    : controller_(std::move(engine)),
      map_(map),
      schedule_(schedule),
      log_(log),
      sampleRate_(sampleRate),
      maxBlock_(maxBlock > 0 ? maxBlock : kDefaultMaxBlock),
      tickInterval_(static_cast<std::uint64_t>(std::max(1.0, sampleRate * kTickSeconds))) {
    urids_.atomSequence = mapUri(map_, LV2_ATOM__Sequence);
    urids_.atomObject = mapUri(map_, LV2_ATOM__Object);
    urids_.atomUrid = mapUri(map_, LV2_ATOM__URID);
    urids_.atomPath = mapUri(map_, LV2_ATOM__Path);
    urids_.atomString = mapUri(map_, LV2_ATOM__String);
    urids_.atomDouble = mapUri(map_, LV2_ATOM__Double);
    urids_.atomFloat = mapUri(map_, LV2_ATOM__Float);
    urids_.atomInt = mapUri(map_, LV2_ATOM__Int);
    urids_.atomLong = mapUri(map_, LV2_ATOM__Long);
    urids_.atomFrameTime = mapUri(map_, LV2_ATOM__frameTime);
    urids_.midiEvent = mapUri(map_, LV2_MIDI__MidiEvent);
    urids_.patchSet = mapUri(map_, LV2_PATCH__Set);
    urids_.patchGet = mapUri(map_, LV2_PATCH__Get);
    urids_.patchProperty = mapUri(map_, LV2_PATCH__property);
    urids_.patchValue = mapUri(map_, LV2_PATCH__value);
    urids_.instrument = mapUri(map_, instrumentUri().c_str());
    urids_.stateKey = mapUri(map_, stateUri().c_str());
    urids_.maxBlockLength = mapUri(map_, LV2_BUF_SIZE__maxBlockLength);
    urids_.logNote = mapUri(map_, LV2_LOG__Note);
    urids_.logError = mapUri(map_, LV2_LOG__Error);

    const auto registry = paramRegistry();
    structural_.reserve(registry.size());
    for (const ParamInfo& info : registry) {
        if (info.cls == ParamClass::Structural) {
            structural_.push_back({mapUri(map_, structuralParamUri(info).c_str()), info.id});
        }
    }
    lv2_atom_forge_init(&forge_, map_);
    lastBits_.fill(0U);
    controls_.fill(nullptr);
}

void Plugin::log(LV2_URID type, const char* message) const noexcept {
    if (log_ != nullptr && log_->printf != nullptr) {
        log_->printf(log_->handle, type, "pme: %s\n", message);
    }
}

void Plugin::connectPort(std::uint32_t port, void* data) noexcept {
    switch (port) {
        case kPortIn:
            in_ = static_cast<const LV2_Atom_Sequence*>(data);
            break;
        case kPortNotify:
            notify_ = static_cast<LV2_Atom_Sequence*>(data);
            break;
        case kPortOutLeft:
            left_ = static_cast<float*>(data);
            break;
        case kPortOutRight:
            right_ = static_cast<float*>(data);
            break;
        default:
            if (port >= kFirstControlPort && port < kPortCount) {
                controls_[port - kFirstControlPort] = static_cast<const float*>(data);
            }
            break;
    }
}

bool Plugin::activate() {
    const std::scoped_lock lock(mutex_);
    try {
        if (controller_.activate(sampleRate_, maxBlock_) != Status::Ok) {
            return false;
        }
    } catch (...) {
        return false;
    }
    // Every port value is sent on the first run after activation (D3-R1).
    resendAll_ = true;
    tickOutstanding_ = false;
    framesSinceTick_ = 0;
    return true;
}

void Plugin::deactivate() noexcept {
    const std::scoped_lock lock(mutex_);
    if (schedule_ == nullptr) {
        controller_.mainThreadTick();  // no worker: the tick runs here (and in save)
    }
    controller_.deactivate();
}

// ---- audio thread ---------------------------------------------------------------------------

void Plugin::run(std::uint32_t frames) noexcept {
    // Real-time rule 6: the whole audio path runs under the trap in the rt-safety build.
    const pme::rtcheck::ScopedRealtimeThread realtime;
    controller_.beginBlock(frames);
    consumeHandoff();
    beginNotify();
    if (notifyAll_) {
        notifyAllProperties(0);
        notifyAll_ = false;
    }
    if (notifyInstrumentPending_) {
        notifyInstrument(0);
        notifyInstrumentPending_ = false;
    }
    walkInput(frames);
    endNotify();
    sendControlPorts();
    std::array<float*, 2> out{left_, right_};
    controller_.render(frames, std::span<float* const>(out.data(), out.size()));
    scheduleTick(frames);
}

void Plugin::consumeHandoff() noexcept {
    int expected = 2;
    if (!handoff_.compare_exchange_strong(expected, 3, std::memory_order_acquire)) {
        return;
    }
    std::memcpy(instrumentCache_.data(), handoffPath_.data(), handoffLength_);
    instrumentCacheLength_ = handoffLength_;
    handoff_.store(0, std::memory_order_release);
    // Port values win over the restored JSON; the host learns the restored properties.
    resendAll_ = true;
    notifyAll_ = true;
}

void Plugin::walkInput(std::uint32_t frames) noexcept {
    if (in_ == nullptr) {
        return;
    }
    if (in_->body.unit != 0 && in_->body.unit != urids_.atomFrameTime) {
        return;  // time in beats: not supported, events ignored
    }
    plugin::EventTranslator& tr = controller_.events();
    LV2_ATOM_SEQUENCE_FOREACH(in_, ev) {
        const std::int64_t t = ev->time.frames;
        const auto frame = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(t, 0, frames > 0 ? static_cast<std::int64_t>(frames) - 1 : 0));
        if (ev->body.type == urids_.midiEvent) {
            const auto* bytes = static_cast<const std::uint8_t*>(LV2_ATOM_BODY_CONST(&ev->body));
            if (ev->body.size >= 1) {
                tr.midi(frame, bytes[0], ev->body.size > 1 ? bytes[1] : std::uint8_t{0},
                        ev->body.size > 2 ? bytes[2] : std::uint8_t{0});
            }
        } else if (ev->body.type == urids_.atomObject) {
            handleObject(frame, reinterpret_cast<const LV2_Atom_Object*>(&ev->body));
        }
    }
}

void Plugin::handleObject(std::uint32_t frame, const LV2_Atom_Object* object) noexcept {
    if (object->body.otype == urids_.patchSet) {
        handleSet(frame, object);
    } else if (object->body.otype == urids_.patchGet) {
        handleGet(frame, object);
    }
}

void Plugin::handleSet(std::uint32_t frame, const LV2_Atom_Object* object) noexcept {
    const LV2_Atom* property = nullptr;
    const LV2_Atom* value = nullptr;
    LV2_ATOM_OBJECT_FOREACH(object, prop) {
        if (prop->key == urids_.patchProperty) {
            property = &prop->value;
        } else if (prop->key == urids_.patchValue) {
            value = &prop->value;
        }
    }
    if (property == nullptr || value == nullptr || property->type != urids_.atomUrid ||
        property->size < sizeof(LV2_URID)) {
        return;
    }
    const LV2_URID key = reinterpret_cast<const LV2_Atom_URID*>(property)->body;
    if (key == urids_.instrument) {
        if (value->type == urids_.atomPath || value->type == urids_.atomString) {
            const auto* text = static_cast<const char*>(LV2_ATOM_BODY_CONST(value));
            scheduleInstrument(text, static_cast<std::uint32_t>(::strnlen(text, value->size)));
        }
        return;
    }
    for (const StructuralParam& p : structural_) {
        if (p.property == key) {
            double v = 0.0;
            if (atomToDouble(urids_, value, v)) {
                controller_.paramEvent(frame, p.id, v);  // the engine recompiles off-thread
            }
            return;
        }
    }
}

void Plugin::handleGet(std::uint32_t frame, const LV2_Atom_Object* object) noexcept {
    const LV2_Atom* property = nullptr;
    LV2_ATOM_OBJECT_FOREACH(object, prop) {
        if (prop->key == urids_.patchProperty) {
            property = &prop->value;
        }
    }
    if (property == nullptr) {
        notifyAllProperties(frame);
        return;
    }
    if (property->type != urids_.atomUrid || property->size < sizeof(LV2_URID)) {
        return;
    }
    const LV2_URID key = reinterpret_cast<const LV2_Atom_URID*>(property)->body;
    if (key == urids_.instrument) {
        notifyInstrument(frame);
        return;
    }
    for (const StructuralParam& p : structural_) {
        if (p.property == key) {
            notifyParam(frame, p);
            return;
        }
    }
}

void Plugin::scheduleInstrument(const char* path, std::uint32_t length) noexcept {
    // Without a worker, or with a path that does not fit, the change is ignored and counted.
    if (schedule_ == nullptr || schedule_->schedule_work == nullptr || length >= kMaxPath) {
        droppedInstrument_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    job_.kind = static_cast<std::uint32_t>(JobKind::Instrument);
    job_.length = length;
    std::memcpy(job_.path.data(), path, length);
    job_.path[length] = '\0';
    if (schedule_->schedule_work(schedule_->handle, sizeof(Job), &job_) != LV2_WORKER_SUCCESS) {
        droppedInstrument_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Plugin::scheduleTick(std::uint32_t frames) noexcept {
    framesSinceTick_ += frames;
    if (schedule_ == nullptr || schedule_->schedule_work == nullptr || tickOutstanding_ ||
        framesSinceTick_ < tickInterval_) {
        return;
    }
    framesSinceTick_ = 0;
    job_.kind = static_cast<std::uint32_t>(JobKind::Tick);
    job_.length = 0;
    tickOutstanding_ = true;
    if (schedule_->schedule_work(schedule_->handle, sizeof(Job), &job_) != LV2_WORKER_SUCCESS) {
        tickOutstanding_ = false;
    }
}

void Plugin::sendControlPorts() noexcept {
    for (std::size_t i = 0; i < kControlPortCount; ++i) {
        if (controls_[i] == nullptr) {
            continue;
        }
        const float value = *controls_[i];
        const auto bits = std::bit_cast<std::uint32_t>(value);
        if (resendAll_ || bits != lastBits_[i]) {
            lastBits_[i] = bits;
            controller_.paramEvent(0, kControlPortParams[i], static_cast<double>(value));
        }
    }
    resendAll_ = false;
}

LV2_Worker_Status Plugin::workResponse(std::uint32_t size, const void* data) noexcept {
    if (data == nullptr || size < sizeof(JobResult)) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    const auto* result = static_cast<const JobResult*>(data);
    if (result->kind == static_cast<std::uint32_t>(JobKind::Tick)) {
        tickOutstanding_ = false;
    } else if (result->kind == static_cast<std::uint32_t>(JobKind::Instrument) &&
               result->length < kMaxPath) {
        std::memcpy(instrumentCache_.data(), result->path.data(), result->length);
        instrumentCacheLength_ = result->length;
        notifyInstrumentPending_ = true;
    }
    return LV2_WORKER_SUCCESS;
}

// ---- notify port ----------------------------------------------------------------------------

void Plugin::beginNotify() noexcept {
    notifyActive_ = false;
    if (notify_ == nullptr) {
        return;
    }
    const std::uint32_t capacity = notify_->atom.size;  // the host sets the capacity before run
    lv2_atom_forge_set_buffer(&forge_, reinterpret_cast<std::uint8_t*>(notify_), capacity);
    lv2_atom_forge_sequence_head(&forge_, &sequenceFrame_, 0);
    notifyActive_ = true;
}

void Plugin::endNotify() noexcept {
    if (notifyActive_) {
        lv2_atom_forge_pop(&forge_, &sequenceFrame_);
        notifyActive_ = false;
    }
}

void Plugin::notifyParam(std::uint32_t frame, const StructuralParam& param) noexcept {
    if (!notifyActive_) {
        return;
    }
    const std::size_t index = controller_.params().indexOf(param.id);
    if (index >= controller_.params().count()) {
        return;
    }
    LV2_Atom_Forge_Frame object;
    lv2_atom_forge_frame_time(&forge_, frame);
    lv2_atom_forge_object(&forge_, &object, 0, urids_.patchSet);
    lv2_atom_forge_key(&forge_, urids_.patchProperty);
    lv2_atom_forge_urid(&forge_, param.property);
    lv2_atom_forge_key(&forge_, urids_.patchValue);
    lv2_atom_forge_double(&forge_, controller_.params().value(index));
    lv2_atom_forge_pop(&forge_, &object);
}

void Plugin::notifyInstrument(std::uint32_t frame) noexcept {
    if (!notifyActive_) {
        return;
    }
    LV2_Atom_Forge_Frame object;
    lv2_atom_forge_frame_time(&forge_, frame);
    lv2_atom_forge_object(&forge_, &object, 0, urids_.patchSet);
    lv2_atom_forge_key(&forge_, urids_.patchProperty);
    lv2_atom_forge_urid(&forge_, urids_.instrument);
    lv2_atom_forge_key(&forge_, urids_.patchValue);
    lv2_atom_forge_path(&forge_, instrumentCache_.data(), instrumentCacheLength_);
    lv2_atom_forge_pop(&forge_, &object);
}

void Plugin::notifyAllProperties(std::uint32_t frame) noexcept {
    for (const StructuralParam& p : structural_) {
        notifyParam(frame, p);
    }
    notifyInstrument(frame);
}

// ---- worker (non-audio) ---------------------------------------------------------------------

LV2_Worker_Status Plugin::work(LV2_Worker_Respond_Function respond,
                               LV2_Worker_Respond_Handle handle, std::uint32_t size,
                               const void* data) noexcept {
    if (data == nullptr || size < sizeof(Job)) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    const auto* job = static_cast<const Job*>(data);
    JobResult result;
    result.kind = job->kind;
    try {
        const std::scoped_lock lock(mutex_);
        if (job->kind == static_cast<std::uint32_t>(JobKind::Instrument)) {
            if (job->length >= kMaxPath) {
                return LV2_WORKER_ERR_UNKNOWN;
            }
            // D6 HOOK: load and compile the instrument here and hand it to
            // EngineAdapter::setInstrument (SPEC-EXEC "Instrument lifetime"); until then only the
            // reference is stored, which is what the state saves.
            controller_.setInstrumentReference(std::string(job->path.data(), job->length));
            result.length = job->length;
            std::memcpy(result.path.data(), job->path.data(), job->length);
        } else if (job->kind == static_cast<std::uint32_t>(JobKind::Tick)) {
            controller_.mainThreadTick();  // collectRetired and diagnostics
        } else {
            return LV2_WORKER_ERR_UNKNOWN;
        }
    } catch (...) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    if (respond != nullptr) {
        return respond(handle, sizeof(JobResult), &result);
    }
    return LV2_WORKER_SUCCESS;
}

// ---- state (non-audio) ----------------------------------------------------------------------

LV2_State_Status Plugin::save(LV2_State_Store_Function store, LV2_State_Handle handle,
                              std::uint32_t /*flags*/) noexcept {
    if (store == nullptr) {
        return LV2_STATE_ERR_UNKNOWN;
    }
    try {
        std::string text;
        {
            const std::scoped_lock lock(mutex_);
            if (schedule_ == nullptr) {
                controller_.mainThreadTick();
            }
            text = plugin::serializeState(controller_.captureState());  // D1's JSON
        }
        if (text.empty()) {
            return LV2_STATE_ERR_UNKNOWN;
        }
        return store(handle, urids_.stateKey, text.c_str(), text.size() + 1, urids_.atomString,
                     LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

void Plugin::publishRestoredInstrument(std::string_view instrument) noexcept {
    // Single writer (mutex held). Wait only for the audio thread's short copy (state 3).
    for (;;) {
        int expected = 0;
        if (handoff_.compare_exchange_strong(expected, 1, std::memory_order_acquire)) {
            break;
        }
        expected = 2;
        if (handoff_.compare_exchange_strong(expected, 1, std::memory_order_acquire)) {
            break;
        }
        std::this_thread::yield();
    }
    handoffLength_ = static_cast<std::uint32_t>(std::min(instrument.size(), kMaxPath - 1));
    std::memcpy(handoffPath_.data(), instrument.data(), handoffLength_);
    handoff_.store(2, std::memory_order_release);
}

LV2_State_Status Plugin::restore(LV2_State_Retrieve_Function retrieve,
                                 LV2_State_Handle handle) noexcept {
    if (retrieve == nullptr) {
        return LV2_STATE_ERR_UNKNOWN;
    }
    try {
        std::size_t size = 0;
        std::uint32_t type = 0;
        std::uint32_t flags = 0;
        const void* value = retrieve(handle, urids_.stateKey, &size, &type, &flags);
        if (value == nullptr) {
            return LV2_STATE_ERR_NO_PROPERTY;
        }
        if (type != urids_.atomString) {
            return LV2_STATE_ERR_BAD_TYPE;
        }
        std::string_view text(static_cast<const char*>(value), size);
        if (!text.empty() && text.back() == '\0') {
            text.remove_suffix(1);
        }
        plugin::StateData data;
        // A newer schema or unparsable text leaves the plug-in unchanged.
        if (plugin::parseState(text, data) != plugin::StateParse::Ok) {
            log(urids_.logError, "state rejected (malformed or newer schema)");
            return LV2_STATE_ERR_UNKNOWN;
        }
        const std::scoped_lock lock(mutex_);
        const plugin::Controller::ApplyReport report = controller_.applyState(data);
        if (report.unknown > 0) {
            log(urids_.logNote, "state contains unknown parameter ids; ignored");
        }
        publishRestoredInstrument(data.instrument);
        return LV2_STATE_SUCCESS;
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

}  // namespace pme::lv2
