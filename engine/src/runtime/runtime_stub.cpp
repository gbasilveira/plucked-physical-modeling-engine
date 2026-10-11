// SPDX-License-Identifier: MIT
// P0.4 stubs of toString, compileInstrument and Engine (SPEC-API, SPEC-EXEC, ADR-0006);
// replaced by phase C1 (note group runtime) and the scheduler phases.
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "pme/engine.hpp"
#include "pme/instrument.hpp"
#include "pme/status.hpp"

namespace pme {

const char* toString(Status s) noexcept {
    switch (s) {
        case Status::Ok:
            return "Ok";
        case Status::NotImplemented:
            return "NotImplemented";
        case Status::InvalidArgument:
            return "InvalidArgument";
        case Status::Rejected:
            return "Rejected";
    }
    return "Unknown";
}

Status compileInstrument(const InstrumentDesc& /*desc*/, const EngineConfig& /*config*/,
                         const BodyModes& /*body*/,
                         std::shared_ptr<const CompiledInstrument>& /*out*/) {
    return Status::NotImplemented;
}

// Private state of the engine. The stub keeps only the configuration it was prepared with.
struct Engine::Impl {
    EngineConfig config{};
    bool prepared{false};
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Status Engine::prepare(const EngineConfig& config) {
    // SPEC-CONV: sub-block S is 16 or 32; the other fields are the P0.4-R3 sanity checks.
    const bool valid = config.fs > 0.0 && (config.subBlock == 16 || config.subBlock == 32) &&
                       config.maxBlock >= 1 && config.channels >= 1 && config.threads >= 1;
    if (!valid) {
        return Status::InvalidArgument;
    }
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->config = config;
    impl_->prepared = false;  // the stub never becomes ready
    return Status::NotImplemented;
}

void Engine::setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept {
    // The stub never swaps: the argument is released when it goes out of scope.
    (void)instrument;
}

void Engine::process(const EventList& events, AudioOut& out, int frames) noexcept {
    // SPEC-EXEC: the stub reads and ignores the events and writes exact zeros.
    (void)events;
    if (frames <= 0) {
        return;
    }
    const auto n = static_cast<std::size_t>(frames);
    for (float* ch : out.channels) {
        if (ch != nullptr) {
            std::fill_n(ch, n, 0.0f);
        }
    }
}

void Engine::reset() noexcept {}

bool Engine::popDiagnostic(Diagnostic& /*d*/) noexcept { return false; }

}  // namespace pme
