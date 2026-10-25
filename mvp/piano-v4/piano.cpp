// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "preset.hpp"
#include "bridge_model.hpp"
#include "string_loss.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <cstdlib>

namespace piano_mvp_v4 {
bool Piano::prepare(double fs) {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000) return false;
    fs_ = fs;
    if (!qualitySet_) tensionLimit_ = tensionMaxKey_;
    shared_ = sharedBoard_ && feedback_;
    leakyActive_ = leaky_ && shared_;
    std::vector<double> phi;
    if (shared_) buildParticipation(grandGeometry_, phi);
    releaseStep_ = std::exp(-35.0 / fs);
    for (int i = 0; i < 88; ++i) {
        auto& v = voices_[i];
        v.undamped = undampedTreble_ && i + 21 >= firstUndampedKey;
        v.pan = 0.2 + 0.6 * i / 87.0;
        const auto preset = piano_mvp_v2::parametersForKey(i + 21);
        pme::exciters::StruckNoteDesc d;
        const int count = unisons_ ? stringsForKey(i + 21) : 1;
        for (int j = 0; j < count; ++j) {
            auto string = preset;
            // Small symmetric unison spread: an explicit tuning assumption, not
            // measured data. Compile each string, including its own bridge terms.
            const double cents = count == 1 ? 0 : (j - (count - 1) * 0.5) * 0.8;
            string.string.f1Target *= std::exp2(cents / 1200.0);
            if (piano_mvp_v2::compilePresetString(string, fs, v.modes[j]) != pme::Status::Ok)
                return false;
            if (saturatingLoss_ && applySaturatingStringLoss(v.modes[j], preset.source.transverse_viscosity_s,
                    kStringLossCornerHz) != pme::Status::Ok)
                return false;
            if (feedback_ && residualLoss_ &&
                (shared_ ? applyResidualBridgeLoss(v.modes[j],phi.data()+i*kBoardModeCount+kLocalBridgeModes)
                         : applyResidualBridgeLoss(v.modes[j],v.pan))!=pme::Status::Ok)
                return false;
            if (pme::compileBridgeTerms(v.modes[j], pme::BridgeEnd::AtL, v.terms[j]) != pme::Status::Ok)
                return false;
            d.strings.push_back(&v.modes[j]);
        }
        if (shared_ && !v.light.configure(v.modes.data(), count, sympatheticCutHz)) return false;
        if (leakyActive_) {
            // 0.5.3: one damper law for the unstruck light model and (leakyRelease_) the struck modes.
            v.light.configureLeak(v.modes.data(), damperRate_, damperExponent_, damperRefHz);
            for (int j = 0; j < count; ++j) {
                v.leak[j].assign(v.modes[j].omega.size(), 1.0);
                for (std::size_t n = 0; n < v.modes[j].omega.size(); ++n)
                    v.leak[j][n] = SympatheticString::leakFactor(v.modes[j].omega[n] / (2 * std::numbers::pi), fs, damperRate_, damperExponent_, damperRefHz);
            }
        }
        d.hammer = preset.hammer;
        // Treat the tabulated felt stiffness as the whole contact patch and
        // distribute it across strings. Keep one full-mass shared hammer.
        d.hammer.K /= count;
        // Voicing experiment: softer contact, same mass, exponent and velocity map.
        // Reference comparison supports softening bass/mids, not the treble.
        // The relative scale is a voicing choice, not measured felt stiffness.
        if (softFelt_) d.hammer.K *= feltStiffnessScale(i+21);
        // Shared board: the point has no modes of its own; its motion is the board's port motion
        // fed through the engine's external-velocity hook (SPEC-BODY Sympathetic path).
        if (feedback_ && v.bridge.configure({},shared_ ? pme::bridge::ParallelModel{} : localBridgeModel(v.pan),fs)
                != pme::Status::Ok)
            return false;
        // 0.5.2 SPEC-GEO: the core's axial stiffness EA = E_c pi d_c^2 / 4 (longitudinal.cpp
        // axialStiffness); for wound bass strings d_c is the report's core diameter, so the winding
        // adds no axial stiffness.
        const double ea = preset.string.ECore * std::numbers::pi * preset.string.dCore * preset.string.dCore / 4.0;
        const std::array<double, 3> eas{ea, ea, ea};
        const bool useTension = tension_ && i + 21 <= tensionMaxKey_;
        if (v.note.configure(d,fs,feedback_ ? &v.bridge : nullptr,
                             useTension ? std::span<const double>(eas.data(), count) : std::span<const double>{}) != pme::Status::Ok)
            return false;
        for (int j = 0; j < count; ++j) v.note.setTermCoefficients(static_cast<std::size_t>(j), v.terms[j].a, v.terms[j].tOverL);
        v.note.setFastKernels(fast_); v.light.setFast(fast_); board_.setFast(fast_);
        v.note.setTruncation(truncation_);
        v.note.setTensionEnabled(i + 21 <= tensionLimit_);
    }
    if (shared_) {
        // Harmonic coincidence of key c's idle partials with key s's sounding partials (first 8
        // each, within 20 cents, weight 1/(m n)): ranks which unstruck strings to simulate when
        // the sympathetic limit is reached. A heuristic, not a measured coupling.
        coincidence_.assign(88 * 88, 0.0f);
        for (int c = 0; c < 88; ++c) for (int s = 0; s < 88; ++s) {
            if (c == s) continue;
            const auto& oc = voices_[c].modes[0].omega; const auto& os = voices_[s].modes[0].omega;
            for (std::size_t n = 0; n < std::min<std::size_t>(8, oc.size()); ++n)
                for (std::size_t m = 0; m < std::min<std::size_t>(8, os.size()); ++m)
                    if (std::abs(1200 * std::log2(os[m] / oc[n])) < 20) coincidence_[c * 88 + s] += 1.0f / float((n + 1) * (m + 1));
        }
        if (!board_.configure(fs,std::move(phi),kLocalBridgeModes,false,bridgeMass_)) return false;
    } else if (!soundboard_.prepare(fs)) return false;
    bridgeMassFilter_.prepare(fs);
    sleepSamples_ = static_cast<std::uint64_t>(sleepSeconds_ * fs);
    radThread_.stop(); pool_.stop();
    ring_.keys.assign(std::size_t{ringSize} * 88, 0); ring_.count.assign(ringSize, 0);
    ring_.g.assign(std::size_t{ringSize} * 88, 0.0);
    ring_.outL.assign(ringSize, 0.0f); ring_.outR.assign(ringSize, 0.0f);
    blockL_.assign(maxBlockFrames, 0.0f); blockR_.assign(maxBlockFrames, 0.0f);
    produced_.store(0); consumed_.store(0); radLock_.store(0); radFault_.store(false);
    ready_ = true; reset();
    return startThreads();
}
void Piano::reset() noexcept {
    idle_ = false; idleCount_ = 0;
    // A reset inside a block (MIDI CC120) must not discard the radiation of frames already stepped: finish
    // the pending records first (no-op outside a block).
    while (consumed_.load(std::memory_order_acquire) < produced_.load(std::memory_order_relaxed))
        if (!pumpRecords(~0u)) {
#if defined(__x86_64__) || defined(__i386__)
            _mm_pause();
#endif
        }
    for (auto& v : voices_) {
        v.note.reset(); v.note.clearDiagnostics(); v.bridge.reset(); v.light.reset(); v.lightW = v.dwPrev = 0;
        v.active = v.held = v.struck = v.sympathetic = false; v.age = v.quiet = 0; v.release = 1; v.force = 0;
        v.slot = -1; v.peakForce = 0; v.sinceTruncation = 0;
    }
    heldCount_.fill(0); slotLoad_.fill(0.0); slotsDirty_ = true;
    sampleClock_ = 0;
    soundboard_.reset(); if (shared_) board_.reset(); bridgeMassFilter_.reset();
    pedal_ = false; unaCordaDown_ = false; clock_ = 0; numericalFaults_ = 0; voiceSteals_ = 0; peakBridgeMotion_ = 0;
}
int Piano::activeVoices() const noexcept {
    int n = 0; for (const auto& v : voices_) n += v.active && v.struck; return n;
}
int Piano::sympatheticVoices() const noexcept {
    int n = 0; for (const auto& v : voices_) n += v.active && !v.struck; return n;
}
void Piano::noteOn(int key, double velocity) noexcept {
    if (!ready_ || key < 21 || key > 108 || !std::isfinite(velocity)) return;
    if (velocity <= 0) { noteOff(key); return; }
    idle_ = false; idleCount_ = 0;
    auto& v = voices_[key - 21];
    // Steal the oldest struck voices until a slot is free (a lower limit set at run time by
    // setQuality may leave more voices than the limit sounding; they are released one per note-on).
    if (!v.struck && activeVoices() >= voiceLimit_) {
        Voice* oldest = nullptr;
        for (auto& x : voices_) if (x.active && x.struck && (!oldest || x.age < oldest->age)) oldest = &x;
        if (oldest) {
            ++voiceSteals_;
            oldest->active = oldest->held = oldest->struck = oldest->sympathetic = false;
            oldest->note.reset(); oldest->bridge.reset(); oldest->force = 0;
            heldCount_[static_cast<std::size_t>(oldest - voices_.data())] = 0;
        }
    }
    const bool wasSympathetic = shared_ && v.active && !v.struck;
    if (!v.active) { v.note.reset(); v.bridge.reset(); if (shared_) board_.resetPort(key - 21); }
    if (shared_ && !v.struck) {
        // A struck string joins the board with the board's current motion as history, so the
        // first acceleration is not a step. A ringing unstruck string hands its vertical modes over.
        if (wasSympathetic) {
            v.note.reset(); v.bridge.reset();
            auto& s = v.note.segment();
            for (std::size_t j = 0; j < v.light.strings(); ++j) {
                const auto off = v.note.modeOffset(j);
                for (std::size_t n = 0; n < v.light.modeCount(j); ++n) { s.q()[off+n] = v.light.q(j)[n]; s.u()[off+n] = v.light.u(j)[n]; }
            }
        }
        primeBridge(v, key - 21);
    }
    if (// SPEC-HAM Una corda: shift s = 1 while the soft pedal is down at launch (a hammer in
    // flight is never moved: setShift happens only inside launch()).
    v.note.launch(std::clamp(velocity, 0.001, 1.0), unaCordaDown_ ? 1.0 : 0.0) != pme::Status::Ok) return;
    v.note.setTensionEnabled(key <= tensionLimit_); v.note.setTruncation(truncation_); v.note.setTensionFloor(tensionFloor_);
    v.peakForce = 0; v.sinceTruncation = 0;
    if (heldCount_[key - 21] < 255) ++heldCount_[key - 21];
    v.active = v.held = v.struck = true; v.sympathetic = false; v.release = 1; v.age = ++clock_; v.quiet = 0;
}
void Piano::noteOff(int key) noexcept {
    if (key < 21 || key > 108) return;
    auto& n = heldCount_[key - 21];
    // 0.5.4: overlapping note-ons of one key (note-on, note-on, note-off) release the key only when
    // the last of them is released; the 0.5.3 flag released it at the first note-off.
    if (heldCount_on_ && n > 1) { --n; return; }
    n = 0; voices_[key - 21].held = false;
}
void Piano::primeBridge(Voice& v, int port) noexcept {
    v.bridge.setExternalVelocity(board_.portIncrement(port) * fs_);
    v.bridge.advance();   // no force on a modeless, conductance-free point: only the history moves
}
void Piano::sustain(bool down) noexcept { pedal_ = down; }
void Piano::sample(float& left, float& right) noexcept {
    double l = 0, r = 0;
    if (!ready_) { left = right = 0; return; }
    if (shared_) { sampleShared(left, right); return; }
    for (auto& v : voices_) {
        if (!v.active) continue;
        v.note.step();
        auto& s = v.note.segment();
        if (!v.held && !pedal_ && !v.undamped) {
            v.release *= releaseStep_;
            // MVP diagonal damper: remove energy from string coordinates only.
            for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                const auto off = v.note.modeOffset(j);
                for (std::size_t n = 0; n < v.note.modeCount(j); ++n) {
                    s.q()[off+n] *= releaseStep_; s.u()[off+n] *= releaseStep_;
                }
            }
            v.note.clampPsi();
        }
        const double w = feedback_ ? v.bridge.wV() : 0;
        peakBridgeMotion_ = std::max(peakBridgeMotion_,std::abs(w));
        double force = 0;
        for (std::size_t j = 0; j < v.note.stringCount(); ++j)
            force += pme::bridge::bridgeForce<double>(v.terms[j], s.q() + v.note.modeOffset(j), w);
        if (!std::isfinite(force)) {
            ++numericalFaults_; v.note.reset(); v.bridge.reset(); v.active = false; continue;
        }
        l += force * (1 - v.pan); r += force * v.pan;
        if (std::abs(force) < 1e-7 && !v.note.inFlight()) ++v.quiet; else v.quiet = 0;
        if (v.release < 1e-6 || v.quiet > static_cast<std::uint64_t>(fs_)) {
            v.active = v.struck = false; v.note.reset(); v.bridge.reset();
        }
    }
    // 0.4.9: the board sees the force through the bridge-top mass (series-mass low pass).
    if (bridgeMass_) bridgeMassFilter_.process(l, r);
    double pressureL, pressureR;
    soundboard_.sample(l, r, pressureL, pressureR);
    // Fixed linear gain; pressure is never mixed with raw string force.
    left = static_cast<float>(0.05 * pressureL);
    right = static_cast<float>(0.05 * pressureR);
    if (!std::isfinite(left) || !std::isfinite(right)) {
        ++numericalFaults_; left = right = 0; soundboard_.reset();
    }
}

