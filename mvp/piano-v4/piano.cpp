// SPDX-License-Identifier: MIT
#include "piano.hpp"
#include "preset.hpp"
#include "bridge_model.hpp"
#include "string_loss.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace piano_mvp_v4 {
bool Piano::prepare(double fs) {
    ready_ = false;
    if (!std::isfinite(fs) || fs < 22050 || fs > 192000) return false;
    fs_ = fs;
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
    ready_ = true; reset(); return true;
}
void Piano::reset() noexcept {
    for (auto& v : voices_) {
        v.note.reset(); v.note.clearDiagnostics(); v.bridge.reset(); v.light.reset(); v.lightW = v.dwPrev = 0;
        v.active = v.held = v.struck = v.sympathetic = false; v.age = v.quiet = 0; v.release = 1; v.force = 0;
    }
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
    auto& v = voices_[key - 21];
    if (!v.struck && activeVoices() >= voiceLimit_) {
        Voice* oldest = nullptr;
        for (auto& x : voices_) if (x.active && x.struck && (!oldest || x.age < oldest->age)) oldest = &x;
        if (oldest) {
            ++voiceSteals_;
            oldest->active = oldest->held = oldest->struck = oldest->sympathetic = false;
            oldest->note.reset(); oldest->bridge.reset(); oldest->force = 0;
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
    v.active = v.held = v.struck = true; v.sympathetic = false; v.release = 1; v.age = ++clock_; v.quiet = 0;
}
void Piano::noteOff(int key) noexcept {
    if (key >= 21 && key <= 108) voices_[key - 21].held = false;
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
void Piano::sampleShared(float& left, float& right) noexcept {
    if (sympathetic_ && (sampleClock_++ & 31u) == 0) wakeSympathetic();
    std::array<int, 88> ports; int count = 0;
    for (int i = 0; i < 88; ++i) {
        const auto& v = voices_[i];
        if (!v.active) continue;
        ports[count++] = i;
        board_.addForce(i, v.force);
    }
    board_.advanceCoupled();
    for (int k = 0; k < count; ++k) {
        auto& v = voices_[ports[k]];
        const double dw = board_.portIncrement(ports[k]);
        const bool damped = !v.held && !pedal_ && !v.undamped;
        double force;
        if (v.struck) {
            v.bridge.setExternalVelocity(dw * fs_);
            v.note.step();
            auto& s = v.note.segment();
            if (damped && leakyActive_ && leakyRelease_) {
                // 0.5.3 SPEC-OSC difference form with the felt's added damping, per mode.
                for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                    const auto off = v.note.modeOffset(j);
                    for (std::size_t n = 0; n < v.note.modeCount(j); ++n) {
                        s.q()[off+n] *= v.leak[j][n]; s.u()[off+n] *= v.leak[j][n];
                    }
                }
                v.note.clampPsi();
            } else if (damped) {
                v.release *= releaseStep_;
                for (std::size_t j = 0; j < v.note.stringCount(); ++j) {
                    const auto off = v.note.modeOffset(j);
                    for (std::size_t n = 0; n < v.note.modeCount(j); ++n) {
                        s.q()[off+n] *= releaseStep_; s.u()[off+n] *= releaseStep_;
                    }
                }
                v.note.clampPsi();
            }
            const double w = v.bridge.wV();
            peakBridgeMotion_ = std::max(peakBridgeMotion_,std::abs(w));
            force = 0;
            for (std::size_t j = 0; j < v.note.stringCount(); ++j)
                force += pme::bridge::bridgeForce<double>(v.terms[j], s.q() + v.note.modeOffset(j), w);
        } else {
            v.light.advance((dw - v.dwPrev) * fs_ * fs_);
            v.dwPrev = dw; v.lightW += dw;
            if (damped) {
                if (leakyActive_) v.light.dampLeak();
                else { v.release *= releaseStep_; v.light.damp(releaseStep_); }
            }
            peakBridgeMotion_ = std::max(peakBridgeMotion_,std::abs(v.lightW));
            force = v.light.force(v.lightW);
        }
        if (!std::isfinite(force)) {
            ++numericalFaults_; v.note.reset(); v.bridge.reset(); v.light.reset();
            v.active = v.struck = v.sympathetic = false; v.force = 0; continue;
        }
        v.force = force;
        board_.radiate(ports[k], force, v.struck ? kBoardModeCount : kLightRadiatingModes);
        if (std::abs(force) < 1e-7 && !(v.struck && v.note.inFlight()) && std::abs(dw) * fs_ < 0.5 * wakeVelocity) ++v.quiet;
        else v.quiet = 0;
        if (v.release < 1e-6 || v.quiet > static_cast<std::uint64_t>(fs_)) {
            v.active = v.struck = v.sympathetic = false; v.note.reset(); v.bridge.reset(); v.light.reset(); v.force = 0;
        }
    }
    double pressureL, pressureR;
    board_.radiation(pressureL, pressureR);
    left = static_cast<float>(0.05 * pressureL);
    right = static_cast<float>(0.05 * pressureR);
    if (!std::isfinite(left) || !std::isfinite(right)) {
        ++numericalFaults_; left = right = 0; board_.reset();
        for (auto& v : voices_) if (v.active) {
            v.note.reset(); v.bridge.reset(); v.light.reset(); v.active = v.struck = v.sympathetic = false; v.force = 0;
        }
    }
}
}
