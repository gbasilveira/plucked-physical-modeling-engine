// SPDX-License-Identifier: MIT
// CLAP entry point (D1-R1): the only symbol the module exports.
#include <cstring>

#include <clap/clap.h>

#include "clap_shell.hpp"

namespace {

bool entryInit(const char* /*pluginPath*/) noexcept { return true; }

void entryDeinit() noexcept {}

const void* entryGetFactory(const char* factoryId) noexcept {
    if (factoryId != nullptr && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return pme::clap::factory();
    }
    return nullptr;
}

}  // namespace

// A linkage specification without braces gives the const object external linkage.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the CLAP ABI symbol.
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
