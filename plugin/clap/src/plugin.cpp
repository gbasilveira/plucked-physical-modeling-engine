// SPDX-License-Identifier: MIT
// CLAP plug-in object and extensions (D1-R1 .. D1-R7). Translates CLAP structures to and from the
// format-independent controller (plugin/common). SPEC sections used: SPEC-API (events, parameter
// classes, real-time interface), SPEC-EXEC (threading contract, instrument lifetime).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <clap/clap.h>

#include "clap_shell.hpp"
#include "pme/plugin/ids.hpp"
#include "pme/rtcheck/rtcheck.hpp"

namespace pme::clap {

namespace {

using plugin::Controller;
using plugin::ParamModel;
using plugin::RenderMode;

constexpr std::array<int, 4> kPortConfigChannels{2, 4, 6, 8};  // D1-R1
constexpr std::size_t kMaxStateBytes = 16U * 1024U * 1024U;
constexpr std::size_t kReadChunk = 4096;
constexpr std::uint32_t kTimerPeriodMs = 100;

template <std::size_t N>
void copyText(char (&dst)[N], std::string_view src) noexcept {  // NOLINT(*-avoid-c-arrays)
    const std::size_t n = std::min(src.size(), N - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

template <typename T>
const T* eventAs(const clap_event_header_t* h) noexcept {
    return static_cast<const T*>(static_cast<const void*>(h));
}

/// The host's `clap.thread-pool` seen as the runner's pool (D1-R6).
class ClapHostPool final : public plugin::TaskRunner::HostPool {
public:
    void set(const clap_host_t* host, const clap_host_thread_pool_t* ext) noexcept {
        host_ = host;
        ext_ = ext;
    }
    bool requestExec(std::uint32_t taskCount) noexcept override {
        return ext_ != nullptr && ext_->request_exec != nullptr &&
               ext_->request_exec(host_, taskCount);
    }

private:
    const clap_host_t* host_{nullptr};
    const clap_host_thread_pool_t* ext_{nullptr};
};

struct Shell {
    clap_plugin_t plugin{};
    const clap_host_t* host{nullptr};
    Controller controller;
    ClapHostPool pool;
    const clap_host_log_t* hostLog{nullptr};
    const clap_host_params_t* hostParams{nullptr};
    const clap_host_timer_support_t* hostTimer{nullptr};
    clap_id timerId{CLAP_INVALID_ID};

    explicit Shell(std::unique_ptr<plugin::EngineAdapter> engine)
        : controller(std::move(engine)) {}
};

Shell& shellOf(const clap_plugin_t* p) noexcept {
    return *static_cast<Shell*>(p->plugin_data);
}

void logHost(const Shell& s, clap_log_severity severity, const char* msg) noexcept {
    if (s.hostLog != nullptr && s.hostLog->log != nullptr) {
        s.hostLog->log(s.host, severity, msg);
    }
}

// ---- clap.audio-ports (D1-R1) ---------------------------------------------------------------

uint32_t portsCount(const clap_plugin_t* /*p*/, bool isInput) noexcept { return isInput ? 0U : 1U; }

bool portsGet(const clap_plugin_t* p, uint32_t index, bool isInput,
              clap_audio_port_info_t* info) noexcept {
    if (isInput || index != 0 || info == nullptr) {
        return false;
    }
    const int channels = shellOf(p).controller.channels();
    info->id = 0;
    copyText(info->name, "Main");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = static_cast<uint32_t>(channels);
    info->port_type = channels == 2 ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts{portsCount, portsGet};

// ---- clap.audio-ports-config (D1-R1) --------------------------------------------------------

uint32_t configCount(const clap_plugin_t* /*p*/) noexcept {
    return static_cast<uint32_t>(kPortConfigChannels.size());
}

bool configGet(const clap_plugin_t* /*p*/, uint32_t index, clap_audio_ports_config_t* config) noexcept {
    if (index >= kPortConfigChannels.size() || config == nullptr) {
        return false;
    }
    const int channels = kPortConfigChannels[index];
    config->id = index;
    char name[CLAP_NAME_SIZE];  // NOLINT(*-avoid-c-arrays)
    std::snprintf(name, sizeof(name), channels == 2 ? "Stereo" : "%d channels", channels);
    copyText(config->name, name);
    config->input_port_count = 0;
    config->output_port_count = 1;
    config->has_main_input = false;
    config->main_input_channel_count = 0;
    config->main_input_port_type = nullptr;
    config->has_main_output = true;
    config->main_output_channel_count = static_cast<uint32_t>(channels);
    config->main_output_port_type = channels == 2 ? CLAP_PORT_STEREO : nullptr;
    return true;
}

bool configSelect(const clap_plugin_t* p, clap_id id) noexcept {
    Shell& s = shellOf(p);
    if (id >= kPortConfigChannels.size() || s.controller.active()) {
        return false;
    }
    return s.controller.setChannels(kPortConfigChannels[id]);
}

clap_id configCurrent(const clap_plugin_t* p) noexcept {
    const int channels = shellOf(p).controller.channels();
    for (std::size_t i = 0; i < kPortConfigChannels.size(); ++i) {
        if (kPortConfigChannels[i] == channels) {
            return static_cast<clap_id>(i);
        }
    }
    return CLAP_INVALID_ID;
}

const clap_plugin_audio_ports_config_t kAudioPortsConfig{configCount, configGet, configSelect};

bool configInfoGet(const clap_plugin_t* /*p*/, clap_id configId, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info) noexcept {
    if (configId >= kPortConfigChannels.size() || isInput || index != 0 || info == nullptr) {
        return false;
    }
    const int channels = kPortConfigChannels[configId];
    info->id = 0;
    copyText(info->name, "Main");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = static_cast<uint32_t>(channels);
    info->port_type = channels == 2 ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_config_info_t kAudioPortsConfigInfo{configCurrent, configInfoGet};

// ---- clap.note-ports (D1-R2) ----------------------------------------------------------------

uint32_t noteCount(const clap_plugin_t* /*p*/, bool isInput) noexcept { return isInput ? 1U : 0U; }

bool noteGet(const clap_plugin_t* /*p*/, uint32_t index, bool isInput,
             clap_note_port_info_t* info) noexcept {
    if (!isInput || index != 0 || info == nullptr) {
        return false;
    }
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    copyText(info->name, "Notes");
    return true;
}

const clap_plugin_note_ports_t kNotePorts{noteCount, noteGet};

// ---- clap.params (D1-R3) --------------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t* p) noexcept {
    return static_cast<uint32_t>(shellOf(p).controller.params().count());
}

bool paramsGetInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* out) noexcept {
    const ParamModel& model = shellOf(p).controller.params();
    if (index >= model.count() || out == nullptr) {
        return false;
    }
    const ParamInfo& info = model.info(index);
    *out = clap_param_info_t{};
    out->id = info.id;
    // Continuous: automatable. Structural: not automatable; the module path starts with
    // "structural/" to mark "requires recompilation" (CLAP has no such flag).
    out->flags = info.cls == ParamClass::Continuous
                     ? static_cast<clap_param_info_flags>(CLAP_PARAM_IS_AUTOMATABLE)
                     : 0U;
    out->cookie = nullptr;
    copyText(out->name, info.name);
    copyText(out->module, ParamModel::modulePath(info));
    out->min_value = info.min;
    out->max_value = info.max;
    out->default_value = info.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* out) noexcept {
    const ParamModel& model = shellOf(p).controller.params();
    const std::size_t i = model.indexOf(id);
    if (i >= model.count() || out == nullptr) {
        return false;
    }
    *out = model.value(i);
    return true;
}

bool paramsValueToText(const clap_plugin_t* p, clap_id id, double value, char* out,
                       uint32_t capacity) noexcept {
    const ParamModel& model = shellOf(p).controller.params();
    const std::size_t i = model.indexOf(id);
    if (i >= model.count() || out == nullptr || capacity == 0) {
        return false;
    }
    ParamModel::valueToText(model.info(i), value, out, capacity);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* p, clap_id id, const char* text, double* out) noexcept {
    const ParamModel& model = shellOf(p).controller.params();
    const std::size_t i = model.indexOf(id);
    if (i >= model.count() || out == nullptr) {
        return false;
    }
    double v = 0.0;
    if (!ParamModel::textToValue(model.info(i), text, v)) {
        return false;
    }
    *out = v;
    return true;
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t* /*out*/) noexcept {
    Controller& c = shellOf(p).controller;
    if (in == nullptr) {
        return;
    }
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* e = eventAs<clap_event_param_value_t>(h);
            c.flushParamEvent(e->param_id, e->value);
        }
    }
}

const clap_plugin_params_t kParams{paramsCount,       paramsGetInfo,     paramsGetValue,
                                   paramsValueToText, paramsTextToValue, paramsFlush};

// ---- clap.state (D1-R4) ---------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) noexcept {
    if (stream == nullptr) {
        return false;
    }
    try {
        const std::string text = plugin::serializeState(shellOf(p).controller.captureState());
        const char* data = text.data();
        std::size_t left = text.size();
        while (left > 0) {
            const std::int64_t n = stream->write(stream, data, left);
            if (n <= 0) {
                return false;
            }
            data += n;
            left -= static_cast<std::size_t>(n);
        }
        return !text.empty();
    } catch (...) {
        return false;
    }
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) noexcept {
    if (stream == nullptr) {
        return false;
    }
    Shell& s = shellOf(p);
    try {
        std::string text;
        std::array<char, kReadChunk> buffer{};
        for (;;) {
            const std::int64_t n = stream->read(stream, buffer.data(), buffer.size());
            if (n < 0) {
                return false;
            }
            if (n == 0) {
                break;
            }
            text.append(buffer.data(), static_cast<std::size_t>(n));
            if (text.size() > kMaxStateBytes) {
                return false;
            }
        }
        plugin::StateData data;
        if (plugin::parseState(text, data) != plugin::StateParse::Ok) {
            return false;
        }
        const Controller::ApplyReport report = s.controller.applyState(data);
        if (report.unknown > 0) {
            logHost(s, CLAP_LOG_INFO, "pme: state contains unknown parameter ids; ignored");
        }
        if (s.hostParams != nullptr && s.hostParams->rescan != nullptr) {
            s.hostParams->rescan(s.host, CLAP_PARAM_RESCAN_VALUES);
        }
        return true;
    } catch (...) {
        return false;
    }
}

const clap_plugin_state_t kState{stateSave, stateLoad};

// ---- clap.latency, clap.tail, clap.voice-info (D1-R5) ---------------------------------------

uint32_t latencyGet(const clap_plugin_t* /*p*/) noexcept { return 0; }
const clap_plugin_latency_t kLatency{latencyGet};

uint32_t tailGet(const clap_plugin_t* p) noexcept { return shellOf(p).controller.tailSamples(); }
const clap_plugin_tail_t kTail{tailGet};

bool voiceInfoGet(const clap_plugin_t* /*p*/, clap_voice_info_t* info) noexcept {
    if (info == nullptr) {
        return false;
    }
    info->voice_count = Controller::voiceCount();
    info->voice_capacity = Controller::voiceCount();
    info->flags = 0;  // no CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES
    return true;
}
const clap_plugin_voice_info_t kVoiceInfo{voiceInfoGet};

// ---- clap.thread-pool (D1-R6) ---------------------------------------------------------------

void threadPoolExec(const clap_plugin_t* p, uint32_t taskIndex) noexcept {
    shellOf(p).controller.taskRunner().exec(taskIndex);
}
const clap_plugin_thread_pool_t kThreadPool{threadPoolExec};

// ---- clap.render (D1-R7) --------------------------------------------------------------------

bool renderHardRealtime(const clap_plugin_t* /*p*/) noexcept { return false; }

bool renderSet(const clap_plugin_t* p, clap_plugin_render_mode mode) noexcept {
    if (mode != CLAP_RENDER_REALTIME && mode != CLAP_RENDER_OFFLINE) {
        return false;
    }
    shellOf(p).controller.setRenderMode(mode == CLAP_RENDER_OFFLINE ? RenderMode::Offline
                                                                    : RenderMode::Realtime);
    return true;
}
const clap_plugin_render_t kRender{renderHardRealtime, renderSet};

// ---- clap.timer-support ---------------------------------------------------------------------

void onTimer(const clap_plugin_t* p, clap_id /*timerId*/) noexcept {
    shellOf(p).controller.mainThreadTick();
}
const clap_plugin_timer_support_t kTimer{onTimer};

// ---- plug-in --------------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) noexcept {
    Shell& s = shellOf(p);
    if (s.host->get_extension != nullptr) {
        s.hostLog =
            static_cast<const clap_host_log_t*>(s.host->get_extension(s.host, CLAP_EXT_LOG));
        s.hostParams =
            static_cast<const clap_host_params_t*>(s.host->get_extension(s.host, CLAP_EXT_PARAMS));
        s.hostTimer = static_cast<const clap_host_timer_support_t*>(
            s.host->get_extension(s.host, CLAP_EXT_TIMER_SUPPORT));
        const auto* pool = static_cast<const clap_host_thread_pool_t*>(
            s.host->get_extension(s.host, CLAP_EXT_THREAD_POOL));
        if (pool != nullptr) {
            s.pool.set(s.host, pool);
            s.controller.setHostPool(&s.pool);
        }
    }
    return true;
}