// 0.5.1 shared board. Per-sample order (SPEC-BRG Discrete coupling order, generalised to many
// bridge points): (1) every active port adds its F^n = sum_strings F_b,s(q^n, w^n) to the board;
// (2) the board advances all modes with those forces; (3) each port's w^{n+1}-w^n reaches its
// strings: struck voices get it through the engine's external-velocity hook, so that
// BridgePoint::advance forms delta_tt w^n = (dw^n - dw^{n-1})/dt^2 of the board exactly (the point
// has no modes or conductance of its own); unstruck voices form the same acceleration directly;
// (4) the strings advance (hammer, SAV, bridge reaction).
void Piano::wakeSympathetic() noexcept {
    // Candidate pools: strings with lifted dampers, and (0.5.3) damped ones whose felt leaks. Each
    // pool has its own cap; lifted strings are woken first. Leaky off: one pool, the 0.5.2 rule.
    int liftedAwake = 0, dampedAwake = 0;
    for (const auto& v : voices_)
        if (v.active && !v.struck) { if (leakyActive_ && isDamped(v)) ++dampedAwake; else ++liftedAwake; }
    int freeLift = sympLimit_ - liftedAwake, freeLeak = leakyActive_ ? leakyLimit_ - dampedAwake : 0;
    if (freeLift <= 0 && freeLeak <= 0) return;
    std::array<double, 88> speedLift{}, speedLeak{};
    int candLift = 0, candLeak = 0;
    for (int i = 0; i < 88; ++i) {
        const auto& v = voices_[i];
        if (v.active) continue;
        const double speed = std::abs(board_.portVelocity(i));
        if (v.held || pedal_ || v.undamped) { speedLift[i] = speed; candLift += speed > wakeVelocity; }
        else if (leakyActive_) { speedLeak[i] = speed; candLeak += speed > wakeVelocity; }
    }
    if (!candLift && !candLeak) return;
    // Score candidates by harmonic coincidence with the loudest sounding struck strings (a proxy of
    // expected excitation); used only when a pool's limit is smaller than its candidates. Ties:
    // lower key.
    std::array<double, 88> score{};
    const bool rankLift = candLift > freeLift, rankLeak = candLeak > freeLeak;
    if (rankLift || rankLeak)
        for (int s = 0; s < 88; ++s) {
            const auto& v = voices_[s];
            if (!v.active || !v.struck) continue;
            const double weight = std::abs(v.force);
            for (int c = 0; c < 88; ++c) score[c] += weight * coincidence_[c * 88 + s];
        }
    const auto wake = [&](std::array<double, 88>& speed, int free, bool rank) {
        while (free-- > 0) {
            int best = -1; double bestScore = -1;
            for (int i = 0; i < 88; ++i) {
                if (!(speed[i] > wakeVelocity)) continue;
                const double sc = rank ? score[i] : 0.0;
                if (sc > bestScore) { best = i; bestScore = sc; }
            }
            if (best < 0) break;
            speed[best] = 0;
            auto& v = voices_[best];
            v.light.reset(); board_.resetPort(best); v.lightW = 0; v.dwPrev = board_.portIncrement(best);
            v.active = v.sympathetic = true; v.struck = false;   // strings start from rest here
            v.release = 1; v.age = 0; v.quiet = 0; v.force = 0;
        }
    };
    wake(speedLift, freeLift, rankLift);
    wake(speedLeak, freeLeak, rankLeak);
}
const Piano::Preset& Piano::preset(Quality q) noexcept {
    //                                 voices sympathetic leaky tensionMaxKey truncation fast  sleepForce sleepSeconds
    static const Preset live{16, 12, 6, 40, 1e-6, true, 1e-6, 0.5, 1e-10, true};
    static const Preset balanced{24, 16, 8, 47, 1e-7, true, 3e-7, 0.75, 1e-12, true};
    static const Preset full{maxPolyphony, maxPolyphony, maxPolyphony, 108, 0.0, false, 1e-7, 1.0, 1e-14, false};
    return q == Quality::Live ? live : (q == Quality::Balanced ? balanced : full);
}
void Piano::setQuality(Quality q) noexcept { applyPreset(q, preset(q)); }
// Shed levels (Live caps; Balanced takes the smaller of its preset and the level): struck voices,
// sympathetic strings, leaky strings, highest key with tension modulation (20 = none).
void Piano::applyShed() noexcept {
    const Preset& p = preset(quality_);
    static const int voices[4] = {1 << 20, 12, 10, 8}, symp[4] = {1 << 20, 6, 3, 1}, leaky[4] = {1 << 20, 3, 2, 1}, tension[4] = {1 << 20, 33, 20, 20};
    const int s = (quality_ == Quality::Full) ? 0 : shed_;
    voiceLimit_ = std::min(p.voices, voices[s]); sympLimit_ = std::min(p.sympathetic, symp[s]);
    leakyLimit_ = std::min(p.leaky, leaky[s]); tensionLimit_ = std::min(p.tensionMaxKey, tension[s]);
    // Sounding notes above the new tension limit stop modulating (the same as V_KC falling below the floor).
    if (s > 0) for (int i = 0; i < 88; ++i) if (voices_[i].active && voices_[i].struck && i + 21 > tensionLimit_) voices_[i].note.setTensionEnabled(false);
}
// Called from blockEnd with the block's wall time.
void Piano::governBlock() noexcept {
    if (!adaptive_ || quality_ == Quality::Full || !blockFrames_) return;
    const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - blockT0_).count() * fs_ / blockFrames_;
    costEma_ += (cost - costEma_) * 0.08;
    shedHold_ += blockFrames_;
    if (idle_) { if (shed_) { shed_ = 0; applyShed(); } shedCalm_ = 0; costEma_ = 0; return; }
    if ((cost > 1.0 || costEma_ > 0.7) && shed_ < 3 && shedHold_ > static_cast<std::uint64_t>(0.15 * fs_)) {
        ++shed_; ++shedEvents_; shedHold_ = 0; shedCalm_ = 0; applyShed();
    } else if (shed_ > 0 && costEma_ < 0.2) {
        shedCalm_ += blockFrames_;
        if (shedCalm_ > static_cast<std::uint64_t>(4 * fs_)) { --shed_; shedCalm_ = 0; shedHold_ = 0; applyShed(); }
    } else shedCalm_ = 0;
}
void Piano::applyPreset(Quality q, const Preset& p) noexcept {
    quality_ = q; qualitySet_ = true;
    voiceLimit_ = p.voices; sympLimit_ = p.sympathetic; leakyLimit_ = p.leaky; tensionLimit_ = p.tensionMaxKey; shed_ = 0; costEma_ = 0;
    truncation_ = p.truncation; fast_ = p.fast; sleepForce_ = p.sleepForce; sleepSeconds_ = p.sleepSeconds; tensionFloor_ = p.tensionFloor; idleSkip_ = p.idleSkip; if (!idleSkip_) idle_ = false;
    sleepSamples_ = static_cast<std::uint64_t>(p.sleepSeconds * fs_);
    // Sounding notes keep their tension model (changed at the next note-on); truncation and fast sums
    // apply to them from now on. Modes already truncated stay off until the key is struck again.
    board_.setFast(fast_);
    for (auto& v : voices_) { v.note.setFastKernels(fast_); v.light.setFast(fast_); v.note.setTruncation(truncation_); v.note.setTensionFloor(tensionFloor_); }
}
void Piano::warmUp() noexcept {
    if (!ready_) return;
    reset();
    sustain(true);
    for (int key : {28, 45, 60, 76, 100}) noteOn(key, 0.9);
    std::array<float, maxBlockFrames> l{}, r{};
    for (int b = 0; b < 40; ++b) {
        blockBegin(maxBlockFrames);
        for (std::uint32_t i = 0; i < maxBlockFrames; ++i) { if (b == 20 && i == 0) for (int key : {33, 52, 67, 88}) noteOn(key, 0.7); blockStep(); }
        blockEnd(l.data(), r.data());
    }
    reset();
    radOn_ = radRequested_; fallbackLeft_ = 0; healthyFrames_ = 0; radStalls_ = 0; backoffFrames_ = static_cast<std::uint64_t>(fs_);
}
void Piano::stopThreads() noexcept {
    radOn_ = radRequested_ = false;
    radThread_.stop();
    pool_.stop();
    // Anything still in the ring was produced by blockStep() and is drained by the caller's blockEnd().
}
bool Piano::startThreads() {
    if (!ready_) return false;
    if (!pool_.start(workerThreads_ > 1 ? workerThreads_ - 1 : 0)) return false;
    if (workerThreads_ >= 1 && shared_ && !radThread_.running() && !radThread_.start(&Piano::radiationPump, this)) return false;
    setActiveWorkers(activeWorkers_);
    return true;
}
void Piano::setActiveWorkers(int n) noexcept {
    // Worker 1 is the radiation thread (block interface), workers 2.. help with the voices.
    if (n < 0) n = 0;
    if (n > workerThreads_) n = workerThreads_;
    activeWorkers_ = n;
    radRequested_ = n >= 1 && radThread_.running();
    radOn_ = radRequested_; fallbackLeft_ = 0; healthyFrames_ = 0; backoffFrames_ = static_cast<std::uint64_t>(fs_);
    pool_.setActive(n > 0 ? n - 1 : 0);
    slotsDirty_ = true;
}
int Piano::defaultWorkerThreads() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1 ? static_cast<int>(std::min(3u, hw - 1)) : 0;
}
double Piano::voiceCost(const Voice& v) const noexcept {
    if (v.struck) return 120.0 + 4.0 * static_cast<double>(v.note.activeModeTotal());
    std::size_t n = 0; for (std::size_t j = 0; j < v.light.strings(); ++j) n += v.light.modeCount(j);
    return 100.0 + 2.0 * static_cast<double>(n);
}
// Sticky assignment of every sounding voice to a pool slot (0 = audio thread): a new voice goes to
// the least loaded slot, so a voice keeps its cache-warm state on one core. Deterministic, serial.
void Piano::assignSlots() noexcept {
    const int slots = pool_.active() + 1;
    if (slotsDirty_) { slotLoad_.fill(0.0); for (auto& v : voices_) v.slot = -1; slotsDirty_ = false; }
    for (int i = 0; i < 88; ++i) {
        auto& v = voices_[i];
        if (!v.active) {
            if (v.slot >= 0) { slotLoad_[v.slot] -= v.slotCost; v.slot = -1; }
            continue;
        }
        if (v.slot >= 0) continue;
        int best = 0;
        for (int s = 1; s < slots; ++s) if (slotLoad_[s] < slotLoad_[best]) best = s;
        v.slot = best; v.slotCost = voiceCost(v); slotLoad_[best] += v.slotCost;
    }
}
void Piano::voiceTask(void* self, int k) {
    auto* p = static_cast<Piano*>(self);
    p->voiceStep(p->ports_[k]);
}
// One port's string update (phase 2). Touches only this voice and reads the board's coupled-mode
// state, so the voices of a sample are independent tasks.
void Piano::voiceStep(int port) noexcept {
    auto& v = voices_[port];
    const double dw = board_.portIncrement(port);
    const bool damped = !v.held && !pedal_ && !v.undamped;
    double force;
    v.faulted = false; v.radiated = false; v.peakMotion = 0;
    if (v.struck) {
        v.bridge.setExternalVelocity(dw * fs_);
        v.note.step();
        auto& s = v.note.segment();
        if (damped && leakyActive_ && leakyRelease_) {
            // 0.5.3 SPEC-OSC difference form with the felt's added damping, per mode.
            for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                const auto off = v.note.modeOffset(j);
                kern::scaleQUPerMode(s.q() + off, s.u() + off, v.leak[j].data(), v.note.activeModes(j));
            }
            v.note.clampPsi();
        } else if (damped) {
            v.release *= releaseStep_;
            for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                const auto off = v.note.modeOffset(j);
                kern::scaleQU(s.q() + off, s.u() + off, releaseStep_, v.note.activeModes(j));
            }
            v.note.clampPsi();
        }
        const double w = v.bridge.wV();
        v.peakMotion = std::abs(w);
        force = v.note.bridgeForceNow(w);
        v.peakForce = std::max(v.peakForce, std::abs(force));
        if (truncation_ > 0 && ++v.sinceTruncation >= 32) { v.sinceTruncation = 0; v.note.truncate(v.peakForce); }
    } else {
        v.light.advance((dw - v.dwPrev) * fs_ * fs_);
        v.dwPrev = dw; v.lightW += dw;
        if (damped) {
            if (leakyActive_) v.light.dampLeak();
            else { v.release *= releaseStep_; v.light.damp(releaseStep_); }
        }
        v.peakMotion = std::abs(v.lightW);
        force = v.light.force(v.lightW);
    }
    if (!std::isfinite(force)) {
        v.faulted = true; v.note.reset(); v.bridge.reset(); v.light.reset();
        v.active = v.struck = v.sympathetic = false; v.force = 0; return;
    }
    v.force = force;
    v.g = board_.lowpassStep(port, force);
    v.radModes = v.struck ? static_cast<unsigned>(kBoardModeCount) : static_cast<unsigned>(kLightRadiatingModes);
    v.radiated = true;
    // Idle-skip qualities judge an unstruck string by its modal force only: the static term -(T/L) w of a
    // string woken while the board was displaced never vanishes (a constant force and a DC drive), which kept
    // such strings awake for ever in 0.5.3. Full keeps the 0.5.3 rule.
    const double sleepForce = (!v.struck && idleSkip_) ? force + v.light.staticStiffness() * v.lightW : force;
    if (std::abs(sleepForce) < sleepForce_ && !(v.struck && v.note.inFlight()) && std::abs(dw) * fs_ < 0.5 * wakeVelocity) ++v.quiet;
    else v.quiet = 0;
    if (v.release < 1e-6 || v.quiet > sleepSamples_) {
        v.active = v.struck = v.sympathetic = false; v.note.reset(); v.bridge.reset(); v.light.reset(); v.force = 0;
    }
}
void Piano::stringsStep() noexcept {
    if (sympathetic_ && (sampleClock_++ & 31u) == 0) wakeSympathetic();
    nPorts_ = 0;
    for (int i = 0; i < 88; ++i) {
        const auto& v = voices_[i];
        if (!v.active) continue;
        ports_[nPorts_++] = i;
        board_.addForce(i, v.force);
    }
    board_.advanceCoupled();
    // Every port's strings: independent tasks with sticky owners; the board's coupled state is read-only.
    const int helpers = pool_.active();
    bool parallel = helpers > 0 && nPorts_ >= 3;
    if (parallel) {
        assignSlots();
        double cost = 0;
        for (int k = 0; k < nPorts_; ++k) { cost += voiceCost(voices_[ports_[k]]); owner_[k] = static_cast<std::uint8_t>(voices_[ports_[k]].slot); }
        parallel = cost > 3000.0;
    }
    if (parallel) pool_.run(&Piano::voiceTask, this, nPorts_, owner_.data());
    else for (int k = 0; k < nPorts_; ++k) voiceStep(ports_[k]);
    for (int k = 0; k < nPorts_; ++k) {
        auto& v = voices_[ports_[k]];
        peakBridgeMotion_ = std::max(peakBridgeMotion_, v.peakMotion);
        if (v.faulted) ++numericalFaults_;
    }
}
void Piano::resetAfterNonFinite() noexcept {
    ++numericalFaults_; board_.reset();
    for (auto& v : voices_) if (v.active) {
        v.note.reset(); v.bridge.reset(); v.light.reset(); v.active = v.struck = v.sympathetic = false; v.force = 0;
    }
}
void Piano::radiateInline(float& left, float& right) noexcept {
    // The drive of the radiating modes from every port in port order (the order of the 0.5.3 radiate
    // calls), the modal update, and the pressure sum in mode order.
    for (int k = 0; k < nPorts_; ++k) {
        const auto& v = voices_[ports_[k]];
        if (v.radiated) board_.accumulateDrive(ports_[k], v.g, 0, v.radModes);
    }
    board_.radiationModes(0, kBoardModeCount);
    double pressureL, pressureR;
    board_.radiationFinish(pressureL, pressureR);
    left = static_cast<float>(0.05 * pressureL);
    right = static_cast<float>(0.05 * pressureR);
    if (!std::isfinite(left) || !std::isfinite(right)) { left = right = 0; resetAfterNonFinite(); }
}
bool Piano::idleStep() noexcept {
    // Called before stepping a frame: skip it when idle.
    return idle_;
}
void Piano::trackIdle() noexcept {
    if (!idleSkip_) return;
    if (nPorts_ != 0) idleCount_ = 0; else if (idleCount_ < (1 << 30)) ++idleCount_;
}
// Only where the radiation is drained (end of a block, or sample()): the board state is then stable.
void Piano::checkIdle() noexcept {
    if (idleSkip_ && !idle_ && idleCount_ >= idleHoldSamples && board_.quiet(idleThreshold)) idle_ = true;
}
void Piano::sampleShared(float& left, float& right) noexcept {
    if (idle_) { left = right = 0; return; }
    stringsStep();
    radiateInline(left, right);
    trackIdle();
    if ((idleCount_ & 255) == 0) checkIdle();
}

