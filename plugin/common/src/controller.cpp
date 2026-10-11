// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-API "Real-time interface" and "Events" (one sorted EventList per
// host block), SPEC-EXEC "Threading contract" (prepare in activate on the main thread; process,
// reset on the audio thread; collectRetired from the main thread).
#include "pme/plugin/controller.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "pme/plugin/ids.hpp"

namespace pme::plugin {

namespace {
constexpr ParamId kParamSustain = 1001;  // pedal.sustain
constexpr ParamId kParamFMax = 6003;     // strings.fMax
constexpr int kSubBlock = 32;            // SPEC-CONV: 16 or 32
constexpr int kDiagnosticsPerTick = 64;
}  // namespace

Controller::Controller(std::unique_ptr<EngineAdapter> engine) : engine_(std::move(engine)) {
    carry_.fill({});
}

Status Controller::activate(double sampleRate, std::uint32_t maxFrames) {
    if (active_) {
        deactivate();
    }
    EngineConfig cfg;
    cfg.fs = sampleRate;
    cfg.maxBlock = static_cast<int>(std::min<std::uint32_t>(
        maxFrames, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    cfg.subBlock = kSubBlock;
    cfg.channels = channels_;
    // With a host pool the engine routes its tasks through it (D6, ADR-0007); the engine then
    // starts no pool of its own. Without one, the engine uses its own pool of this size (D1-R6).
    cfg.threads = tasks_.hasHostPool() ? 1 : threadsSetting_;
    const std::size_t fMaxIndex = params_.indexOf(kParamFMax);
    cfg.fMaxStrings = fMaxIndex < params_.count() ? params_.value(fMaxIndex) : 16000.0;
    cfg.referenceMode = false;  // D1-R7: the render mode never changes this

    const Status s = engine_->prepare(cfg);
    if (s != Status::Ok && s != Status::NotImplemented) {
        return s;
    }
    engineReady_ = s == Status::Ok;
    maxBlock_ = maxFrames;
    scratch_.assign(maxFrames, 0.0F);
    held_.fill(false);
    heldCount_ = 0;
    sustain_ = 0.0F;
    carryCount_ = 0;
    sounding_.store(false);
    queue_.clear();
    for (std::size_t i = 0; i < params_.count(); ++i) {
        const ParamInfo& info = params_.info(i);
        if (params_.value(i) != info.defaultValue) {
            (void)queue_.push({info.id, params_.value(i)});
        }
    }
    active_ = true;
    return Status::Ok;
}

void Controller::deactivate() noexcept {
    active_ = false;
    engineReady_ = false;
}

bool Controller::setChannels(int channels) noexcept {
    if (channels < 1 || channels > kMaxChannels) {
        return false;
    }
    channels_ = channels;
    return true;
}

void Controller::setThreadsSetting(int threads) noexcept {
    threadsSetting_ = std::clamp(threads, 1, 8);
}

StateData Controller::captureState() const {
    StateData s;
    s.pluginVersion = kPluginVersion;
    s.instrument = instrument_;
    for (std::size_t i = 0; i < params_.count(); ++i) {
        const ParamInfo& info = params_.info(i);
        if (params_.value(i) != info.defaultValue) {
            s.params.emplace_back(info.id, params_.value(i));
        }
    }
    return s;
}

Controller::ApplyReport Controller::applyState(const StateData& state) {
    ApplyReport report;
    std::vector<double> target(params_.count());
    for (std::size_t i = 0; i < params_.count(); ++i) {
        target[i] = params_.info(i).defaultValue;
    }
    for (const auto& [id, value] : state.params) {
        const std::size_t i = params_.indexOf(id);
        if (i >= params_.count()) {
            ++report.unknown;
            continue;
        }
        target[i] = ParamModel::clamp(params_.info(i), value);
        ++report.applied;
    }
    for (std::size_t i = 0; i < params_.count(); ++i) {
        if (params_.value(i) != target[i]) {
            params_.setValue(i, target[i]);
            if (active_) {
                (void)queue_.push({params_.info(i).id, target[i]});
            }
        }
    }
    instrument_ = state.instrument;
    return report;
}

void Controller::mainThreadTick() noexcept {
    engine_->collectRetired();
    Diagnostic d;
    for (int i = 0; i < kDiagnosticsPerTick && engine_->popDiagnostic(d); ++i) {
        ++diagnostics_;
    }
}

std::uint32_t Controller::tailSamples() const noexcept {
    return sounding_.load(std::memory_order_relaxed) ? std::numeric_limits<std::uint32_t>::max()
                                                     : 0U;
}

void Controller::beginBlock(std::uint32_t frames) noexcept {
    translator_.begin(frames);
    for (std::size_t i = 0; i < carryCount_; ++i) {
        translator_.param(0, carry_[i].id, carry_[i].value);
    }
    carryCount_ = 0;
    ParamChange c;
    while (queue_.pop(c)) {
        translator_.param(0, c.id, c.value);
    }
}

void Controller::paramEvent(std::uint32_t offset, ParamId id, double value) noexcept {
    const std::size_t i = params_.indexOf(id);
    if (i >= params_.count()) {
        return;
    }
    const double v = params_.setValue(i, value);
    translator_.param(offset, id, v);
}

void Controller::flushParamEvent(ParamId id, double value) noexcept {
    const std::size_t i = params_.indexOf(id);
    if (i >= params_.count()) {
        return;
    }
    const double v = params_.setValue(i, value);
    if (active_ && carryCount_ < carry_.size()) {
        carry_[carryCount_++] = {id, v};
    }
}

void Controller::track(const Event& e) noexcept {
    if (const auto* on = std::get_if<NoteOn>(&e.payload)) {
        const auto key = static_cast<std::size_t>(on->key);
        if (key < held_.size() && !held_[key]) {
            held_[key] = true;
            ++heldCount_;
        }
    } else if (const auto* off = std::get_if<NoteOff>(&e.payload)) {
        const auto key = static_cast<std::size_t>(off->key);
        if (key < held_.size() && held_[key]) {
            held_[key] = false;
            --heldCount_;
        }
    } else if (const auto* pedal = std::get_if<Pedal>(&e.payload)) {
        if (pedal->which == PedalKind::Sustain) {
            sustain_ = pedal->value;
        }
    } else if (const auto* param = std::get_if<Param>(&e.payload)) {
        if (param->id == kParamSustain) {
            sustain_ = static_cast<float>(param->value);
        }
    }
}

void Controller::render(std::uint32_t frames, std::span<float* const> hostChannels) noexcept {
    translator_.sort();
    const std::span<Event> evs = translator_.events();
    for (const Event& e : evs) {
        track(e);
    }
    sounding_.store(heldCount_ > 0 || sustain_ > 0.0F, std::memory_order_relaxed);

    if (frames == 0) {
        // A block without audio acts as a flush: parameter values wait for the next block.
        for (const Event& e : evs) {
            if (const auto* p = std::get_if<Param>(&e.payload);
                p != nullptr && carryCount_ < carry_.size()) {
                carry_[carryCount_++] = {p->id, p->value};
            }
        }
        return;
    }
    if (!engineReady_) {
        for (float* ch : hostChannels) {
            if (ch != nullptr) {
                std::memset(ch, 0, sizeof(float) * frames);
            }
        }
        return;
    }
    const std::uint32_t chunk = maxBlock_ > 0 ? maxBlock_ : frames;
    std::size_t next = 0;
    for (std::uint32_t start = 0; start < frames; start += chunk) {
        const std::uint32_t n = std::min(chunk, frames - start);
        std::size_t end = next;
        while (end < evs.size() && static_cast<std::uint32_t>(evs[end].offset) < start + n) {
            evs[end].offset -= static_cast<std::int32_t>(start);  // re-base for the chunk
            ++end;
        }
        renderChunk(evs.subspan(next, end - next), start, n, hostChannels);
        next = end;
    }
}

void Controller::renderChunk(std::span<const Event> events, std::uint32_t start,
                             std::uint32_t frames, std::span<float* const> hostChannels) noexcept {
    const auto channels = static_cast<std::size_t>(channels_);
    for (std::size_t c = 0; c < channels; ++c) {
        float* host = c < hostChannels.size() ? hostChannels[c] : nullptr;
        pointers_[c] = host != nullptr ? host + start : scratch_.data();
    }
    for (std::size_t c = channels; c < hostChannels.size(); ++c) {
        if (hostChannels[c] != nullptr) {
            std::memset(hostChannels[c] + start, 0, sizeof(float) * frames);
        }
    }
    AudioOut out{std::span<float* const>(pointers_.data(), channels)};
    engine_->process(EventList(events.data(), events.size()), out, static_cast<int>(frames));
}

void Controller::reset() noexcept {
    if (engineReady_) {
        engine_->reset();
    }
    held_.fill(false);
    heldCount_ = 0;
    sustain_ = 0.0F;
    sounding_.store(false, std::memory_order_relaxed);
}

}  // namespace pme::plugin
