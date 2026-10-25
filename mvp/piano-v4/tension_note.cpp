// SPDX-License-Identifier: MIT
// 0.5.2 TensionStruckNote: copy-and-adapt of pme::exciters::StruckNote (engine/src/exciters/hammer.cpp,
// "StruckNote" section). See tension_note.hpp for the differences. The sections that are unchanged
// keep the engine's comments (SPEC-EXEC per-sample order steps 2 to 7).
#include "tension_note.hpp"
#include "kernels.hpp"
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

double VerticalTensionPotential::sum(std::span<const double> z, std::size_t s) const noexcept {
    const auto& sl = slices_[s];
    const std::size_t n = limits_ ? std::min(limits_[s], sl.k2.size()) : sl.k2.size();
    if (fast_) return kern::sumSqFast(sl.k2.data(), z.data() + sl.offset, n);
    double acc = 0;
    for (std::size_t i = 0; i < n; ++i) acc += sl.k2[i] * z[sl.offset + i] * z[sl.offset + i];
    return acc;
}
double VerticalTensionPotential::value(std::span<const double> z) const noexcept {
    double v = 0;
    for (std::size_t s = 0; s < slices_.size(); ++s) { const double S = sum(z, s); v += 0.25 * slices_[s].c * S * S; }
    return v;
}
void VerticalTensionPotential::gradient(std::span<const double> z, std::span<double> grad) const noexcept {
    for (auto& g : grad) g = 0;
    for (std::size_t s = 0; s < slices_.size(); ++s) {
        const auto& sl = slices_[s];
        const double dT = sl.c * sum(z, s);
        const std::size_t n = limits_ ? std::min(limits_[s], sl.k2.size()) : sl.k2.size();
        for (std::size_t i = 0; i < n; ++i) grad[sl.offset + i] = dT * sl.k2[i] * z[sl.offset + i];
    }
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
        st.beta.resize(n); st.kappa.resize(n); st.gamma.resize(n); st.wdt.resize(n);
        for (std::size_t i = 0; i < n; ++i) st.wdt[i] = m.omega[i] / fs;
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
    for (std::size_t s = 0; s < ns; ++s) {
        strings[s].gf.resize(strings[s].n);
        for (std::size_t i = 0; i < strings[s].n; ++i) {
            const std::size_t j = strings[s].off + i;
            strings[s].gf[i] = seg.gamma()[j] * strings[s].forceCoef[i] / seg.mass()[j];   // gamma f / acc
        }
    }
    P potential;
    if (const Status e = potential.configure(std::move(slices), totalModes, 0.0, desc.approachSign); e != Status::Ok) return e;
    HammerFelt<double> felt;
    if (const Status e = felt.configure(desc.hammer, potential, nz, fs); e != Status::Ok) return e;
    StrikeMap map;
    if (const Status e = strikeMap(desc.hammer, desc.unaCorda, desc.pedal, ns, 0.0, map); e != Status::Ok) return e;
    potential.setStrikeMap(map);

    seg_ = std::move(seg); potential_ = std::move(potential); felt_ = std::move(felt);
    tension_ = VerticalTensionPotential{}; tension_.configure(std::move(tslices));
    tension_.setFast(fast_);
    bridge_ = bridge; strings_ = std::move(strings);
    nAct_ = {}; for (std::size_t s = 0; s < strings_.size(); ++s) nAct_[s] = strings_[s].n;
    tension_.setLimits(nAct_.data());   // the note must not be moved after configure()
    fg_.assign(nz, 0.0); fz_ = fa_ = fg_; for (auto& v : fh_) v = fg_; for (auto& v : fw_) v = fg_; invMass_.assign(nz, 1.0);
    for (std::size_t i = 0; i < nz; ++i) invMass_[i] = 1.0 / seg_.mass()[i];
    fastContact_ = 0;
    fastDriftThreshold_ = static_cast<std::uint32_t>(std::llround(seg_.params().driftAfterSeconds * fs));
    fastDriftDecay_ = std::exp(-dt / seg_.params().driftTau);
    hZero_ = true; psi0_ = std::sqrt(2.0 * seg_.params().eps0);
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
    dotValid_ = false;
    hZero_ = true;
    for (std::size_t s = 0; s < strings_.size(); ++s) nAct_[s] = strings_[s].n;
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
    dotValid_ = false;
    for (std::size_t s = 0; s < strings_.size(); ++s) nAct_[s] = strings_[s].n;   // 0.5.4: truncated modes restart
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
    if (!tensionEnabled()) return;
    const std::span<const double> z(seg_.q(), seg_.size());
    const double v = potential_.value(z) + tension_.value(z);
    const double star = std::sqrt(2.0 * (v + seg_.params().eps0));
    if (std::abs(seg_.psi()) > star) seg_.setPsi(seg_.psi() < 0 ? -star : star);
}

