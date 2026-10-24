// SPDX-License-Identifier: MIT
// D1-R1, D1-R2 (ports), D1-R3, D1-R5, D1-R6, D1-R7 and the activation rules, through the CLAP
// interface of the plug-in object.
#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <limits>
#include <string_view>

#include "clap_test_support.hpp"
#include "pme/plugin/ids.hpp"

using namespace pme::clap;
using namespace pme::clap::test;

TEST_CASE("D1-R1 the factory serves one instrument descriptor with the provisional ids") {
    const clap_plugin_factory_t* f = factory();
    REQUIRE(f->get_plugin_count(f) == 1);
    const clap_plugin_descriptor_t* d = f->get_plugin_descriptor(f, 0);
    REQUIRE(d != nullptr);
    CHECK(f->get_plugin_descriptor(f, 1) == nullptr);
    CHECK(std::strcmp(d->id, pme::plugin::kPluginId) == 0);
    CHECK(std::strcmp(d->name, pme::plugin::kPluginName) == 0);
    CHECK(std::strcmp(d->vendor, pme::plugin::kPluginVendor) == 0);
    bool instrument = false;
    bool stereo = false;
    bool synth = false;
    for (const char* const* feature = d->features; *feature != nullptr; ++feature) {
        instrument = instrument || std::strcmp(*feature, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0;
        stereo = stereo || std::strcmp(*feature, CLAP_PLUGIN_FEATURE_STEREO) == 0;
        synth = synth || std::strcmp(*feature, CLAP_PLUGIN_FEATURE_SYNTHESIZER) == 0;
    }
    CHECK(instrument);
    CHECK(stereo);
    CHECK(synth);

    TestHost host;
    CHECK(f->create_plugin(f, &host.host, "pme.instrument.nope") == nullptr);
    CHECK(f->create_plugin(f, nullptr, pme::plugin::kPluginId) == nullptr);
    const clap_plugin_t* p = f->create_plugin(f, &host.host, pme::plugin::kPluginId);
    REQUIRE(p != nullptr);
    CHECK(p->desc == d);
    p->destroy(p);
}

TEST_CASE("D1-R1 audio ports: no inputs, one stereo output, configurations of 2 4 6 and 8") {
    Instance inst;
    REQUIRE(inst.initialised);
    const auto* ports = inst.ext<clap_plugin_audio_ports_t>(CLAP_EXT_AUDIO_PORTS);
    const auto* config = inst.ext<clap_plugin_audio_ports_config_t>(CLAP_EXT_AUDIO_PORTS_CONFIG);
    REQUIRE(ports != nullptr);
    REQUIRE(config != nullptr);
    CHECK(ports->count(inst.plugin, true) == 0);
    REQUIRE(ports->count(inst.plugin, false) == 1);
    clap_audio_port_info_t info{};
    REQUIRE(ports->get(inst.plugin, 0, false, &info));
    CHECK(info.channel_count == 2);
    CHECK((info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0);
    CHECK(std::strcmp(info.port_type, CLAP_PORT_STEREO) == 0);
    CHECK_FALSE(ports->get(inst.plugin, 1, false, &info));

    REQUIRE(config->count(inst.plugin) == 4);
    const std::array<uint32_t, 4> expected{2, 4, 6, 8};
    for (uint32_t i = 0; i < 4; ++i) {
        clap_audio_ports_config_t c{};
        REQUIRE(config->get(inst.plugin, i, &c));
        CHECK(c.input_port_count == 0);
        CHECK(c.output_port_count == 1);
        CHECK(c.main_output_channel_count == expected[i]);
    }
    const auto* configInfo =
        inst.ext<clap_plugin_audio_ports_config_info_t>(CLAP_EXT_AUDIO_PORTS_CONFIG_INFO);
    REQUIRE(configInfo != nullptr);
    CHECK(configInfo->current_config(inst.plugin) == 0);
    REQUIRE(config->select(inst.plugin, 3));
    CHECK(configInfo->current_config(inst.plugin) == 3);
    REQUIRE(ports->get(inst.plugin, 0, false, &info));
    CHECK(info.channel_count == 8);

    // The selected count becomes EngineConfig::channels at the next activate.
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 256));
    CHECK(inst.engine->lastConfig.channels == 8);
    CHECK_FALSE(config->select(inst.plugin, 0));  // not while active
    inst.plugin->deactivate(inst.plugin);
    CHECK_FALSE(config->select(inst.plugin, 4));  // no such configuration
}

