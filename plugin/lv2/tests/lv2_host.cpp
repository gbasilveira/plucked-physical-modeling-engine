// SPDX-License-Identifier: MIT
// D3-T2: a headless LV2 host built on lilv (test-only, never linked into the plug-in). It finds the
// plug-in by URI through LV2_PATH, instantiates it at 48 kHz with urid:map, options:options
// (maxBlockLength 512) and a work:schedule backed by a real worker thread, connects every port,
// activates and renders the scenario of the brief in blocks of 512 frames for 1 s.
// Usage: pme_lv2_host <plug-in URI>. Exit status 0 = pass.
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ports.hpp"
#include "turtle.hpp"

namespace {

constexpr double kRate = 48000.0;
constexpr std::uint32_t kBlock = 512;
constexpr std::uint32_t kTotalFrames = 48000;  // 1 s
constexpr std::size_t kAtomBufferBytes = 8192;
constexpr char kInstrumentPath[] = "/tmp/pme-d3-test-instrument.json";

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "D3-T2 FAIL: %s\n", what);
        ++g_failures;
    }
}

// ---- urid:map -------------------------------------------------------------------------------

class UridMap {
public:
    UridMap() {
        map_.handle = this;
        map_.map = &UridMap::mapCallback;
        unmap_.handle = this;
        unmap_.unmap = &UridMap::unmapCallback;
    }
    LV2_URID_Map* map() { return &map_; }
    LV2_URID_Unmap* unmap() { return &unmap_; }

private:
    static LV2_URID mapCallback(LV2_URID_Map_Handle h, const char* uri) {
        auto* self = static_cast<UridMap*>(h);
        const std::scoped_lock lock(self->mutex_);
        for (std::size_t i = 0; i < self->uris_.size(); ++i) {
            if (self->uris_[i] == uri) {
                return static_cast<LV2_URID>(i + 1);
            }
        }
        self->uris_.emplace_back(uri);
        return static_cast<LV2_URID>(self->uris_.size());
    }
    static const char* unmapCallback(LV2_URID_Unmap_Handle h, LV2_URID urid) {
        auto* self = static_cast<UridMap*>(h);
        const std::scoped_lock lock(self->mutex_);
        return urid >= 1 && urid <= self->uris_.size() ? self->uris_[urid - 1].c_str() : nullptr;
    }
    std::mutex mutex_;
    std::vector<std::string> uris_;
    LV2_URID_Map map_{};
    LV2_URID_Unmap unmap_{};
};

// ---- worker ---------------------------------------------------------------------------------

/// One worker thread for one plug-in instance (LV2 worker extension: schedule_work from the audio
/// thread, work on the worker thread, work_response back on the audio thread).
class Worker {
public:
    Worker() {
        schedule_.handle = this;
        schedule_.schedule_work = &Worker::scheduleCallback;
        thread_ = std::thread([this] { loop(); });
    }
    ~Worker() {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void bind(LilvInstance* instance, const LV2_Worker_Interface* iface) {
        instance_ = instance;
        iface_ = iface;
    }
    LV2_Worker_Schedule* schedule() { return &schedule_; }

    /// Audio thread: deliver the responses that arrived since the last call.
    void deliverResponses() {
        std::deque<std::vector<std::uint8_t>> responses;
        {
            const std::scoped_lock lock(mutex_);
            responses.swap(responses_);
        }
        for (const auto& r : responses) {
            iface_->work_response(lilv_instance_get_handle(instance_),
                                  static_cast<std::uint32_t>(r.size()), r.data());
        }
    }
    /// Wait until every scheduled job has run (bounded).
    bool waitIdle() {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(5), [this] { return jobs_.empty() && !busy_; });
    }