void TensionStruckNote::setTermCoefficients(std::size_t s, std::span<const float> a, double tOverL) {
    String& st = strings_[s];
    st.aTerm.assign(a.begin(), a.end());
    st.aTerm.resize(st.n, 0.0);
    st.tOverLTerm = tOverL;
}
double TensionStruckNote::bridgeForceNow(double w) noexcept {
    // SPEC-BRG Bridge force per string: sum_n a_n q_n - (T/L) w, summed over the strings in order.
    const std::size_t ns = strings_.size();
    const double* a[3]; const double* qq[3]; std::size_t n[3]; double d[3] = {0, 0, 0};
    for (std::size_t s = 0; s < ns; ++s) { a[s] = strings_[s].aTerm.data(); qq[s] = seg_.q() + strings_[s].off; n[s] = nAct_[s]; }
    if (fast_) {
        for (std::size_t s = 0; s < ns; ++s) { d[s] = kern::dotFast(a[s], qq[s], n[s]); dotCache_[s] = d[s]; }
        dotValid_ = true;
    } else kern::dotExact3(static_cast<int>(ns), a, qq, n, d);
    double force = 0;
    for (std::size_t s = 0; s < ns; ++s) force += d[s] - strings_[s].tOverLTerm * w;
    return force;
}
void TensionStruckNote::zeroTruncated() noexcept {
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        String& st = strings_[s];
        for (std::size_t i = nAct_[s]; i < st.n; ++i) {
            seg_.q()[st.off + i] = 0.0; seg_.u()[st.off + i] = 0.0; st.qH[i] = 0.0; st.uH[i] = 0.0;
        }
    }
}
void TensionStruckNote::truncate(double peakForce) noexcept {
    if (!(truncRel_ > 0.0) || inFlight_) return;
    const double threshold = truncRel_ * peakForce;
    bool changed = false;
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        String& st = strings_[s];
        while (nAct_[s] > 0) {
            const std::size_t i = nAct_[s] - 1;
            const double q = seg_.q()[st.off + i], u = seg_.u()[st.off + i] / st.wdt[i];
            if (std::abs(st.aV[i]) * std::sqrt(q * q + u * u) >= threshold) break;
            seg_.q()[st.off + i] = 0.0; seg_.u()[st.off + i] = 0.0; st.qH[i] = 0.0; st.uH[i] = 0.0;
            --nAct_[s]; changed = true;
        }
    }
    if (changed) { dotValid_ = false; clampPsi(); }
}
// SPEC-NL Skipping, fused: V^n = 0 and no dashpot -> savStep would only run the free oscillator
// update. Preconditions make savStep's other bookkeeping (contact counter, drift controls, psi reset,
// vPrev) no-ops, so the result is bit-identical to the skip path; solvesSkipped is counted as there.
bool TensionStruckNote::tryBypass(double accV, double accH, double* vTensionOut) noexcept {
    if (inFlight_ || seg_.potentialPrev() != 0.0 || seg_.contactSamples() != 0 || std::abs(seg_.psi()) > psi0_)
        return false;
    for (const auto& d : felt_.dashpots()) if (d.R > 0.0) return false;
    if (tensionEnabled()) {
        const double v = tension_.value(std::span<const double>(seg_.q(), seg_.size()));
        *vTensionOut = v;
        if (v > floor_) return false;
    }
    double* const q = seg_.q();
    double* const u = seg_.u();
    // qPrev/uPrev are written through the segment's own (non-const) buffers.
    double* const qp = const_cast<double*>(seg_.qPrev());
    double* const up = const_cast<double*>(seg_.uPrev());
    dotValid_ = false;
    if (fast_) {
        // Reassociated update (README 0.5.4): no division, no qPrev/uPrev.
        for (std::size_t s = 0; s < strings_.size(); ++s) {
            const String& st = strings_[s];
            const std::size_t o = st.off;
            kern::freeStepVFast(q + o, u + o, seg_.beta() + o, seg_.kappa() + o, st.gf.data(), accV, nAct_[s]);
        }
        ++seg_.counters().solvesSkipped;
        if (bridge_ != nullptr && (!hZero_ || accH != 0.0)) stepHorizontal(accH);
        ++sample_;
        return true;
    }
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        const String& st = strings_[s];
        const std::size_t o = st.off;
        kern::freeStepV(q + o, u + o, qp + o, up + o, seg_.beta() + o, seg_.kappa() + o, seg_.gamma() + o,
                        seg_.mass() + o, st.forceCoef.data(), accV, nAct_[s]);
    }
    qp[hammerIdx_] = q[hammerIdx_]; up[hammerIdx_] = u[hammerIdx_];   // frozen lumped coordinate (u = 0)
    ++seg_.counters().solvesSkipped;
    if (bridge_ != nullptr && (!hZero_ || accH != 0.0)) stepHorizontal(accH);
    ++sample_;
    return true;
}
void TensionStruckNote::stepHorizontal(double accH) noexcept {
    // SPEC-EXEC step 7: the horizontal polarisations (linear), driven by the bridge term only.
    hZero_ = false;
    for (String& st : strings_)
        kern::freeStepH(st.qH.data(), st.uH.data(), st.beta.data(), st.kappa.data(), st.gamma.data(),
                        st.g.data(), accH, nAct_[&st - strings_.data()]);
}