TEST_CASE("D1-R2 note ports: one input accepting CLAP and MIDI, preferring CLAP") {
    Instance inst;
    const auto* notes = inst.ext<clap_plugin_note_ports_t>(CLAP_EXT_NOTE_PORTS);
    REQUIRE(notes != nullptr);
    CHECK(notes->count(inst.plugin, true) == 1);
    CHECK(notes->count(inst.plugin, false) == 0);
    clap_note_port_info_t info{};
    REQUIRE(notes->get(inst.plugin, 0, true, &info));
    CHECK((info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0);
    CHECK((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0);
    CHECK(info.preferred_dialect == CLAP_NOTE_DIALECT_CLAP);
}

TEST_CASE("D1-R3 one CLAP parameter per registry entry: continuous automatable, structural not") {
    Instance inst;
    const auto* params = inst.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    REQUIRE(params != nullptr);
    const auto registry = pme::paramRegistry();
    REQUIRE(params->count(inst.plugin) == registry.size());
    for (uint32_t i = 0; i < registry.size(); ++i) {
        clap_param_info_t info{};
        REQUIRE(params->get_info(inst.plugin, i, &info));
        const pme::ParamInfo& r = registry[i];
        CHECK(info.id == r.id);
        CHECK(info.min_value == r.min);
        CHECK(info.max_value == r.max);
        CHECK(info.default_value == r.defaultValue);
        CHECK(std::string_view(info.name) == r.name);
        const bool automatable = (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0;
        const bool structural = r.cls == pme::ParamClass::Structural;
        CHECK(automatable == !structural);
        CHECK(std::string_view(info.module).starts_with("structural") == structural);
        double value = -1.0;
        REQUIRE(params->get_value(inst.plugin, r.id, &value));
        CHECK(value == r.defaultValue);
    }
    clap_param_info_t none{};
    CHECK_FALSE(params->get_info(inst.plugin, static_cast<uint32_t>(registry.size()), &none));
    double v = 0.0;
    CHECK_FALSE(params->get_value(inst.plugin, 1010, &v));
}

TEST_CASE("D1-R3 value-to-text shows the unit and text-to-value parses it") {
    Instance inst;
    const auto* params = inst.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    std::array<char, 64> text{};
    REQUIRE(params->value_to_text(inst.plugin, 2001, -6.0, text.data(),
                                  static_cast<uint32_t>(text.size())));
    CHECK(std::strcmp(text.data(), "-6 dB") == 0);
    double v = 0.0;
    REQUIRE(params->text_to_value(inst.plugin, 2001, text.data(), &v));
    CHECK(v == -6.0);
    CHECK_FALSE(params->text_to_value(inst.plugin, 2001, "not a number", &v));
    CHECK_FALSE(params->value_to_text(inst.plugin, 424242, 1.0, text.data(), 64));
}

TEST_CASE("D1-R3 values outside the range are clamped and unknown namespaces are ignored") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 128));
    InEvents in;
    in.param(0, 2001, 500.0);
    in.param(1, 4001, 7.0, 42);  // another event space: ignored
    OutBuffers out(128);
    REQUIRE(inst.plugin->start_processing(inst.plugin));
    process(inst.plugin, 128, out, in);
    const auto* params = inst.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    double v = 0.0;
    REQUIRE(params->get_value(inst.plugin, 2001, &v));
    CHECK(v == 24.0);
    REQUIRE(params->get_value(inst.plugin, 4001, &v));
    CHECK(v == 0.004);  // default
    inst.plugin->stop_processing(inst.plugin);
    inst.plugin->deactivate(inst.plugin);
}

TEST_CASE("D1-R3 flush outside processing stores values and delivers them at the next block") {
    Instance inst;
    const auto* params = inst.ext<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    InEvents in;
    in.param(0, 2001, -12.0);
    params->flush(inst.plugin, in.list(), nullptr);  // inactive: stored only
    double v = 0.0;
    REQUIRE(params->get_value(inst.plugin, 2001, &v));
    CHECK(v == -12.0);
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    in.clear();
    in.param(0, 3001, 0.5);
    params->flush(inst.plugin, in.list(), nullptr);  // active: waits for the next block
    OutBuffers out(64);
    process(inst.plugin, 64, out, InEvents());
    // The activation queued the non-default value, the flush queued the other.
    REQUIRE(inst.engine->events.size() == 2);
    CHECK(inst.engine->events[0].offset == 0);
    CHECK(inst.engine->events[1].offset == 0);
    inst.plugin->deactivate(inst.plugin);
}

TEST_CASE("D1-R5 latency 0, tail infinite while a string sounds, one voice per key") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    const auto* latency = inst.ext<clap_plugin_latency_t>(CLAP_EXT_LATENCY);
    const auto* tail = inst.ext<clap_plugin_tail_t>(CLAP_EXT_TAIL);
    const auto* voices = inst.ext<clap_plugin_voice_info_t>(CLAP_EXT_VOICE_INFO);
    REQUIRE(latency != nullptr);
    REQUIRE(tail != nullptr);
    REQUIRE(voices != nullptr);
    CHECK(latency->get(inst.plugin) == 0);
    CHECK(tail->get(inst.plugin) == 0);

    InEvents in;
    in.note(2, CLAP_EVENT_NOTE_ON, 60, 0.8);
    OutBuffers out(64);
    process(inst.plugin, 64, out, in);
    CHECK(tail->get(inst.plugin) == std::numeric_limits<uint32_t>::max());
    in.clear();
    in.note(2, CLAP_EVENT_NOTE_OFF, 60, 0.0);
    process(inst.plugin, 64, out, in);
    CHECK(tail->get(inst.plugin) == 0);
    in.clear();
    in.midi(0, 0xB0, 64, 90);  // sustain down
    process(inst.plugin, 64, out, in);
    CHECK(tail->get(inst.plugin) == std::numeric_limits<uint32_t>::max());
    inst.plugin->reset(inst.plugin);
    CHECK(tail->get(inst.plugin) == 0);

    clap_voice_info_t info{};
    REQUIRE(voices->get(inst.plugin, &info));
    CHECK(info.voice_count == 128);
    CHECK(info.voice_capacity == 128);
    CHECK((info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) == 0);
    inst.plugin->deactivate(inst.plugin);
}

