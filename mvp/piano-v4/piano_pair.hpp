// SPDX-License-Identifier: MIT
// Piano 0.7c: the plug-ins' Body setting (Classic 0.6 | Grand 0.7) at run time. Both bodies are prepared at activation (a piano is
// about 12 MB and prepares in well under a second); the audio thread switches by routing note-ons to the other piano. The piano
// being left keeps receiving pedal and note-off events and is rendered until it is idle (or a timeout), so sounding notes die out
// naturally instead of being cut; its output is mixed in. Nothing is allocated or locked in the real-time methods.
#pragma once
#include <algorithm>
#include <array>
#include "piano.hpp"

namespace piano_mvp_v4 {
class PianoPair {
public:
    static constexpr std::uint32_t maxBlockFrames = Piano::maxBlockFrames;
    static constexpr double retireSeconds = 8.0;       // a left piano that never reports idle (Full) is dropped after this
    // ---- non-audio
    bool setWorkerThreads(int n) noexcept { return classic_.setWorkerThreads(n) && grand_.setWorkerThreads(n); }
    // Prepares the Classic piano, and the Grand piano when the bundled table is present (grandAvailable()).
    bool prepare(double fs) {
        fs_ = fs; classic_.setBody(Piano::Body::Classic); grand_.setBody(Piano::Body::Grand);
        if (!classic_.prepare(fs)) return false;
        grandOk_ = grand_.prepare(fs);
        active_ = &classic_; left_ = nullptr; body_ = 0;
        return true;
    }
    bool grandAvailable() const noexcept { return grandOk_; }
    void stopThreads() noexcept { classic_.stopThreads(); if (grandOk_) grand_.stopThreads(); }
    bool startThreads() { return classic_.startThreads() && (!grandOk_ || grand_.startThreads()); }
    void warmUp() noexcept { classic_.warmUp(); if (grandOk_) grand_.warmUp(); }
    void reset() noexcept { classic_.reset(); if (grandOk_) grand_.reset(); left_ = nullptr; pedal_ = soft_ = false; }
    // ---- settings (audio thread; cheap)
    void setQuality(Piano::Quality q) noexcept { classic_.setQuality(q); if (grandOk_) grand_.setQuality(q); }
    void setActiveWorkers(int n) noexcept { classic_.setActiveWorkers(n); if (grandOk_) grand_.setActiveWorkers(n); }
    void setVoices(int n) noexcept { classic_.setVoices(n); if (grandOk_) grand_.setVoices(n); }
    void setImperfection(double a) noexcept { classic_.setImperfection(a); if (grandOk_) grand_.setImperfection(a); }
    void setAdaptive(bool on) noexcept { classic_.setAdaptive(on); if (grandOk_) grand_.setAdaptive(on); }
    void setRadiation(Piano::Radiation r) noexcept { classic_.setRadiation(r); }       // the 0.6 stage (Body Classic)
    void setGrandLid(int lid) noexcept { grand_.setGrandLid(lid); }
    void setGrandMic(int mic) noexcept { grand_.setGrandMic(mic); }
    // 0 Classic, 1 Grand. A Grand request is ignored when the table is missing. Takes effect at the next note-on.
    void setBody(int body) noexcept {
        const int want = (body == 1 && grandOk_) ? 1 : 0;
        if (want == body_) return;
        Piano* next = want ? &grand_ : &classic_;
        left_ = active_; leftFrames_ = 0;
        if (left_ == next) left_ = nullptr;
        next->sustain(pedal_); next->softPedal(soft_);
        active_ = next; body_ = want;
    }
    int body() const noexcept { return body_; }
    Piano& active() noexcept { return *active_; }
    Piano& classic() noexcept { return classic_; }
    Piano& grand() noexcept { return grand_; }
    bool leaving() const noexcept { return left_ != nullptr; }
    // ---- MIDI
    void noteOn(int key, double velocity) noexcept { active_->noteOn(key, velocity); }
    void noteOff(int key) noexcept { active_->noteOff(key); if (left_) left_->noteOff(key); }
    void sustain(bool down) noexcept { pedal_ = down; active_->sustain(down); if (left_) left_->sustain(down); }
    void softPedal(bool down) noexcept { soft_ = down; active_->softPedal(down); if (left_) left_->softPedal(down); }
    void allNotesOff() noexcept { active_->allNotesOff(); if (left_) left_->allNotesOff(); }
    // ---- audio
    void blockBegin(std::uint32_t frames) noexcept {
        if (left_ && (left_->idle() || leftFrames_ > static_cast<std::uint64_t>(retireSeconds * fs_))) { left_->reset(); left_ = nullptr; }
        frames_ = std::min(frames, maxBlockFrames);
        active_->blockBegin(frames);
        if (left_) left_->blockBegin(frames);
    }
    void blockStep() noexcept { active_->blockStep(); if (left_) left_->blockStep(); }
    void blockEnd(float* left, float* right) noexcept {
        active_->blockEnd(left, right);
        if (left_) {
            std::array<float, maxBlockFrames> l, r;
            left_->blockEnd(l.data(), r.data());
            for (std::uint32_t i = 0; i < frames_; ++i) { if (left) left[i] += l[i]; if (right) right[i] += r[i]; }
            leftFrames_ += frames_;
        }
    }
    void blockEnd(double* left, double* right) noexcept {
        std::array<float, maxBlockFrames> l, r;
        blockEnd(l.data(), r.data());
        for (std::uint32_t i = 0; i < frames_; ++i) { if (left) left[i] = l[i]; if (right) right[i] = r[i]; }
    }
    // Single-sample interface (tests).
    void sample(float& left, float& right) noexcept {
        active_->sample(left, right);
        if (left_) { float l, r; left_->sample(l, r); left += l; right += r; ++leftFrames_;
            if (left_->idle() || leftFrames_ > static_cast<std::uint64_t>(retireSeconds * fs_)) { left_->reset(); left_ = nullptr; } }
    }
private:
    Piano classic_, grand_;
    Piano* active_{&classic_}; Piano* left_{};
    bool grandOk_{}, pedal_{}, soft_{};
    int body_{0}; double fs_{48000}; std::uint32_t frames_{}; std::uint64_t leftFrames_{};
};
}