    std::thread::id workerThread() const { return thread_.get_id(); }
    std::atomic<int> instrumentJobs{0};
    std::atomic<int> tickJobs{0};
    std::atomic<int> jobsOnWorkerThread{0};
    std::atomic<int> jobsOffWorkerThread{0};

private:
    static LV2_Worker_Status scheduleCallback(LV2_Worker_Schedule_Handle h, std::uint32_t size,
                                              const void* data) {
        auto* self = static_cast<Worker*>(h);
        {
            const std::scoped_lock lock(self->mutex_);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            self->jobs_.emplace_back(bytes, bytes + size);
        }
        self->cv_.notify_all();
        return LV2_WORKER_SUCCESS;
    }
    static LV2_Worker_Status respondCallback(LV2_Worker_Respond_Handle h, std::uint32_t size,
                                             const void* data) {
        auto* self = static_cast<Worker*>(h);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        const std::scoped_lock lock(self->mutex_);
        self->responses_.emplace_back(bytes, bytes + size);
        return LV2_WORKER_SUCCESS;
    }
    void loop() {
        for (;;) {
            std::vector<std::uint8_t> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    return;  // stop requested and nothing left
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
            }
            // The first word of a job is its kind (plugin/lv2/src/lv2_shell.hpp: Job::kind).
            std::uint32_t kind = 0;
            std::memcpy(&kind, job.data(), sizeof(kind));
            if (kind == 1) {
                ++instrumentJobs;
            } else if (kind == 2) {
                ++tickJobs;
            }
            if (std::this_thread::get_id() == thread_.get_id()) {
                ++jobsOnWorkerThread;
            } else {
                ++jobsOffWorkerThread;
            }
            iface_->work(lilv_instance_get_handle(instance_), &Worker::respondCallback, this,
                         static_cast<std::uint32_t>(job.size()), job.data());
            {
                const std::scoped_lock lock(mutex_);
                busy_ = false;
            }
            cv_.notify_all();
        }
    }

    LV2_Worker_Schedule schedule_{};
    LilvInstance* instance_{nullptr};
    const LV2_Worker_Interface* iface_{nullptr};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::vector<std::uint8_t>> jobs_;
    std::deque<std::vector<std::uint8_t>> responses_;
    bool stop_{false};
    bool busy_{false};
    std::thread thread_;
};

// ---- state ----------------------------------------------------------------------------------

struct StoredProperty {
    std::uint32_t type{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> bytes;
};
using StateMap = std::map<std::uint32_t, StoredProperty>;

LV2_State_Status storeCallback(LV2_State_Handle h, std::uint32_t key, const void* value,
                               std::size_t size, std::uint32_t type, std::uint32_t flags) {
    auto* state = static_cast<StateMap*>(h);
    const auto* b = static_cast<const std::uint8_t*>(value);
    (*state)[key] = StoredProperty{type, flags, std::vector<std::uint8_t>(b, b + size)};
    return LV2_STATE_SUCCESS;
}

const void* retrieveCallback(LV2_State_Handle h, std::uint32_t key, std::size_t* size,
                             std::uint32_t* type, std::uint32_t* flags) {
    auto* state = static_cast<StateMap*>(h);
    const auto it = state->find(key);
    if (it == state->end()) {
        return nullptr;
    }
    *size = it->second.bytes.size();
    *type = it->second.type;
    *flags = it->second.flags;
    return it->second.bytes.data();
}

// ---- one plug-in instance ---------------------------------------------------------------------

struct AtomBuffer {
    // 64-bit aligned storage for an atom sequence.
    std::array<std::uint64_t, kAtomBufferBytes / 8> words{};
    LV2_Atom_Sequence* sequence() { return reinterpret_cast<LV2_Atom_Sequence*>(words.data()); }
    std::uint8_t* bytes() { return reinterpret_cast<std::uint8_t*>(words.data()); }
};

struct HostInstance {
    HostInstance(const LilvPlugin* plugin, UridMap& urids) {
        maxBlockStorage = static_cast<std::int32_t>(kBlock);
        const LV2_URID atomInt = urids.map()->map(urids.map()->handle, LV2_ATOM__Int);
        options = {
            LV2_Options_Option{LV2_OPTIONS_INSTANCE, 0,
                               urids.map()->map(urids.map()->handle, LV2_BUF_SIZE__maxBlockLength),
                               sizeof(maxBlockStorage), atomInt, &maxBlockStorage},
            LV2_Options_Option{LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr}};
        mapFeature = {LV2_URID__map, urids.map()};
        optionsFeature = {LV2_OPTIONS__options, options.data()};
        workerFeature = {LV2_WORKER__schedule, worker.schedule()};
        features = {&mapFeature, &optionsFeature, &workerFeature, nullptr};
        instance = lilv_plugin_instantiate(plugin, kRate, features.data());
        if (instance != nullptr) {
            workerIface = static_cast<const LV2_Worker_Interface*>(
                lilv_instance_get_extension_data(instance, LV2_WORKER__interface));
            stateIface = static_cast<const LV2_State_Interface*>(
                lilv_instance_get_extension_data(instance, LV2_STATE__interface));
            worker.bind(instance, workerIface);
        }
    }
    ~HostInstance() {
        if (instance != nullptr) {
            worker.waitIdle();
            lilv_instance_free(instance);
        }
    }
    HostInstance(const HostInstance&) = delete;
    HostInstance& operator=(const HostInstance&) = delete;

