// SPDX-License-Identifier: MIT
// Piano 0.7c test G7-6: CLAP state round trip with the new parameters (Body, Mic, Lid appended as ids 6, 7, 8), and states written by older
// versions (7 and 10 bytes, no body byte) load as Body Classic, Mic Close AB, Lid Open so existing sessions keep their sound.
#include <clap/clap.h>
#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Buf { std::vector<unsigned char> data; std::size_t pos{}; };
int main(int argc, char** argv) {
    try {
        require(argc == 2, "plug-in path required");
        void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); require(lib, "dlopen");
        auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry")); require(entry && entry->init(argv[1]), "entry");
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        clap_host_t host{CLAP_VERSION, nullptr, "state check", "PME", "", "0.1", [](const clap_host_t*, const char*) -> const void* { return nullptr; },
                         [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
        const auto* plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
        require(plugin && plugin->init(plugin), "instance");
        const auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        const auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        require(params && state && params->count(plugin) == 9, "9 parameters");
        const char* names[] = {"Body", "Mic", "Lid"}; clap_id ids[3];
        for (std::uint32_t i = 6; i < 9; ++i) { clap_param_info_t info{}; require(params->get_info(plugin, i, &info) && !std::strcmp(info.name, names[i - 6]), "param info"); ids[i - 6] = info.id; require(info.id == i, "ids appended after 5"); }
        const auto load = [&](std::vector<unsigned char> bytes) {
            Buf b{std::move(bytes), 0};
            clap_istream_t in{&b, [](const clap_istream_t* s, void* out, std::uint64_t n) -> std::int64_t {
                auto* p = static_cast<Buf*>(s->ctx); const std::uint64_t left = p->data.size() - p->pos, k = n < left ? n : left;
                std::memcpy(out, p->data.data() + p->pos, k); p->pos += k; return std::int64_t(k); }};
            return state->load(plugin, &in);
        };
        const auto get = [&](clap_id id) { double v = -1; require(params->get_value(plugin, id, &v), "get_value"); return int(v + 0.5); };
        // an 0.5.5-era state (9 bytes) and a 0.6 state (10 bytes): Classic
        std::vector<unsigned char> old = {'P', 'M', 'S', '4', 0, 1, 0, 101, 1, 1};
        for (std::size_t n : {std::size_t{9}, std::size_t{10}}) {
            require(load(std::vector<unsigned char>(old.begin(), old.begin() + n)), "load old");
            require(get(ids[0]) == 0 && get(ids[1]) == 0 && get(ids[2]) == 0, "old states must load as Body Classic, Close AB, Open");
        }
        // round trip
        std::vector<unsigned char> mine = {'P', 'M', 'S', '4', 1, 2, 0, 101, 2, 1, /*body Grand*/ 2, /*mic Audience*/ 3, /*lid Half*/ 2};
        require(load(mine), "load new");
        require(get(ids[0]) == 1 && get(ids[1]) == 2 && get(ids[2]) == 1, "new state values");
        Buf out;
        clap_ostream_t os{&out, [](const clap_ostream_t* s, const void* data, std::uint64_t n) -> std::int64_t {
            auto* p = static_cast<Buf*>(s->ctx); const auto* c = static_cast<const unsigned char*>(data); p->data.insert(p->data.end(), c, c + n); return std::int64_t(n); }};
        require(state->save(plugin, &os), "save");
        require(out.data == mine, "saved state differs from the loaded one");
        plugin->destroy(plugin);
        std::cout << "G7-6 PASS: CLAP state with Body/Mic/Lid round-trips; 9- and 10-byte states from older versions load as Classic\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    return 0;
}