void pluginDestroy(const clap_plugin_t* p) noexcept {
    delete static_cast<Shell*>(p->plugin_data);  // NOLINT(cppcoreguidelines-owning-memory)
}

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t /*minFrames*/,
                    uint32_t maxFrames) noexcept {
    Shell& s = shellOf(p);
    try {
        if (s.controller.activate(sampleRate, maxFrames) != Status::Ok) {
            return false;
        }
    } catch (...) {
        return false;
    }
    if (s.hostTimer != nullptr && s.hostTimer->register_timer != nullptr &&
        s.timerId == CLAP_INVALID_ID) {
        if (!s.hostTimer->register_timer(s.host, kTimerPeriodMs, &s.timerId)) {
            s.timerId = CLAP_INVALID_ID;
        }
    }
    if (s.timerId == CLAP_INVALID_ID && s.host->request_callback != nullptr) {
        s.host->request_callback(s.host);  // fall back to on_main_thread
    }
    return true;
}

void pluginDeactivate(const clap_plugin_t* p) noexcept {
    Shell& s = shellOf(p);
    if (s.timerId != CLAP_INVALID_ID && s.hostTimer != nullptr) {
        s.hostTimer->unregister_timer(s.host, s.timerId);
        s.timerId = CLAP_INVALID_ID;
    }
    s.controller.mainThreadTick();
    s.controller.deactivate();
}