    void connectAll() {
        lilv_instance_connect_port(instance, pme::lv2::kPortIn, in.sequence());
        lilv_instance_connect_port(instance, pme::lv2::kPortNotify, notify.sequence());
        lilv_instance_connect_port(instance, pme::lv2::kPortOutLeft, left.data());
        lilv_instance_connect_port(instance, pme::lv2::kPortOutRight, right.data());
        for (std::size_t i = 0; i < pme::lv2::kControlPortCount; ++i) {
            lilv_instance_connect_port(instance,
                                       pme::lv2::kFirstControlPort + static_cast<std::uint32_t>(i),
                                       &controls[i]);
        }
    }

    Worker worker;
    std::array<LV2_Options_Option, 2> options{};
    std::int32_t maxBlockStorage{0};
    LV2_Feature mapFeature{};
    LV2_Feature optionsFeature{};
    LV2_Feature workerFeature{};
    std::array<const LV2_Feature*, 4> features{};
    LilvInstance* instance{nullptr};
    const LV2_Worker_Interface* workerIface{nullptr};
    const LV2_State_Interface* stateIface{nullptr};
    AtomBuffer in;
    AtomBuffer notify;
    std::array<float, kBlock> left{};
    std::array<float, kBlock> right{};
    std::array<float, pme::lv2::kControlPortCount> controls{};
};

struct Event {
    std::uint32_t frame;  // absolute frame in the scenario
    enum class Kind { Midi, SetStructural, SetInstrument } kind;
    std::uint8_t midi[3];
};


int execute(LilvWorld* world, const LilvPlugin* plugin) {
    // ---- the descriptor as lilv reads it: port count, symbols, ranges, patch:writable ----------
    check(lilv_plugin_get_num_ports(plugin) == pme::lv2::kPortCount, "port count");
    for (std::size_t i = 0; i < pme::lv2::kControlPortCount; ++i) {
        const pme::ParamInfo& info = *pme::findParam(pme::lv2::kControlPortParams[i]);
        const LilvPort* port = lilv_plugin_get_port_by_index(
            plugin, pme::lv2::kFirstControlPort + static_cast<std::uint32_t>(i));
        check(port != nullptr, "control port exists");
        if (port == nullptr) {
            continue;
        }
        check(std::string(lilv_node_as_string(lilv_port_get_symbol(plugin, port))) ==
                  pme::lv2::portSymbol(info),
              "control port symbol");
        LilvNode* def = nullptr;
        LilvNode* min = nullptr;
        LilvNode* max = nullptr;
        lilv_port_get_range(plugin, port, &def, &min, &max);
        check(def != nullptr && min != nullptr && max != nullptr, "control port range");
        if (def != nullptr && min != nullptr && max != nullptr) {
            check(static_cast<double>(lilv_node_as_float(def)) == static_cast<float>(info.defaultValue),
                  "control port default");
            check(static_cast<double>(lilv_node_as_float(min)) == static_cast<float>(info.min),
                  "control port minimum");
            check(static_cast<double>(lilv_node_as_float(max)) == static_cast<float>(info.max),
                  "control port maximum");
        }
        lilv_node_free(def);
        lilv_node_free(min);
        lilv_node_free(max);
    }
    {
        LilvNode* writable = lilv_new_uri(world, LV2_PATCH__writable);
        LilvNodes* nodes = lilv_plugin_get_value(plugin, writable);
        std::size_t structural = 0;
        for (const pme::ParamInfo& info : pme::paramRegistry()) {
            structural += info.cls == pme::ParamClass::Structural ? 1U : 0U;
        }
        check(lilv_nodes_size(nodes) == structural + 1, "patch:writable count (structural + instrument)");
        lilv_nodes_free(nodes);
        lilv_node_free(writable);
    }

    UridMap urids;
    HostInstance host(plugin, urids);
    check(host.instance != nullptr, "instantiate");
    if (host.instance == nullptr) {
        return 1;
    }
    check(host.workerIface != nullptr && host.stateIface != nullptr, "extension data");
    if (host.workerIface == nullptr || host.stateIface == nullptr) {
        return 1;
    }
    const LV2_URID midiEvent = urids.map()->map(urids.map()->handle, LV2_MIDI__MidiEvent);
    const LV2_URID patchSet = urids.map()->map(urids.map()->handle, LV2_PATCH__Set);
    const LV2_URID patchProperty = urids.map()->map(urids.map()->handle, LV2_PATCH__property);
    const LV2_URID patchValue = urids.map()->map(urids.map()->handle, LV2_PATCH__value);
    const LV2_URID instrumentProp =
        urids.map()->map(urids.map()->handle, pme::lv2::instrumentUri().c_str());
    const pme::ParamInfo& structuralInfo = *pme::findParam(5001);  // unaCorda.s0
    const LV2_URID structuralProp =
        urids.map()->map(urids.map()->handle, pme::lv2::structuralParamUri(structuralInfo).c_str());
    const LV2_URID atomSequence = urids.map()->map(urids.map()->handle, LV2_ATOM__Sequence);
    const LV2_URID atomChunk = urids.map()->map(urids.map()->handle, LV2_ATOM__Chunk);

    LV2_Atom_Forge forge;
    lv2_atom_forge_init(&forge, urids.map());

    for (std::size_t i = 0; i < pme::lv2::kControlPortCount; ++i) {
        host.controls[i] = static_cast<float>(pme::findParam(pme::lv2::kControlPortParams[i])->defaultValue);
    }
    host.connectAll();
    lilv_instance_activate(host.instance);

    // The scenario of the brief (absolute frames).
    const std::array<Event, 6> scenario{{
        {100, Event::Kind::Midi, {0x90, 60, 100}},                        // note on, key 60, vel 100
        {1000, Event::Kind::Midi, {0xB0, 64, 127}},                       // CC64 127
        {2000, Event::Kind::SetStructural, {0, 0, 0}},                    // patch:Set structural
        {3000, Event::Kind::SetInstrument, {0, 0, 0}},                    // patch:Set instrument
        {24000, Event::Kind::Midi, {0x80, 60, 0}},                        // note off
        {30000, Event::Kind::Midi, {0xB0, 64, 0}},                        // CC64 0
    }};
    const std::size_t outputGainIndex = 3;  // output.gain is the fourth control port (2001)
    bool allFinite = true;
    bool allZero = true;
    int notifyInstrumentReplies = 0;
    std::uint32_t frame = 0;
    int block = 0;
    std::size_t nextEvent = 0;
    auto runBlock = [&](std::uint32_t frames, bool useScenario) {
        host.worker.deliverResponses();
        host.in.sequence()->atom.type = atomSequence;
        lv2_atom_forge_set_buffer(&forge, host.in.bytes(), kAtomBufferBytes);
        LV2_Atom_Forge_Frame seq;
        lv2_atom_forge_sequence_head(&forge, &seq, 0);
        while (useScenario && nextEvent < scenario.size() && scenario[nextEvent].frame < frame + frames) {
            const Event& e = scenario[nextEvent++];
            lv2_atom_forge_frame_time(&forge, e.frame - frame);
            switch (e.kind) {
                case Event::Kind::Midi:
                    lv2_atom_forge_atom(&forge, 3, midiEvent);
                    lv2_atom_forge_write(&forge, e.midi, 3);
                    break;
                case Event::Kind::SetStructural: {
                    LV2_Atom_Forge_Frame obj;
                    lv2_atom_forge_object(&forge, &obj, 0, patchSet);
                    lv2_atom_forge_key(&forge, patchProperty);
                    lv2_atom_forge_urid(&forge, structuralProp);
                    lv2_atom_forge_key(&forge, patchValue);
                    lv2_atom_forge_double(&forge, 0.5);
                    lv2_atom_forge_pop(&forge, &obj);
                    break;
                }
                case Event::Kind::SetInstrument: {
                    LV2_Atom_Forge_Frame obj;
                    lv2_atom_forge_object(&forge, &obj, 0, patchSet);
                    lv2_atom_forge_key(&forge, patchProperty);
                    lv2_atom_forge_urid(&forge, instrumentProp);
                    lv2_atom_forge_key(&forge, patchValue);
                    lv2_atom_forge_path(&forge, kInstrumentPath, sizeof(kInstrumentPath) - 1);
                    lv2_atom_forge_pop(&forge, &obj);
                    break;
                }
            }
        }
        lv2_atom_forge_pop(&forge, &seq);
        if (useScenario && block == 3) {
            host.controls[outputGainIndex] = -6.0F;  // a control-port change in block 3
        }
        host.notify.sequence()->atom.type = atomChunk;
        host.notify.sequence()->atom.size = static_cast<std::uint32_t>(kAtomBufferBytes - sizeof(LV2_Atom));
        host.left.fill(1.0F);
        host.right.fill(1.0F);
        lilv_instance_run(host.instance, frames);
        for (std::uint32_t i = 0; i < frames; ++i) {
            allFinite = allFinite && std::isfinite(host.left[i]) && std::isfinite(host.right[i]);
            allZero = allZero && host.left[i] == 0.0F && host.right[i] == 0.0F;
        }
        LV2_ATOM_SEQUENCE_FOREACH(host.notify.sequence(), ev) {
            if (ev->body.type == urids.map()->map(urids.map()->handle, LV2_ATOM__Object)) {
                const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);
                if (obj->body.otype != patchSet) {
                    continue;
                }
                LV2_ATOM_OBJECT_FOREACH(obj, prop) {
                    if (prop->key == patchProperty &&
                        reinterpret_cast<const LV2_Atom_URID*>(&prop->value)->body == instrumentProp) {
                        ++notifyInstrumentReplies;
                    }
                }
            }
        }
        frame += frames;
        ++block;
    };