// SPEC-NL explicit low-rank solve, rank one (hammer SAV column, dashpots along the gradient merged), in
// fused passes with reassociated reductions: the same equations as pme::nl::savStep, not its bit pattern.
bool TensionStruckNote::flightFast(double vNow, std::span<const nl::Dashpot<double>> dashpots) noexcept {
    const std::size_t n = seg_.size();
    double* const q = seg_.q();
    double* const u = seg_.u();
    const auto& prm = seg_.params();
    ++fastContact_;
    if (prm.driftRelaxation && fastContact_ > fastDriftThreshold_) {
        const double psiStar = std::sqrt(2.0 * (vNow + prm.eps0));
        const double mag = std::abs(seg_.psi());
        if (mag > psiStar) {
            const double relaxed = psiStar + (mag - psiStar) * fastDriftDecay_;
            seg_.setPsi(seg_.psi() < 0.0 ? -relaxed : relaxed);
            ++seg_.counters().driftCorrections;
        }
    }
    const std::span<const double> z(q, n);
    potential_.gradient(z, std::span<double>(fg_.data(), n));
    const double invDen = 1.0 / std::sqrt(2.0 * (vNow + prm.eps0));
    double gg = kern::dotFast(fg_.data(), fg_.data(), n) * invDen * invDen;
    if (!(gg > 0.0)) return false;
    // Columns of H and W (SPEC-NL: SAV pair h = gamma g / m, w = g / 4; dashpot pair h = R gamma c / m,
    // w = c / (2 dt); a dashpot along g merges into the SAV column).
    double wScale = 0.25;
    for (const auto& d : dashpots)
        if (d.R > 0.0 && d.alongGradient) {
            const double lambda = kern::dotFast(d.c.data(), fg_.data(), n) * invDen / gg;
            wScale += d.R * lambda * lambda / (2.0 * seg_.dt());
        }
    const double* H[4]; const double* W[4];
    int r = 0;
    {
        double* hv = fh_[0].data(); double* wv = fw_[0].data();
        for (std::size_t i = 0; i < n; ++i) {
            const double gi = fg_[i] * invDen;
            fg_[i] = gi; hv[i] = seg_.gamma()[i] * gi * invMass_[i]; wv[i] = gi * wScale;
        }
        H[0] = hv; W[0] = wv; r = 1;
    }
    for (const auto& d : dashpots) {
        if (!(d.R > 0.0) || d.alongGradient) continue;
        if (r >= 4) return false;
        double* hv = fh_[r].data(); double* wv = fw_[r].data();
        const double inv2dt = 1.0 / (2.0 * seg_.dt());
        for (std::size_t i = 0; i < n; ++i) {
            hv[i] = d.R * seg_.gamma()[i] * d.c[i] * invMass_[i]; wv[i] = d.c[i] * inv2dt;
        }
        H[r] = hv; W[r] = wv; ++r;
    }
    double* const qp = const_cast<double*>(seg_.qPrev());
    double* const up = const_cast<double*>(seg_.uPrev());
    double known[4] = {}, wa[4] = {}, M[16] = {}, y[4] = {}, dPsi = 0;
    kern::flightPassA(r, W, q, u, fz_.data(), qp, up, n, known);
    const double psi = seg_.psi();
    kern::flightPassB(r, fa_.data(), W, H, q, u, seg_.beta(), seg_.kappa(), seg_.gamma(), invMass_.data(),
                      force_.data(), known, true, psi, n, wa, M);
    // (I + W^T H) y = W^T a by Gaussian elimination with partial pivoting (as pme::nl::lowRankSolve).
    std::array<std::array<double, 5>, 4> m{};
    for (int j = 0; j < r; ++j) {
        for (int k = 0; k < r; ++k) m[j][k] = M[j * r + k] + (j == k ? 1.0 : 0.0);
        m[j][r] = wa[j];
    }
    for (int col = 0; col < r; ++col) {
        int piv = col;
        for (int row = col + 1; row < r; ++row) if (std::abs(m[row][col]) > std::abs(m[piv][col])) piv = row;
        std::swap(m[piv], m[col]);
        const double pivot = m[col][col];
        for (int row = col + 1; row < r; ++row) {
            const double factor = m[row][col] / pivot;
            for (int k = col; k <= r; ++k) m[row][k] -= factor * m[col][k];
        }
    }
    for (int jj = r; jj-- > 0;) {
        double s = m[jj][r];
        for (int k = jj + 1; k < r; ++k) s -= m[jj][k] * y[k];
        y[jj] = s / m[jj][jj];
    }
    kern::flightPassC(r, q, u, up, fa_.data(), H, y, fg_.data(), n, &dPsi);
    seg_.setPsi(psi + 0.5 * dPsi);
    return true;
}

