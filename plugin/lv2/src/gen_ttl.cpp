// SPDX-License-Identifier: MIT
// pme_lv2_gen: writes manifest.ttl and the plug-in description into the bundle directory (D3-R2).
// Usage: pme_lv2_gen <bundle-dir> <module-file-name>
#include <cstdio>
#include <fstream>
#include <string>

#include "turtle.hpp"

namespace {
bool write(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return static_cast<bool>(f);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: pme_lv2_gen <bundle-dir> <module-file-name>\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::string error;
    const std::string plugin = pme::lv2::generatePlugin(error);
    if (plugin.empty()) {
        std::fprintf(stderr, "pme_lv2_gen: %s\n", error.c_str());
        return 1;
    }
    if (!write(dir + "/manifest.ttl", pme::lv2::generateManifest(argv[2])) ||
        !write(dir + "/" + pme::lv2::pluginFileName(), plugin)) {
        std::fprintf(stderr, "pme_lv2_gen: cannot write into %s\n", dir.c_str());
        return 1;
    }
    return 0;
}