    while (frame < kTotalFrames) {
        runBlock(std::min(kBlock, kTotalFrames - frame), true);
    }
    check(nextEvent == scenario.size(), "every scenario event was delivered");
    check(allFinite, "every output sample is finite");
    check(allZero, "every output sample is exactly 0 (stub engine)");
    check(host.worker.waitIdle(), "worker drained");
    // A few more blocks (outside the scenario): the worker's answers reach `run`, which reports
    // the instrument on the notify port.
    for (int i = 0; i < 4; ++i) {
        runBlock(kBlock, false);
    }
    check(allFinite && allZero, "output stays silent after the scenario");
    check(host.worker.instrumentJobs.load() >= 1, "the worker executed the instrument job");
    check(host.worker.tickJobs.load() >= 1, "the worker executed at least one tick");
    check(host.worker.jobsOffWorkerThread.load() == 0 && host.worker.jobsOnWorkerThread.load() >= 2,
          "jobs ran on the worker thread");
    check(notifyInstrumentReplies >= 1, "the instrument change was reported on the notify port");

    // ---- state: save -> restore into a second instance -> save is byte identical ----------------
    StateMap saved;
    check(host.stateIface->save(lilv_instance_get_handle(host.instance), storeCallback, &saved,
                                LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, nullptr) == LV2_STATE_SUCCESS,
          "save");
    check(saved.size() == 1, "one state property");
    if (!saved.empty()) {
        const std::string text(reinterpret_cast<const char*>(saved.begin()->second.bytes.data()));
        check(text.find(kInstrumentPath) != std::string::npos, "state holds the instrument path");
        check(text.find("\"5001\":0.5") != std::string::npos, "state holds the structural change");
        check(text.find("\"2001\":-6") != std::string::npos, "state holds the control-port change");
    }
    lilv_instance_deactivate(host.instance);

