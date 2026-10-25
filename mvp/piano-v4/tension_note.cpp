// SPDX-License-Identifier: MIT
// 0.5.2 TensionStruckNote: copy-and-adapt of pme::exciters::StruckNote (engine/src/exciters/hammer.cpp,
// "StruckNote" section). See tension_note.hpp for the differences. The sections that are unchanged
// keep the engine's comments (SPEC-EXEC per-sample order steps 2 to 7).
#include "tension_note.hpp"
#include <algorithm>
#include <cmath>
#include "pme/bridge/terms.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/string/string_model.hpp"

namespace piano_mvp_v4 {
using namespace pme;
using namespace pme::exciters;
namespace {
constexpr double kVeryNegative = -1.0e300;
}

Status TensionStruckNote::configure(const StruckNoteDesc& desc, double fs, bridge::BridgePoint<double>* bridge,
                                    std::span<const double> ea) {
    const std::size_t ns = desc.strings.size();
    if (ns < 1 || ns > kMaxStruckStrings || !(fs > 0) || validate(desc.hammer) != Status::Ok ||
        validate(desc.unaCorda) != Status::Ok || (desc.approachSign != 1.0 && desc.approachSign != -1.0) ||
        desc.shank.has_value())
        return Status::InvalidArgument;
    std::size_t totalModes = 0;
    for (const StringModes* m : desc.strings) {
        if (m == nullptr || m->k.empty() || m->fs != fs) return Status::InvalidArgument;
        totalModes += m->k.size();
    }
    const double dt = 1.0 / fs;
    const std::size_t nz = totalModes + 1;
    std::vector<String> strings(ns);
    std::vector<StringSlice> slices(ns);
    std::vector<VerticalTensionPotential::Slice> tslices;
    std::size_t off = 0;
    for (std::size_t s = 0; s < ns; ++s) {
        const StringModes& m = *desc.strings[s];
        String& st = strings[s];
        st.off = off; st.n = m.k.size(); st.rhoA = m.rhoA; st.tOverL = m.tension / m.L;
        std::vector<double> g, aV, aHd;
        if (Status e = bridge::liftingOverlaps(m, desc.end, g); e != Status::Ok) return e;
        if (Status e = bridge::forceCoefficients(m, desc.end, bridge::Polarisation::Vertical, aV); e != Status::Ok) return e;
        if (Status e = bridge::forceCoefficients(m, desc.end, bridge::Polarisation::Horizontal, aHd); e != Status::Ok) return e;
        const std::size_t n = st.n;
        st.aV.resize(n); st.aH.resize(n); st.forceCoef.resize(n); st.g.resize(n);
        st.beta.resize(n); st.kappa.resize(n); st.gamma.resize(n);
        st.qH.assign(n, 0.0); st.uH.assign(n, 0.0); st.fH.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            st.aV[i] = aV[i]; st.aH[i] = aHd[i]; st.g[i] = g[i]; st.forceCoef[i] = m.rhoA * g[i];
            // SPEC-OSC Difference form of the horizontal polarisation (as bridge::ModalSegment).
            const double sd = m.oscH[i].sigmaHat * dt, denom = 1.0 + sd;
            st.beta[i] = (1.0 - sd) / denom;
            st.kappa[i] = m.oscH[i].omegaHat2 * dt * dt / denom;
            st.gamma[i] = dt * dt / denom;
        }
        slices[s].offset = off;
        slices[s].phiBar.assign(n, 0.0F);
        // SPEC-STR Forces and observations: raised-cosine window of width b_H at x_c = beta_c L.
        string::windowedShapes(m, desc.hammer.strikeRatio * m.L,
            desc.hammer.width == 0.0 ? string::WindowKind::Point : string::WindowKind::RaisedCosine,
            desc.hammer.width, slices[s].phiBar);
        // SPEC-GEO: k2 = k_n^2, c = (EA - T) / (2 L).
        if (s < ea.size() && ea[s] > m.tension) {
            VerticalTensionPotential::Slice t;
            t.offset = off; t.c = (ea[s] - m.tension) / (2.0 * m.L);
            t.k2.resize(n);
            for (std::size_t i = 0; i < n; ++i) t.k2[i] = m.k[i] * m.k[i];
            tslices.push_back(std::move(t));
        }
        off += n;
    }
    if (!tslices.empty() && tslices.size() != ns) return Status::InvalidArgument;  // all or none