void TensionStruckNote::step() noexcept {
    const std::size_t nz = seg_.size();
    double accV = 0.0, accH = 0.0;
    if (bridge_ != nullptr) {
        // SPEC-EXEC step 2: F_b,s from q^n and w^n, per string and polarisation (the dot products of
        // all strings are interleaved, each summed in index order). The horizontal state of a
        // point without perpendicular conductance is exactly zero (hZero_): its sums are +0.
        const double wv = bridge_->wV();
        const double wh = bridge_->wH();
        const std::size_t ns = strings_.size();
        const double* a[3]; const double* qq[3]; std::size_t n[3]; double dv[3] = {0, 0, 0}, dh[3] = {0, 0, 0};
        for (std::size_t s = 0; s < ns; ++s) { a[s] = strings_[s].aV.data(); qq[s] = seg_.q() + strings_[s].off; n[s] = nAct_[s]; }
        if (fast_) {
            // The force dot of the previous sample's q^{n+1} is still valid when nothing touched q since.
            for (std::size_t s = 0; s < ns; ++s)
                dv[s] = dotValid_ ? dotCache_[s] : kern::dotFast(strings_[s].aTerm.empty() ? a[s] : strings_[s].aTerm.data(), qq[s], n[s]);
            dotValid_ = false;
        } else kern::dotExact3(static_cast<int>(ns), a, qq, n, dv);
        if (!hZero_) {
            for (std::size_t s = 0; s < ns; ++s) { a[s] = strings_[s].aH.data(); qq[s] = strings_[s].qH.data(); }
            if (fast_) for (std::size_t s = 0; s < ns; ++s) dh[s] = kern::dotFast(a[s], qq[s], n[s]);
            else kern::dotExact3(static_cast<int>(ns), a, qq, n, dh);
        }
        for (std::size_t s = 0; s < ns; ++s)
            bridge_->addForce(dv[s] - strings_[s].tOverL * wv, dh[s] - strings_[s].tOverL * wh);
        // SPEC-EXEC steps 3 and 4.
        bridge_->advance();
        accV = bridge_->accelV();
        accH = bridge_->accelH();
    }
    double vTension = 0.0;
    if (tryBypass(accV, accH, &vTension)) return;
    std::fill(force_.begin(), force_.end(), 0.0);
    // SPEC-EXEC step 5: explicit modal forces -rho A g_n delta_tt w^n.
    if (bridge_ != nullptr)
        for (const String& st : strings_)
            for (std::size_t i = 0; i < st.n; ++i) force_[st.off + i] = -st.forceCoef[i] * accV;

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
    vTension = 0.0;
    if (tensionEnabled()) {
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
    if (!(fast_ && nPots == 1 && inFlight_ && vNow > 0.0 && flightFast(vNow, dashpots))) {
        if (vNow <= 0.0) fastContact_ = 0;
        nl::savStep<double>(seg_, std::span<const nl::Potential<double>* const>(pots.data(), nPots), dashpots,
                            std::span<const double>(force_));
    }
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
    dotValid_ = false;
    if (truncated()) { zeroTruncated(); clampPsi(); }
    // SPEC-EXEC step 7: the horizontal polarisations (linear), driven by the bridge term only.
    if (bridge_ != nullptr && (!hZero_ || accH != 0.0)) stepHorizontal(accH);
    ++sample_;
}
}