    StateMap saved2;
    {
        HostInstance second(plugin, urids);
        check(second.instance != nullptr, "second instantiate");
        if (second.instance != nullptr) {
            // A host keeps control-port values itself (they are not in the plug-in state): the
            // second instance gets the values the first one ended with.
            second.controls = host.controls;
            second.connectAll();
            check(second.stateIface->restore(lilv_instance_get_handle(second.instance),
                                             retrieveCallback, &saved, 0, nullptr) == LV2_STATE_SUCCESS,
                  "restore");
            lilv_instance_activate(second.instance);
            lilv_instance_run(second.instance, kBlock);
            lilv_instance_deactivate(second.instance);
            check(second.stateIface->save(lilv_instance_get_handle(second.instance), storeCallback,
                                          &saved2, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
                                          nullptr) == LV2_STATE_SUCCESS,
                  "second save");
        }
    }
    const bool identical = !saved.empty() && saved.size() == saved2.size() &&
                           saved.begin()->second.bytes == saved2.begin()->second.bytes;
    check(identical, "state strings are byte-identical after save, restore, save");
    if (!identical) {
        for (const StateMap* m : {&saved, &saved2}) {
            if (!m->empty()) {
                std::fprintf(stderr, "  state: %s\n",
                             reinterpret_cast<const char*>(m->begin()->second.bytes.data()));
            }
        }
    }

    if (g_failures == 0) {
        std::printf("D3-T2 passed: %d blocks, %d instrument job(s), %d tick(s)\n", block,
                    host.worker.instrumentJobs.load(), host.worker.tickJobs.load());
    }
    return g_failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: pme_lv2_host <plug-in URI>\n");
        return 2;
    }
    LilvWorld* world = lilv_world_new();
    lilv_world_load_all(world);
    LilvNode* uriNode = lilv_new_uri(world, argv[1]);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uriNode);
    int status = 1;
    if (plugin == nullptr) {
        std::fprintf(stderr, "D3-T2 FAIL: plug-in %s not found through LV2_PATH\n", argv[1]);
    } else {
        status = execute(world, plugin);
    }
    lilv_node_free(uriNode);
    lilv_world_free(world);
    return status;
}
