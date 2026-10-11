// SPDX-License-Identifier: MIT
#pragma once

/// @file engine_adapter.hpp
/// The controller layer's view of the engine (D1 "Architecture"). The real `pme::Engine` sits
/// behind it; tests substitute a recording fake. Threading follows SPEC-EXEC "Threading contract":
/// `prepare`, `setInstrument`, `collectRetired`, `popDiagnostic` on non-audio threads, `process`
/// and `reset` on the audio thread.

#include <memory>
#include <utility>

#include "pme/pme.hpp"

namespace pme::plugin {

/// Abstract engine. No CLAP types.
class EngineAdapter {
public:
    EngineAdapter() = default;
    virtual ~EngineAdapter() = default;
    EngineAdapter(const EngineAdapter&) = delete;
    EngineAdapter& operator=(const EngineAdapter&) = delete;
    EngineAdapter(EngineAdapter&&) = delete;
    EngineAdapter& operator=(EngineAdapter&&) = delete;

    /// Non-audio thread. See `Engine::prepare`.
    [[nodiscard]] virtual Status prepare(const EngineConfig& config) = 0;
    /// Non-audio thread. See `Engine::setInstrument`.
    virtual void setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept = 0;
    /// Non-audio thread. See `Engine::collectRetired`.
    virtual void collectRetired() noexcept = 0;
    /// Audio thread. See `Engine::process`.
    virtual void process(const EventList& events, AudioOut& out, int frames) noexcept = 0;
    /// Audio thread. See `Engine::reset`.
    virtual void reset() noexcept = 0;
    /// Non-audio thread. See `Engine::popDiagnostic`.
    virtual bool popDiagnostic(Diagnostic& d) noexcept = 0;
};

/// The real engine behind the adapter interface.
class RealEngineAdapter final : public EngineAdapter {
public:
    RealEngineAdapter() = default;

    [[nodiscard]] Status prepare(const EngineConfig& config) override {
        return engine_.prepare(config);
    }
    void setInstrument(std::shared_ptr<const CompiledInstrument> instrument) noexcept override {
        engine_.setInstrument(std::move(instrument));
    }
    void collectRetired() noexcept override { engine_.collectRetired(); }
    void process(const EventList& events, AudioOut& out, int frames) noexcept override {
        engine_.process(events, out, frames);
    }
    void reset() noexcept override { engine_.reset(); }
    bool popDiagnostic(Diagnostic& d) noexcept override { return engine_.popDiagnostic(d); }

private:
    Engine engine_;
};

}  // namespace pme::plugin
