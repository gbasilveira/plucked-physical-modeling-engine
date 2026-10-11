// SPDX-License-Identifier: MIT
#pragma once

/// @file clap_shell.hpp
/// The CLAP adapter's private interface: the factory and a constructor that tests use to build a
/// plug-in around a fake engine. The `.clap` module exports only `clap_entry` (entry.cpp).

#include <clap/clap.h>

#include <memory>

#include "pme/plugin/controller.hpp"
#include "pme/plugin/engine_adapter.hpp"

namespace pme::clap {

/// The single plug-in descriptor (D1-R1).
[[nodiscard]] const clap_plugin_descriptor_t* descriptor() noexcept;

/// The plug-in factory served by `clap_entry`.
[[nodiscard]] const clap_plugin_factory_t* factory() noexcept;

/// Create a plug-in object around `engine` (main thread). Returns null if `host` is null or its
/// CLAP version is incompatible. Destroy with `plugin->destroy`.
[[nodiscard]] const clap_plugin_t* createPlugin(const clap_host_t* host,
                                                std::unique_ptr<plugin::EngineAdapter> engine);

/// The controller behind a plug-in created by `createPlugin` (tests; main thread).
[[nodiscard]] plugin::Controller& controllerOf(const clap_plugin_t* plugin) noexcept;

}  // namespace pme::clap