    nl::SavSegment<double> seg;
    if (const Status e = seg.configure(totalModes, 1, nl::kMaxDashpots, fs, desc.sav); e != Status::Ok) return e;
    for (std::size_t s = 0; s < ns; ++s) {
        const StringModes& m = *desc.strings[s];
        for (std::size_t i = 0; i < strings[s].n; ++i) {
            const double sd = m.oscV[i].sigmaHat * dt, denom = 1.0 + sd;
            osc::OscCoeffsF64 c;
            c.sigmaHatDt = sd; c.omegaHat2Dt2 = m.oscV[i].omegaHat2 * dt * dt;
            c.beta = (1.0 - sd) / denom; c.kappa = m.oscV[i].omegaHat2 * dt * dt / denom;
            c.gamma = dt * dt / denom; c.omegaHat2 = m.oscV[i].omegaHat2; c.sigmaHat = m.oscV[i].sigmaHat;
            if (const Status e = seg.setCoordinate(strings[s].off + i, c, m.rhoA); e != Status::Ok) return e;
        }
    }
    if (const Status e = seg.setLumped(totalModes, desc.hammer.mass); e != Status::Ok) return e;
    P potential;
    if (const Status e = potential.configure(std::move(slices), totalModes, 0.0, desc.approachSign); e != Status::Ok) return e;
    HammerFelt<double> felt;
    if (const Status e = felt.configure(desc.hammer, potential, nz, fs); e != Status::Ok) return e;
    StrikeMap map;
    if (const Status e = strikeMap(desc.hammer, desc.unaCorda, desc.pedal, ns, 0.0, map); e != Status::Ok) return e;
    potential.setStrikeMap(map);

    seg_ = std::move(seg); potential_ = std::move(potential); felt_ = std::move(felt);
    tension_ = VerticalTensionPotential{}; tension_.configure(std::move(tslices));
    bridge_ = bridge; strings_ = std::move(strings);
    force_.assign(nz, 0.0);
    hammer_ = desc.hammer; una_ = desc.unaCorda; pedal_ = desc.pedal;
    hammerIdx_ = totalModes; sE_ = desc.approachSign; dt_ = dt; shift_ = 0.0;
    inFlight_ = false; prevMax_ = kVeryNegative; prevSample_ = 0; contactOpen_ = false;
    peak_ = 0.0; sample_ = 0; releasedEnergy_ = 0.0; resetEnergyRemoved_ = 0.0;
    diag_ = HammerDiagnostics{};
    return Status::Ok;
}

void TensionStruckNote::reset() noexcept {
    seg_.resetState();
    for (String& st : strings_) {
        std::fill(st.qH.begin(), st.qH.end(), 0.0);
        std::fill(st.uH.begin(), st.uH.end(), 0.0);
    }
    potential_.setActive(false);
    felt_.reset();
    inFlight_ = false; contactOpen_ = false; prevMax_ = kVeryNegative; prevSample_ = sample_;
}
double TensionStruckNote::headPosition() const noexcept {
    return potential_.headPosition(std::span<const double>(seg_.q(), seg_.size()));
}
double TensionStruckNote::headVelocity() const noexcept { return seg_.u()[hammerIdx_] / dt_; }
Status TensionStruckNote::setShift(double shift) noexcept {
    StrikeMap map;
    if (const Status e = strikeMap(hammer_, una_, pedal_, strings_.size(), shift, map); e != Status::Ok) return e;
    potential_.setStrikeMap(map);
    shift_ = std::clamp(shift, 0.0, 1.0);
    return Status::Ok;
}
Status TensionStruckNote::launch(double nu, double shift) noexcept {
    double vH = 0.0;
    if (const Status e = launchVelocity(nu, shift, hammer_, una_, pedal_, vH); e != Status::Ok) return e;
    if (const Status e = setShift(shift); e != Status::Ok) return e;
    double* const q = seg_.q();
    double* const u = seg_.u();
    const std::span<const double> z(q, seg_.size());
    const double du = vH * dt_;  // SPEC-HAM Velocity map
    const bool continues = inFlight_ && potential_.maxPenetration(z) >= -hammer_.dCheck;
    if (continues) {
        u[hammerIdx_] = du;  // SPEC-HAM Repetition
    } else {
        double y = kVeryNegative;
        for (std::size_t s = 0; s < strings_.size(); ++s)
            if (potential_.map().weight[s] > 0.0) y = std::max(y, potential_.stringPosition(z, s));
        q[hammerIdx_] = y - du;
        u[hammerIdx_] = du;
        felt_.reset();
        inFlight_ = true;
        potential_.setActive(true);
    }
    prevMax_ = potential_.maxPenetration(z);
    prevSample_ = sample_;
    ++diag_.strikes;
    return Status::Ok;
}
void TensionStruckNote::closeContact(double endSample) noexcept {
    const double dur = (endSample - contactStart_) * dt_;
    diag_.lastContactDuration = dur; diag_.lastPeakForce = peak_; diag_.totalContactTime += dur;
    ++diag_.contacts; contactOpen_ = false;
}
void TensionStruckNote::trackContact(double maxDelta) noexcept {
    const auto n = static_cast<double>(sample_);
    if (sample_ > prevSample_) {
        const double p = prevMax_;
        const auto p0 = static_cast<double>(prevSample_);
        if (!contactOpen_ && maxDelta > 0.0 && p <= 0.0) {
            contactStart_ = p0 + (n - p0) * (-p) / (maxDelta - p);
            contactOpen_ = true; peak_ = 0.0;
        } else if (contactOpen_ && maxDelta <= 0.0 && p > 0.0) {
            closeContact(p0 + (n - p0) * p / (p - maxDelta));
        }
    } else if (!contactOpen_ && maxDelta > 0.0) {
        contactStart_ = n; contactOpen_ = true; peak_ = 0.0;
    }
    prevMax_ = maxDelta; prevSample_ = sample_;
}
void TensionStruckNote::release() noexcept {
    // SPEC-HAM Release and check: the hammer leaves the coordinate set, frozen.
    double* const q = seg_.q();
    double* const u = seg_.u();
    for (std::size_t i = hammerIdx_; i < seg_.size(); ++i)
        releasedEnergy_ += seg_.mass()[i] * osc::modeEnergy(seg_.omegaHat2()[i], seg_.qPrev()[i], q[i], u[i], dt_);
    u[hammerIdx_] = 0.0;
    if (contactOpen_) closeContact(static_cast<double>(sample_));
    inFlight_ = false;
    potential_.setActive(false);
    felt_.reset();
}
void TensionStruckNote::clampPsi() noexcept {
    if (tension_.empty()) return;
    const std::span<const double> z(seg_.q(), seg_.size());
    const double v = potential_.value(z) + tension_.value(z);
    const double star = std::sqrt(2.0 * (v + seg_.params().eps0));
    if (std::abs(seg_.psi()) > star) seg_.setPsi(seg_.psi() < 0 ? -star : star);
}

