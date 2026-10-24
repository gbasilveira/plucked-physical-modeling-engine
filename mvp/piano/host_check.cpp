// SPDX-License-Identifier: MIT
#include <clap/clap.h>
#include <dlfcn.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <chrono>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char** argv) {
    try {
        require(argc == 2, "plug-in path required");
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        require(library, "dlopen failed");
        auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        require(entry && entry->init(argv[1]), "entry init failed");
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        require(factory && factory->get_plugin_count(factory) == 1, "factory failed");
        clap_host_t host{CLAP_VERSION, nullptr, "MVP integration check", "PME", "", "0.1",
            [](const clap_host_t*, const char*) -> const void* { return nullptr; },
            [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
        const auto* plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
        require(plugin && plugin->init(plugin), "instance failed");
        const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        const auto* notes = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        clap_audio_port_info_t info{};
        require(ports && ports->get(plugin, 0, false, &info) && info.channel_count == 2, "stereo port failed");
        require(notes && notes->count(plugin, true) == 1, "MIDI port failed");
        require(plugin->activate(plugin, 48000, 1, 512) && plugin->start_processing(plugin), "activation failed");
        std::array<float, 512> left{}, right{};
        float* channels[] = {left.data(), right.data()};
        clap_audio_buffer_t audio{}; audio.data32 = channels; audio.channel_count = 2;
        clap_event_midi_t event{};
        event.header = {sizeof(event), 128, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        event.data[0] = 0x90; event.data[1] = 60; event.data[2] = 100;
        clap_input_events_t events{&event,
            [](const clap_input_events_t*) -> std::uint32_t { return 1; },
            [](const clap_input_events_t* list, std::uint32_t i) -> const clap_event_header_t* {
                return i == 0 ? &static_cast<const clap_event_midi_t*>(list->ctx)->header : nullptr;
            }};
        clap_process_t block{}; block.frames_count = 512; block.audio_outputs = &audio;
        block.audio_outputs_count = 1; block.in_events = &events;
        require(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "process failed");
        double energy = 0;
        for (int i = 0; i < 512; ++i) {
            if (i < 128) require(left[i] == 0 && right[i] == 0, "MIDI started before event offset");
            energy += left[i] * left[i] + right[i] * right[i];
        }
        require(energy > 1e-6, "MIDI produced silence");
        // Stress all voice slots for 10 audio seconds, including 64-bit output buffers.
        plugin->reset(plugin);
        for (int key = 48; key < 64; ++key) {
            event.header.time = 0; event.data[1] = static_cast<std::uint8_t>(key);
            require(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "chord failed");
        }
        std::array<double, 512> l64{}, r64{};
        double* channels64[] = {l64.data(), r64.data()};
        audio.data32 = nullptr; audio.data64 = channels64; block.in_events = nullptr;
        const auto start = std::chrono::steady_clock::now();
        double peak = 0;
        for (int b = 0; b < 938; ++b) {
            require(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "64-bit process failed");
            for (int i = 0; i < 512; ++i) {
                require(std::isfinite(l64[i]) && std::isfinite(r64[i]), "non-finite audio");
                peak = std::max(peak, std::max(std::abs(l64[i]), std::abs(r64[i])));
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        require(peak > 1e-6 && peak <= 1, "invalid chord output");
        plugin->reset(plugin);
        require(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "reset failed");
        for (int i = 0; i < 512; ++i) require(l64[i] == 0 && r64[i] == 0, "reset not silent");
        plugin->stop_processing(plugin); plugin->deactivate(plugin); plugin->destroy(plugin);
        entry->deinit(); dlclose(library);
        std::cout << "PASS: plug-in load, ports, MIDI offset, stereo float/double, reset; 10 s of 16 voices in "
                  << seconds << " s (offline throughput, not a dropout test), peak " << peak << '\n';
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
