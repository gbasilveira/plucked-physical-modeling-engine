// SPDX-License-Identifier: MIT
// Key, sustain and sostenuto logic (A6-R3, A6-R4, A6-R5). See pme/dampers/controller.hpp.
#include "pme/dampers/controller.hpp"

#include <bit>
#include <cmath>

namespace pme::dmp {

namespace {
double clamp01(double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
}  // namespace

Status DamperController::configure(double fs, double p0, double p1) noexcept {
    if (!(fs > 0.0) || !std::isfinite(fs) || validateHalfPedal(p0, p1) != Status::Ok) {
        return Status::InvalidArgument;
    }
    fs_ = fs;
    p0_ = p0;
    p1_ = p1;
    sustain_ = 0.0;
    lPed_ = 0.0;
    sostenutoDown_ = false;
    slots_ = {};
    movingMask_ = {};
    return Status::Ok;
}

Status DamperController::setKey(int key, const std::optional<DamperParams>& params) noexcept {
    if (!validKey(key) || !(fs_ > 0.0)) {
        return Status::InvalidArgument;
    }
    if (params.has_value() && validateParams(*params) != Status::Ok) {
        return Status::InvalidArgument;
    }
    Slot& s = slots_[static_cast<std::size_t>(key)];
    s = Slot{};
    if (params.has_value() && params->hasDamper) {
        s.damped = true;
        s.follower.configure(*params, fs_, 1.0);
        retarget(key);
        s.follower.setValue(s.follower.target());  // configuration: no ramp
    } else {
        s.follower.setValue(0.0);  // SPEC-DMP "Undamped notes": l = 1, e = 0
    }
    const std::uint64_t bit = std::uint64_t{1} << (static_cast<unsigned>(key) & 63U);
    movingMask_[static_cast<std::size_t>(key) >> 6U] &= ~bit;
    return Status::Ok;
}

void DamperController::markMoving(int key) noexcept {
    const std::uint64_t bit = std::uint64_t{1} << (static_cast<unsigned>(key) & 63U);
    std::uint64_t& word = movingMask_[static_cast<std::size_t>(key) >> 6U];
    if (slots_[static_cast<std::size_t>(key)].follower.moving()) {
        word |= bit;
    } else {
        word &= ~bit;
    }
}

void DamperController::retarget(int key) noexcept {
    Slot& s = slots_[static_cast<std::size_t>(key)];
    if (!s.damped) {
        return;
    }
    // SPEC-DMP lift fraction: l = max(l_key, l_ped), e_target = (1 - l)^eta_d
    const double l = liftFraction(effectiveLKey(s), lPed_);
    s.follower.setTarget(engagementTarget(l, s.follower.etaD()), s.nuRel);
    markMoving(key);
}

void DamperController::retargetAll() noexcept {
    for (int key = 0; key < kKeyCount; ++key) {
        retarget(key);
    }
}

void DamperController::noteOn(int key) noexcept {
    if (!validKey(key)) {
        return;
    }
    slots_[static_cast<std::size_t>(key)].lKey = 1.0;  // SPEC-DMP: l_key = 1 while the key is down
    retarget(key);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): key then release velocity
void DamperController::noteOff(int key, double releaseVelocity) noexcept {
    if (!validKey(key)) {
        return;
    }
    Slot& s = slots_[static_cast<std::size_t>(key)];
    s.lKey = 0.0;
    s.nuRel = std::isfinite(releaseVelocity) ? clamp01(releaseVelocity) : 0.0;
    retarget(key);
}

void DamperController::keyDepth(int key, double depth) noexcept {
    if (!validKey(key) || !std::isfinite(depth)) {
        return;
    }
    slots_[static_cast<std::size_t>(key)].lKey = clamp01(depth);
    retarget(key);
}

void DamperController::setSustain(double depth) noexcept {
    if (!std::isfinite(depth)) {
        return;
    }
    sustain_ = depth;
    lPed_ = pedalLift(depth, p0_, p1_);
    retargetAll();
}

void DamperController::setSostenuto(double depth) noexcept {
    if (!std::isfinite(depth)) {
        return;
    }
    const bool down = depth >= kSostenutoDown;  // SI-0016: no hysteresis
    if (down == sostenutoDown_) {
        return;
    }
    sostenutoDown_ = down;
    for (Slot& s : slots_) {
        if (!s.damped) {
            continue;  // keys without a damper are not affected
        }
        // SI-0016: latch every key with l_key >= 0.5 at the sample the pedal goes down;
        // release every latch when the pedal falls below 0.5.
        s.latched = down && s.lKey >= kSostenutoLatchLift;
    }
    retargetAll();
}

bool DamperController::advance() noexcept {
    bool any = false;
    for (std::size_t w = 0; w < movingMask_.size(); ++w) {
        std::uint64_t word = movingMask_[w];
        while (word != 0U) {
            const auto bit = static_cast<unsigned>(std::countr_zero(word));
            word &= word - 1U;
            Slot& s = slots_[w * 64U + bit];
            if (s.follower.step()) {
                any = true;
            } else {
                movingMask_[w] &= ~(std::uint64_t{1} << bit);
            }
        }
    }
    return any;
}

bool DamperController::advance(int n) noexcept {
    bool any = anyMoving();
    for (int i = 0; i < n; ++i) {
        any = advance();
    }
    return any;
}

double DamperController::engagement(int key) const noexcept {
    return validKey(key) ? slots_[static_cast<std::size_t>(key)].follower.value() : 0.0;
}

double DamperController::target(int key) const noexcept {
    return validKey(key) ? slots_[static_cast<std::size_t>(key)].follower.target() : 0.0;
}

double DamperController::projected(int key, int n) const noexcept {
    return validKey(key) ? slots_[static_cast<std::size_t>(key)].follower.projected(n) : 0.0;
}

double DamperController::tauDown(int key) const noexcept {
    return validKey(key) ? slots_[static_cast<std::size_t>(key)].follower.tauDown() : 0.0;
}

bool DamperController::hasDamper(int key) const noexcept {
    return validKey(key) && slots_[static_cast<std::size_t>(key)].damped;
}

KeyLift DamperController::lift(int key) const noexcept {
    KeyLift out;
    out.lPed = lPed_;
    if (!validKey(key)) {
        return out;
    }
    const Slot& s = slots_[static_cast<std::size_t>(key)];
    if (!s.damped) {
        out.lKey = 1.0;  // SPEC-DMP "Undamped notes": always behaves as l = 1
        out.l = 1.0;
        return out;
    }
    out.lKey = effectiveLKey(s);
    out.latched = s.latched;
    out.l = liftFraction(out.lKey, out.lPed);
    return out;
}

}  // namespace pme::dmp