void TensionStruckNote::step() noexcept {
    const std::size_t nz = seg_.size();
    std::fill(force_.begin(), force_.end(), 0.0);
    double accH = 0.0;
    if (bridge_ != nullptr) {
        // SPEC-EXEC step 2: F_b,s from q^n and w^n, per string and polarisation.
        const double wv = bridge_->wV();
        const double wh = bridge_->wH();
        for (const String& st : strings_)
            bridge_->addForce(
                bridge::bridgeForce<double, double>(std::span<const double>(st.aV), st.tOverL, seg_.q() + st.off, wv),
                bridge::bridgeForce<double, double>(std::span<const double>(st.aH), st.tOverL, st.qH.data(), wh));
        // SPEC-EXEC steps 3 and 4.
        bridge_->advance();
        const double accV = bridge_->accelV();
        accH = bridge_->accelH();
        // SPEC-EXEC step 5: explicit modal forces -rho A g_n delta_tt w^n.
        for (const String& st : strings_)
            for (std::size_t i = 0; i < st.n; ++i) force_[st.off + i] = -st.forceCoef[i] * accV;
    }

    const std::span<const double> z(seg_.q(), nz);
    double vNow = 0.0, gradH = 0.0, psiBefore = 0.0;
    if (inFlight_) {
        trackContact(potential_.maxPenetration(z));
        felt_.update(potential_, z, std::span<double>(force_));
        vNow = potential_.value(z);
        gradH = potential_.hammerGradient(z);
        psiBefore = seg_.psi();
    }
    // SPEC-GEO tension modulation joins the segment's psi while V_KC is above the floor.
    double vTension = 0.0;
    if (!tension_.empty()) {
        vTension = tension_.value(z);
        if (!(vTension > floor_)) vTension = 0.0;
    }
    std::array<const nl::Potential<double>*, 2> pots{&potential_, &tension_};
    std::span<const nl::Dashpot<double>> dashpots = felt_.dashpots();
    std::size_t nPots = 1;
    if (vTension > 0.0) {
        nPots = 2;
        // The SAV gradient is no longer parallel to a single felt direction: no merged column.
        for (std::size_t j = 0; j < dashpots.size(); ++j) { dash_[j] = dashpots[j]; dash_[j].alongGradient = false; }
        dashpots = std::span<const nl::Dashpot<double>>(dash_.data(), dashpots.size());
    }
    // SPEC-EXEC step 6: the low-rank solve over the vertical modes of every struck string and the hammer.
    const double psiPre = seg_.psi();
    const std::uint32_t resetsPre = seg_.counters().resets;
    nl::savStep<double>(seg_, std::span<const nl::Potential<double>* const>(pots.data(), nPots), dashpots,
                        std::span<const double>(force_));
    if (seg_.counters().resets != resetsPre)
        resetEnergyRemoved_ += 0.5 * (psiPre * psiPre - seg_.psi() * seg_.psi());
    if (inFlight_) {
        if (vNow > 0.0) {
            const double gH = gradH / std::sqrt(2.0 * (vNow + vTension + seg_.params().eps0));
            peak_ = std::max(peak_, std::abs(gH) * 0.5 * (psiBefore + seg_.psi()));
        }
        const std::span<const double> zNew(seg_.q(), nz);
        if (potential_.maxPenetration(zNew) < -hammer_.dCheck && headVelocity() < 0.0) release();
    }
    // SPEC-EXEC step 7: the horizontal polarisations (linear), driven by the bridge term only.
    if (bridge_ != nullptr) {
        for (String& st : strings_) {
            for (std::size_t i = 0; i < st.n; ++i) st.fH[i] = -st.g[i] * accH;
            osc::stepModes<double, double>(st.qH.data(), st.uH.data(), st.beta.data(), st.kappa.data(),
                                           st.gamma.data(), st.fH.data(), st.n);
        }
    }
    ++sample_;
}
}
