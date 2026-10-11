// SPDX-License-Identifier: MIT
// lv2_descriptor and the C callbacks (D3-R1, D3-R2). The only exported symbol of the module.
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <cstring>
#include <memory>

#include "lv2_shell.hpp"
#include "pme/plugin/engine_adapter.hpp"
#include "pme/plugin/ids.hpp"

namespace {

using pme::lv2::Plugin;

Plugin& self(LV2_Handle h) noexcept { return *static_cast<Plugin*>(h); }

/// `maxBlockLength` from `options:options`, or 0 when absent.
std::uint32_t maxBlockOption(const LV2_Options_Option* options, LV2_URID_Map* map) noexcept {
    if (options == nullptr) {
        return 0;
    }
    const LV2_URID key = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
    const LV2_URID atomInt = map->map(map->handle, LV2_ATOM__Int);
    const LV2_URID atomLong = map->map(map->handle, LV2_ATOM__Long);
    for (const LV2_Options_Option* o = options; o->key != 0 || o->value != nullptr; ++o) {
        if (o->key != key || o->value == nullptr) {
            continue;
        }
        if (o->type == atomInt && o->size == sizeof(std::int32_t)) {
            const auto v = *static_cast<const std::int32_t*>(o->value);
            return v > 0 ? static_cast<std::uint32_t>(v) : 0U;
        }
        if (o->type == atomLong && o->size == sizeof(std::int64_t)) {
            const auto v = *static_cast<const std::int64_t*>(o->value);
            return v > 0 && v < 0x7fffffff ? static_cast<std::uint32_t>(v) : 0U;
        }
    }
    return 0;
}

LV2_Handle instantiate(const LV2_Descriptor* /*descriptor*/, double sampleRate,
                       const char* /*bundlePath*/, const LV2_Feature* const* features) {
    LV2_URID_Map* map = nullptr;
    const LV2_Worker_Schedule* schedule = nullptr;
    const LV2_Log_Log* logger = nullptr;
    const LV2_Options_Option* options = nullptr;
    for (const LV2_Feature* const* f = features; f != nullptr && *f != nullptr; ++f) {
        const char* uri = (*f)->URI;
        if (std::strcmp(uri, LV2_URID__map) == 0) {
            map = static_cast<LV2_URID_Map*>((*f)->data);
        } else if (std::strcmp(uri, LV2_WORKER__schedule) == 0) {
            schedule = static_cast<const LV2_Worker_Schedule*>((*f)->data);
        } else if (std::strcmp(uri, LV2_LOG__log) == 0) {
            logger = static_cast<const LV2_Log_Log*>((*f)->data);
        } else if (std::strcmp(uri, LV2_OPTIONS__options) == 0) {
            options = static_cast<const LV2_Options_Option*>((*f)->data);
        }
    }
    if (map == nullptr) {
        return nullptr;  // urid:map is a required feature
    }
    try {
        return new Plugin(std::make_unique<pme::plugin::RealEngineAdapter>(), sampleRate, map,
                          schedule, logger, maxBlockOption(options, map));
    } catch (...) {
        return nullptr;
    }
}

void connectPort(LV2_Handle h, std::uint32_t port, void* data) { self(h).connectPort(port, data); }

void activate(LV2_Handle h) { (void)self(h).activate(); }

void run(LV2_Handle h, std::uint32_t frames) { self(h).run(frames); }

void deactivate(LV2_Handle h) { self(h).deactivate(); }

void cleanup(LV2_Handle h) { delete static_cast<Plugin*>(h); }

LV2_State_Status stateSave(LV2_Handle h, LV2_State_Store_Function store, LV2_State_Handle handle,
                           std::uint32_t flags, const LV2_Feature* const* /*features*/) {
    return self(h).save(store, handle, flags);
}

LV2_State_Status stateRestore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                              LV2_State_Handle handle, std::uint32_t /*flags*/,
                              const LV2_Feature* const* /*features*/) {
    return self(h).restore(retrieve, handle);
}

const LV2_State_Interface kStateInterface{stateSave, stateRestore};

LV2_Worker_Status workerWork(LV2_Handle h, LV2_Worker_Respond_Function respond,
                             LV2_Worker_Respond_Handle handle, std::uint32_t size,
                             const void* data) {
    return self(h).work(respond, handle, size, data);
}

LV2_Worker_Status workerResponse(LV2_Handle h, std::uint32_t size, const void* data) {
    return self(h).workResponse(size, data);
}

const LV2_Worker_Interface kWorkerInterface{workerWork, workerResponse, nullptr};

const void* extensionData(const char* uri) {
    if (uri == nullptr) {
        return nullptr;
    }
    if (std::strcmp(uri, LV2_STATE__interface) == 0) {
        return &kStateInterface;
    }
    if (std::strcmp(uri, LV2_WORKER__interface) == 0) {
        return &kWorkerInterface;
    }
    return nullptr;
}

const LV2_Descriptor kDescriptor{pme::plugin::kLv2PluginUri,
                                 instantiate,
                                 connectPort,
                                 activate,
                                 run,
                                 deactivate,
                                 cleanup,
                                 extensionData};

}  // namespace

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(std::uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}