bool pluginStartProcessing(const clap_plugin_t* /*p*/) noexcept { return true; }
void pluginStopProcessing(const clap_plugin_t* /*p*/) noexcept {}
void pluginReset(const clap_plugin_t* p) noexcept { shellOf(p).controller.reset(); }

void translateEvents(Controller& c, const clap_input_events_t* in) noexcept {
    if (in == nullptr) {
        return;
    }
    plugin::EventTranslator& tr = c.events();
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h == nullptr || h->space_id != CLAP_CORE_EVENT_SPACE_ID) {
            continue;
        }
        switch (h->type) {
            case CLAP_EVENT_NOTE_ON: {
                const auto* e = eventAs<clap_event_note_t>(h);
                tr.noteOn(h->time, e->key, e->velocity);
                break;
            }
            case CLAP_EVENT_NOTE_OFF: {
                const auto* e = eventAs<clap_event_note_t>(h);
                tr.noteOff(h->time, e->key, e->velocity);
                break;
            }
            case CLAP_EVENT_NOTE_CHOKE: {
                const auto* e = eventAs<clap_event_note_t>(h);
                tr.noteChoke(h->time, e->key);
                break;
            }
            case CLAP_EVENT_MIDI: {
                const auto* e = eventAs<clap_event_midi_t>(h);
                tr.midi(h->time, e->data[0], e->data[1], e->data[2]);
                break;
            }
            case CLAP_EVENT_PARAM_VALUE: {
                const auto* e = eventAs<clap_event_param_value_t>(h);
                c.paramEvent(h->time, e->param_id, e->value);
                break;
            }
            default:
                break;  // gestures, modulation, transport, sysex: not used
        }
    }
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* proc) noexcept {
    // Real-time rule 6: the whole audio path runs under the trap in the rt-safety build.
    const pme::rtcheck::ScopedRealtimeThread realtime;
    Controller& c = shellOf(p).controller;
    c.beginBlock(proc->frames_count);
    translateEvents(c, proc->in_events);
    std::span<float* const> out;
    if (proc->audio_outputs_count > 0 && proc->audio_outputs != nullptr &&
        proc->audio_outputs[0].data32 != nullptr) {
        out = std::span<float* const>(proc->audio_outputs[0].data32,
                                      proc->audio_outputs[0].channel_count);
    }
    c.render(proc->frames_count, out);
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t* /*p*/, const char* id) noexcept {
    if (id == nullptr) {
        return nullptr;
    }
    struct Entry {
        const char* id;
        const void* ext;
    };
    const std::array<Entry, 13> table{{
        {CLAP_EXT_AUDIO_PORTS, &kAudioPorts},
        {CLAP_EXT_AUDIO_PORTS_CONFIG, &kAudioPortsConfig},
        {CLAP_EXT_AUDIO_PORTS_CONFIG_INFO, &kAudioPortsConfigInfo},
        {CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT, &kAudioPortsConfigInfo},
        {CLAP_EXT_NOTE_PORTS, &kNotePorts},
        {CLAP_EXT_PARAMS, &kParams},
        {CLAP_EXT_STATE, &kState},
        {CLAP_EXT_LATENCY, &kLatency},
        {CLAP_EXT_TAIL, &kTail},
        {CLAP_EXT_VOICE_INFO, &kVoiceInfo},
        {CLAP_EXT_THREAD_POOL, &kThreadPool},
        {CLAP_EXT_RENDER, &kRender},
        {CLAP_EXT_TIMER_SUPPORT, &kTimer},
    }};
    for (const Entry& e : table) {
        if (std::strcmp(id, e.id) == 0) {
            return e.ext;
        }
    }
    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p) noexcept { shellOf(p).controller.mainThreadTick(); }