// ---- Block interface: the radiation runs on its own thread ----
// Record n of the ring holds the active ports of sample n with their bridge-mass-filtered drive g. The
// producer (audio thread) writes it and publishes `produced_`; whoever holds `radLock_` (the radiation
// thread, or the audio thread at the end of the block) consumes records in order, so the radiation
// state has one writer at a time and the arithmetic is that of radiateInline().
void Piano::radiateRecord(std::uint64_t n) noexcept {
    const std::size_t slot = static_cast<std::size_t>(n & (ringSize - 1));
    const unsigned count = ring_.count[slot];
    const std::uint8_t* keys = ring_.keys.data() + slot * 88;
    const double* g = ring_.g.data() + slot * 88;
    for (unsigned k = 0; k < count; ++k) {
        const unsigned port = keys[k] & 0x7fu;
        board_.accumulateDrive(port, g[k], 0, (keys[k] & 0x80u) ? kLightRadiatingModes : kBoardModeCount);
    }
    board_.radiationModes(0, kBoardModeCount);
    double pressureL, pressureR;
    board_.radiationFinish(pressureL, pressureR);
    float l = static_cast<float>(0.05 * pressureL), r = static_cast<float>(0.05 * pressureR);
    if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0; radFault_.store(true, std::memory_order_relaxed); board_.resetRadiation(); }
    ring_.outL[slot] = l; ring_.outR[slot] = r;
}
bool Piano::pumpRecords(std::uint32_t maxRecords, unsigned holdUs) noexcept {
    std::uint32_t expected = 0;
    if (!radLock_.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) return false;
    std::uint64_t done = consumed_.load(std::memory_order_relaxed);
    const std::uint64_t avail = produced_.load(std::memory_order_acquire);
    std::uint32_t n = 0;
    while (done < avail && n < maxRecords) { radiateRecord(done); ++done; ++n; consumed_.store(done, std::memory_order_release); }
    if (holdUs && n > 0) std::this_thread::sleep_for(std::chrono::microseconds(holdUs));   // tests only
    radLock_.store(0, std::memory_order_release);
    return n > 0;
}
bool Piano::radiationPump(void* self) noexcept {
    auto* p = static_cast<Piano*>(self);
    if (p->consumed_.load(std::memory_order_relaxed) >= p->produced_.load(std::memory_order_acquire)) return false;
    return p->pumpRecords(8, p->testHelperHoldUs_.load(std::memory_order_relaxed));
}
// Advance both counters without producing a record (silent or inline-radiated frame). The helper thread
// stays alive while the radiation runs on the audio thread (Worker threads 0, stall fallback, idle skip), so
// the two stores must not be observable half done: with `produced_` ahead of `consumed_` for an instant the
// helper radiated the stale ring slot (old ports and drives from a previous lap) into the live radiation
// state (0.5.4: ghost sound at note-on that differed from run to run, also with Worker threads 0). Both
// counters change under the radiation lock, which the helper takes before it reads either.
void Piano::publishSkipped(std::uint64_t n) noexcept {
    const bool shared = radThread_.running();
    if (shared) {
        std::uint32_t expected = 0;
        while (!radLock_.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = 0;
#if defined(__x86_64__) || defined(__i386__)
            _mm_pause();
#endif
        }
    }
    produced_.store(n + 1, std::memory_order_release);
    consumed_.store(n + 1, std::memory_order_release);
    if (shared) radLock_.store(0, std::memory_order_release);
}
void Piano::blockBegin(std::uint32_t frames) noexcept {
    blockFrames_ = frames > maxBlockFrames ? maxBlockFrames : frames;
    blockStart_ = produced_.load(std::memory_order_relaxed);
    // Everything is drained here (blockEnd waits; the inline path consumes as it produces), so the
    // radiation may move between the audio thread and the helper at a block boundary.
    if (!radOn_ && radRequested_ && fallbackLeft_ > 0) {
        fallbackLeft_ = fallbackLeft_ > blockFrames_ ? fallbackLeft_ - blockFrames_ : 0;
        if (fallbackLeft_ == 0) radOn_ = true;
    }
    if (adaptive_) blockT0_ = std::chrono::steady_clock::now();
    if (radOn_) radThread_.announce();
}
void Piano::blockStep() noexcept {
    const std::uint64_t n = produced_.load(std::memory_order_relaxed);
    if (!ready_) { const std::size_t s = n & (ringSize - 1); ring_.outL[s] = ring_.outR[s] = 0; publishSkipped(n); return; }
    if (!shared_) {
        const std::size_t s = n & (ringSize - 1);
        sample(ring_.outL[s], ring_.outR[s]);
        publishSkipped(n);
        return;
    }
    if (idle_) {   // nothing sounds: silent frame (sample-accurate wake-up happens in noteOn)
        const std::size_t z = static_cast<std::size_t>(n & (ringSize - 1));
        ring_.outL[z] = ring_.outR[z] = 0;
        publishSkipped(n);
        return;
    }
    stringsStep();
    trackIdle();
    const std::size_t s = static_cast<std::size_t>(n & (ringSize - 1));
    if (!radOn_) {
        radiateInline(ring_.outL[s], ring_.outR[s]);
        publishSkipped(n);
        return;
    }
    std::uint8_t* keys = ring_.keys.data() + s * 88;
    double* g = ring_.g.data() + s * 88;
    unsigned count = 0;
    for (int k = 0; k < nPorts_; ++k) {
        const auto& v = voices_[ports_[k]];
        if (!v.radiated) continue;
        keys[count] = static_cast<std::uint8_t>(ports_[k] | (v.radModes == kLightRadiatingModes ? 0x80 : 0));   // as radiateInline
        g[count++] = v.g;
    }
    ring_.count[s] = static_cast<std::uint8_t>(count);
    produced_.store(n + 1, std::memory_order_release);
}
void Piano::blockEnd(float* left, float* right) noexcept {
    const std::uint64_t end = produced_.load(std::memory_order_relaxed);
    // Finish the radiation of the block: helping is allowed whenever the lock is free; wait otherwise
    // (the holder is at most a few records from done).
    // 0.5.4.1: a helper that is descheduled while it holds the lock (a loaded desktop, a helper that
    // could not get the audio thread's real-time class) used to hold the block until it ran again, a few
    // to tens of milliseconds, each time it happened. Measure the wait; after a stall the radiation of
    // the following blocks runs on this thread (bit-identical output, only the thread differs), and the
    // helper is tried again after a back-off (1 s, doubling to 32 s, reset after 20 s without a stall).
    bool waited = false;
    std::chrono::steady_clock::time_point since{};
    unsigned spins = 0;
    while (consumed_.load(std::memory_order_acquire) < end) {
        if (!pumpRecords(~0u)) {
            if (!waited) { waited = true; since = std::chrono::steady_clock::now(); }
#if defined(__x86_64__) || defined(__i386__)
            _mm_pause();
#endif
            if (radOn_ && stallFallback_ && (++spins & 63u) == 0 &&
                std::chrono::steady_clock::now() - since > std::chrono::nanoseconds(kStallNs)) {
                radOn_ = false; ++radStalls_;
                fallbackLeft_ = backoffFrames_ ? backoffFrames_ : static_cast<std::uint64_t>(fs_);
                backoffFrames_ = std::min<std::uint64_t>(fallbackLeft_ * 2, static_cast<std::uint64_t>(32 * fs_));
                healthyFrames_ = 0;
            }
        }
    }
    if (radOn_) {
        healthyFrames_ += blockFrames_;
        if (healthyFrames_ > static_cast<std::uint64_t>(20 * fs_)) backoffFrames_ = static_cast<std::uint64_t>(fs_);
    }
    if (radFault_.exchange(false, std::memory_order_relaxed)) resetAfterNonFinite();
    checkIdle();
    governBlock();
    for (std::uint32_t i = 0; i < blockFrames_; ++i) {
        const std::size_t s = static_cast<std::size_t>((blockStart_ + i) & (ringSize - 1));
        if (left) left[i] = ring_.outL[s];
        if (right) right[i] = ring_.outR[s];
    }
}
void Piano::blockEnd(double* left, double* right) noexcept {
    std::array<float, maxBlockFrames> l, r;
    blockEnd(l.data(), r.data());
    for (std::uint32_t i = 0; i < blockFrames_; ++i) {
        if (left) left[i] = l[i];
        if (right) right[i] = r[i];
    }
}
}