TEST_CASE("D1-R6 the plug-in forwards host thread-pool tasks to the runner") {
    Instance inst(/*withPool=*/true);
    REQUIRE(inst.initialised);
    auto& runner = controllerOf(inst.plugin).taskRunner();
    REQUIRE(runner.hasHostPool());
    std::array<int, 6> runs{};
    runner.run(
        6, [](void* ctx, uint32_t i) noexcept { ++(*static_cast<std::array<int, 6>*>(ctx))[i]; },
        &runs);
    CHECK(inst.host.execRequests == 1);
    for (int n : runs) {
        CHECK(n == 1);
    }
    // With a host pool the engine is configured with no pool of its own.
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    CHECK(inst.engine->lastConfig.threads == 1);
    inst.plugin->deactivate(inst.plugin);

    // Without one, the tasks run inline and the engine gets the thread setting.
    Instance plain;
    auto& inlineRunner = controllerOf(plain.plugin).taskRunner();
    CHECK_FALSE(inlineRunner.hasHostPool());
    runs.fill(0);
    inlineRunner.run(
        6, [](void* ctx, uint32_t i) noexcept { ++(*static_cast<std::array<int, 6>*>(ctx))[i]; },
        &runs);
    for (int n : runs) {
        CHECK(n == 1);
    }
    controllerOf(plain.plugin).setThreadsSetting(4);
    REQUIRE(plain.plugin->activate(plain.plugin, 48000.0, 1, 64));
    CHECK(plain.engine->lastConfig.threads == 4);
    plain.plugin->deactivate(plain.plugin);
}

TEST_CASE("D1-R7 render mode accepts realtime and offline and is only recorded") {
    Instance inst;
    const auto* render = inst.ext<clap_plugin_render_t>(CLAP_EXT_RENDER);
    REQUIRE(render != nullptr);
    CHECK_FALSE(render->has_hard_realtime_requirement(inst.plugin));
    CHECK(controllerOf(inst.plugin).renderMode() == pme::plugin::RenderMode::Realtime);
    REQUIRE(render->set(inst.plugin, CLAP_RENDER_OFFLINE));
    CHECK(controllerOf(inst.plugin).renderMode() == pme::plugin::RenderMode::Offline);
    REQUIRE(render->set(inst.plugin, CLAP_RENDER_REALTIME));
    CHECK(controllerOf(inst.plugin).renderMode() == pme::plugin::RenderMode::Realtime);
    CHECK_FALSE(render->set(inst.plugin, 7));
    REQUIRE(render->set(inst.plugin, CLAP_RENDER_OFFLINE));
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    CHECK_FALSE(inst.engine->lastConfig.referenceMode);  // a bounce is not reference mode
    inst.plugin->deactivate(inst.plugin);
}

TEST_CASE("D1-R1 activation succeeds with the stub engine and fails on InvalidArgument") {
    TestHost host;
    const clap_plugin_t* stub =
        createPlugin(&host.host, std::make_unique<pme::plugin::RealEngineAdapter>());
    REQUIRE(stub != nullptr);
    REQUIRE(stub->init(stub));
    CHECK(stub->activate(stub, 48000.0, 1, 512));  // the stub returns NotImplemented
    OutBuffers out(512);
    InEvents in;
    in.note(0, CLAP_EVENT_NOTE_ON, 60, 1.0);
    REQUIRE(stub->start_processing(stub));
    CHECK(process(stub, 512, out, in) != CLAP_PROCESS_ERROR);
    for (float s : out.storage) {
        CHECK(s == 0.0F);
    }
    stub->stop_processing(stub);
    stub->deactivate(stub);
    CHECK_FALSE(stub->activate(stub, -1.0, 1, 512));  // the engine rejects fs <= 0
    stub->destroy(stub);

    Instance bad(false, pme::Status::InvalidArgument);
    CHECK_FALSE(bad.plugin->activate(bad.plugin, 48000.0, 1, 512));
}

TEST_CASE("D1-R5 on_main_thread releases retired instruments") {
    Instance inst;
    REQUIRE(inst.plugin->activate(inst.plugin, 48000.0, 1, 64));
    const int before = inst.engine->collectCalls;
    inst.plugin->on_main_thread(inst.plugin);
    CHECK(inst.engine->collectCalls == before + 1);
    inst.plugin->deactivate(inst.plugin);
}