// ---- descriptor and factory -----------------------------------------------------------------

// D1-R1: instrument, stereo, synthesizer.
const std::array<const char*, 4> kFeatures{CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                           CLAP_PLUGIN_FEATURE_STEREO,
                                           CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    plugin::kPluginId,
    plugin::kPluginName,
    plugin::kPluginVendor,
    plugin::kPluginUrl,
    plugin::kPluginUrl,
    plugin::kPluginUrl,
    plugin::kPluginVersion,
    plugin::kPluginDescription,
    kFeatures.data(),
};

uint32_t factoryCount(const clap_plugin_factory_t* /*f*/) noexcept { return 1; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t* /*f*/,
                                                  uint32_t index) noexcept {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t* /*f*/, const clap_host_t* host,
                                   const char* pluginId) noexcept {
    if (pluginId == nullptr || std::strcmp(pluginId, plugin::kPluginId) != 0) {
        return nullptr;
    }
    try {
        return createPlugin(host, std::make_unique<plugin::RealEngineAdapter>());
    } catch (...) {
        return nullptr;
    }
}

const clap_plugin_factory_t kFactory{factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

const clap_plugin_descriptor_t* descriptor() noexcept { return &kDescriptor; }

const clap_plugin_factory_t* factory() noexcept { return &kFactory; }

const clap_plugin_t* createPlugin(const clap_host_t* host,
                                  std::unique_ptr<plugin::EngineAdapter> engine) {
    if (host == nullptr || !clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }
    auto shell = std::make_unique<Shell>(std::move(engine));
    shell->host = host;
    shell->plugin = clap_plugin_t{
        &kDescriptor,       shell.get(),         pluginInit,  pluginDestroy,
        pluginActivate,     pluginDeactivate,    pluginStartProcessing, pluginStopProcessing,
        pluginReset,        pluginProcess,       pluginGetExtension,    pluginOnMainThread,
    };
    return &shell.release()->plugin;
}

plugin::Controller& controllerOf(const clap_plugin_t* p) noexcept { return shellOf(p).controller; }

}  // namespace pme::clap
