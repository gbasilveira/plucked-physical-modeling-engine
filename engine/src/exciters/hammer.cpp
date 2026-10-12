// SPDX-License-Identifier: MIT
// Piano hammer and felt (A5-R1 to A5-R7). See hammer.hpp and docs/dev/hammer.md.
//
// SPEC sections implemented: SPEC-HAM (all of "Hammer and action"), SPEC-NL "Coordinates",
// "SAV formulation", "Explicit low-rank solve", "Contracts"; SPEC-EXEC "Per-sample loop of one
// note group" steps 1, 2, 5, 6 (and 7); SPEC-BRG "Discrete coupling order"; SPEC-STR "Forces and
// observations"; SPEC-CONV.
#include "pme/exciters/hammer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

#include "pme/bridge/terms.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/osc/oscillator.hpp"
#include "pme/string/string_model.hpp"

namespace pme::exciters {

namespace {

constexpr double kVeryNegative = -1.0e300;

[[nodiscard]] bool finitePositive(double x) noexcept { return std::isfinite(x) && x > 0.0; }

}  // namespace

// ---- A5-R1 and validation --------------------------------------------------------------------

Status hammerVelocity(double nu, const HammerParams& params, double& vH) noexcept {
    if (!(nu > 0.0) || !std::isfinite(nu)) {
        return Status::InvalidArgument;
    }
    const double n = std::min(nu, 1.0);
    // SPEC-HAM Velocity map: v_H = v_min + (v_max - v_min) nu^gamma_v.
    vH = params.vMin + (params.vMax - params.vMin) * std::pow(n, params.gammaV);
    return Status::Ok;
}

Status validate(const HammerParams& h) noexcept {
    const bool ok = finitePositive(h.mass) && finitePositive(h.K) && finitePositive(h.p) &&
                    std::isfinite(h.muHC) && h.muHC >= 0.0 && finitePositive(h.width) &&
                    std::isfinite(h.strikeRatio) && h.strikeRatio > 0.0 && h.strikeRatio < 1.0 &&
                    std::isfinite(h.vMin) && h.vMin >= 0.0 && std::isfinite(h.vMax) &&
                    h.vMax >= h.vMin && finitePositive(h.gammaV) && finitePositive(h.dCheck) &&
                    std::isfinite(h.stulovEps) && std::isfinite(h.stulovTau) &&
                    (h.law != FeltLaw::Stulov || (h.stulovEps >= 0.0 && h.stulovTau > 0.0));
    return ok ? Status::Ok : Status::InvalidArgument;
}

Status validate(const UnaCordaParams& u) noexcept {
    const bool ok = std::isfinite(u.s0) && std::isfinite(u.s1) && u.s0 >= 0.0 && u.s1 > u.s0 &&
                    u.s1 <= 1.0 && finitePositive(u.kappaUC) && std::isfinite(u.deltaPUC) &&
                    std::isfinite(u.rhoUpright) && u.rhoUpright >= 0.0 && u.rhoUpright < 1.0 &&
                    u.leavingString >= -1;
    return ok ? Status::Ok : Status::InvalidArgument;
}

Status validate(const ShankParams& s) noexcept {
    const bool ok = finitePositive(s.mass) && finitePositive(s.frequency) &&
                    std::isfinite(s.sigma) && s.sigma >= 0.0;
    return ok ? Status::Ok : Status::InvalidArgument;
}

// ---- A5-R5 -----------------------------------------------------------------------------------

double smoothstep(double s, double s0, double s1) noexcept {
    const double t = std::clamp((s - s0) / (s1 - s0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);  // SPEC-HAM Una corda: cubic smoothstep 3 t^2 - 2 t^3
}

Status strikeMap(const HammerParams& hammer, const UnaCordaParams& una, SoftPedal pedal,
                 std::size_t nStrings, double shift, StrikeMap& out) noexcept {
    if (validate(hammer) != Status::Ok || validate(una) != Status::Ok || nStrings < 1 ||
        nStrings > kMaxStruckStrings || !std::isfinite(shift)) {
        return Status::InvalidArgument;
    }
    if (nStrings > 1 && una.leavingString >= static_cast<int>(nStrings)) {
        return Status::InvalidArgument;
    }
    const double s = std::clamp(shift, 0.0, 1.0);
    StrikeMap m;
    m.strings = nStrings;
    const bool grand = pedal == SoftPedal::UnaCorda;
    // SPEC-HAM Una corda: K_s(s) = K [1 - s (1 - kappa_uc)], p_s(s) = p + s Delta p_uc.
    const double kFactor = grand ? 1.0 - s * (1.0 - una.kappaUC) : 1.0;
    const double pShift = grand ? s * una.deltaPUC : 0.0;
    const double leave = grand ? 1.0 - smoothstep(s, una.s0, una.s1) : 1.0;
    for (std::size_t i = 0; i < nStrings; ++i) {
        // SPEC-HAM Una corda table: the leaving string has w = 1 - S(s), the others 1.
        const bool leaves = nStrings > 1 && una.leavingString == static_cast<int>(i);
        m.weight[i] = leaves ? leave : 1.0;
        m.stiffness[i] = hammer.K * kFactor;
        m.exponent[i] = hammer.p + pShift;
    }
    out = m;
    return Status::Ok;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): nu then shift, as SPEC-HAM launchHammer
Status launchVelocity(double nu, double shift, const HammerParams& hammer,
                      const UnaCordaParams& una, SoftPedal pedal, double& vH) noexcept {
    if (!std::isfinite(shift)) {
        return Status::InvalidArgument;
    }
    double v = 0.0;
    if (const Status st = hammerVelocity(nu, hammer, v); st != Status::Ok) {
        return st;
    }
    if (pedal == SoftPedal::Upright) {
        // SPEC-HAM Soft pedal (upright): v_H scaled by (1 - s rho_up).
        v *= 1.0 - std::clamp(shift, 0.0, 1.0) * una.rhoUpright;
    }
    vH = v;
    return Status::Ok;
}

// ---- A5-R2: potential ------------------------------------------------------------------------

template <class S>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): lever then approach sign
Status HammerPotential<S>::configure(std::vector<StringSlice> strings, std::size_t hammerIndex,
                                     double lever, double approachSign) {
    if (strings.empty() || strings.size() > kMaxStruckStrings || !std::isfinite(lever) ||
        (approachSign != 1.0 && approachSign != -1.0)) {
        return Status::InvalidArgument;
    }
    for (const StringSlice& s : strings) {
        if (s.phiBar.empty() || s.offset + s.phiBar.size() > hammerIndex) {
            return Status::InvalidArgument;
        }
        for (const float p : s.phiBar) {
            if (!std::isfinite(p)) {
                return Status::InvalidArgument;
            }
        }
    }
    strings_ = std::move(strings);
    hammerIndex_ = hammerIndex;
    lever_ = lever;
    sE_ = approachSign;
    active_ = false;
    map_ = StrikeMap{};
    map_.strings = strings_.size();
    return Status::Ok;
}

template <class S>
double HammerPotential<S>::headPosition(std::span<const S> z) const noexcept {
    // SPEC-HAM Flexible shank: x_H = X + lever Y (lever = 0 for a rigid hammer).
    auto x = static_cast<double>(z[hammerIndex_]);
    if (lever_ != 0.0) {
        x += lever_ * static_cast<double>(z[hammerIndex_ + 1]);
    }
    return x;
}

template <class S>
double HammerPotential<S>::stringPosition(std::span<const S> z, std::size_t s) const noexcept {
    const StringSlice& sl = strings_[s];
    double acc = 0.0;
    for (std::size_t n = 0; n < sl.phiBar.size(); ++n) {
        acc += static_cast<double>(sl.phiBar[n]) * static_cast<double>(z[sl.offset + n]);
    }
    return sE_ * acc;  // SPEC-HAM Contact: s_E sum_n phi-bar_{s,n} q_{s,n}
}

template <class S>
double HammerPotential<S>::penetration(std::span<const S> z, std::size_t s) const noexcept {
    return headPosition(z) - stringPosition(z, s);  // SPEC-HAM Contact: delta_s
}

template <class S>
double HammerPotential<S>::maxPenetration(std::span<const S> z) const noexcept {
    const double xh = headPosition(z);
    double m = kVeryNegative;
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        if (map_.weight[s] > 0.0) {
            m = std::max(m, xh - stringPosition(z, s));
        }
    }
    return m;
}

template <class S>
double HammerPotential<S>::value(std::span<const S> z) const noexcept {
    if (!active_) {
        return 0.0;
    }
    const double xh = headPosition(z);
    double v = 0.0;
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        const double w = map_.weight[s];
        if (!(w > 0.0)) {
            continue;
        }
        const double d = xh - stringPosition(z, s);
        if (d > 0.0) {
            const double p = map_.exponent[s];
            // SPEC-HAM Contact: V_H = sum_s w_s K_s / (p_s + 1) [delta_s]_+^(p_s + 1)
            v += w * map_.stiffness[s] / (p + 1.0) * d * std::pow(d, p);
        }
    }
    return v;
}

template <class S>
double HammerPotential<S>::hammerGradient(std::span<const S> z) const noexcept {
    if (!active_) {
        return 0.0;
    }
    const double xh = headPosition(z);
    double g = 0.0;
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        const double w = map_.weight[s];
        if (!(w > 0.0)) {
            continue;
        }
        const double d = xh - stringPosition(z, s);
        if (d > 0.0) {
            g += w * map_.stiffness[s] * std::pow(d, map_.exponent[s]);  // dV/dx_H
        }
    }
    return g;
}

template <class S>
void HammerPotential<S>::gradient(std::span<const S> z, std::span<double> grad) const noexcept {
    std::fill(grad.begin(), grad.end(), 0.0);
    if (!active_) {
        return;
    }
    const double xh = headPosition(z);
    for (std::size_t s = 0; s < strings_.size(); ++s) {
        const double w = map_.weight[s];
        if (!(w > 0.0)) {
            continue;
        }
        const StringSlice& sl = strings_[s];
        const double d = xh - stringPosition(z, s);
        if (d > 0.0) {
            // SPEC-HAM Contact: dV/dx_H = w K [delta]_+^p, dV/dq_n = -s_E phi-bar_n w K [delta]_+^p
            const double f = w * map_.stiffness[s] * std::pow(d, map_.exponent[s]);
            grad[hammerIndex_] += f;
            if (lever_ != 0.0) {
                grad[hammerIndex_ + 1] += lever_ * f;
            }
            for (std::size_t n = 0; n < sl.phiBar.size(); ++n) {
                grad[sl.offset + n] += -sE_ * static_cast<double>(sl.phiBar[n]) * f;
            }
        }
    }
}

template <class S>
double RigidFeltPotential<S>::value(std::span<const S> z) const noexcept {
    const double d = static_cast<double>(z[index_]) - target_;
    return d > 0.0 ? k_ / (p_ + 1.0) * d * std::pow(d, p_) : 0.0;  // SPEC-HAM Oracle
}

template <class S>
void RigidFeltPotential<S>::gradient(std::span<const S> z, std::span<double> grad) const noexcept {
    std::fill(grad.begin(), grad.end(), 0.0);
    const double d = static_cast<double>(z[index_]) - target_;
    if (d > 0.0) {
        grad[index_] = k_ * std::pow(d, p_);
    }
}

// ---- A5-R3: felt -----------------------------------------------------------------------------

std::size_t hammerDashpotCount(const HammerParams& hammer, std::size_t nStrings) noexcept {
    return hammer.law == FeltLaw::HuntCrossley ? std::min(nStrings, nl::kMaxDashpots) : 0;
}

std::size_t hammerDashpotBudgetLeft(const HammerParams& hammer, std::size_t nStrings) noexcept {
    return nl::kMaxDashpots - hammerDashpotCount(hammer, nStrings);
}

template <class S>
Status HammerFelt<S>::configure(const HammerParams& hammer, const HammerPotential<S>& potential,
                                std::size_t nz, double fs) {
    if (validate(hammer) != Status::Ok || !finitePositive(fs) || potential.stringCount() == 0 ||
        potential.hammerIndex() >= nz) {
        return Status::InvalidArgument;
    }
    law_ = hammer.law;
    muHC_ = hammer.muHC;
    eps_ = hammer.stulovEps;
    // SPEC-HAM Stulov: a = exp(-dt / tau).
    decay_ = hammer.law == FeltLaw::Stulov ? std::exp(-1.0 / (fs * hammer.stulovTau)) : 0.0;
    const std::size_t ns = potential.stringCount();
    count_ = hammerDashpotCount(hammer, ns);
    c_.assign(ns, std::vector<S>(nz, S{}));
    const std::size_t hi = potential.hammerIndex();
    for (std::size_t s = 0; s < ns; ++s) {
        // SPEC-HAM Felt dissipation: c_s = d delta_s / d z: +1 on x_H, -s_E phi-bar on the modes.
        c_[s][hi] = static_cast<S>(1.0);
        if (potential.lever() != 0.0) {
            c_[s][hi + 1] = static_cast<S>(potential.lever());
        }
        const StringSlice& sl = potential.slice(s);
        for (std::size_t n = 0; n < sl.phiBar.size(); ++n) {
            c_[s][sl.offset + n] =
                static_cast<S>(-potential.approachSign() * static_cast<double>(sl.phiBar[n]));
        }
    }
    reset();
    return Status::Ok;
}

template <class S>
void HammerFelt<S>::reset() noexcept {
    h_.fill(0.0);
    force_.fill(0.0);
    for (nl::Dashpot<S>& d : dashpots_) {
        d.R = 0.0;
        d.alongGradient = false;
    }
}

template <class S>
void HammerFelt<S>::update(const HammerPotential<S>& potential, std::span<const S> z,
                           std::span<S> force) noexcept {
    const StrikeMap& map = potential.map();
    const std::size_t ns = potential.stringCount();
    std::array<double, kMaxStruckStrings> delta{};
    std::array<double, kMaxStruckStrings> dp{};
    std::size_t contributors = 0;
    for (std::size_t s = 0; s < ns; ++s) {
        const double d = potential.penetration(z, s);
        delta[s] = d;
        dp[s] = d > 0.0 ? std::pow(d, map.exponent[s]) : 0.0;
        if (d > 0.0 && map.weight[s] * map.stiffness[s] > 0.0) {
            ++contributors;
        }
    }
    if (law_ == FeltLaw::HuntCrossley) {
        // The contact gradient is parallel to c_s only if one string contributes (r = 1).
        const bool along = contributors == 1;
        for (std::size_t s = 0; s < ns; ++s) {
            nl::Dashpot<S>& d = dashpots_[s];
            d.c = std::span<const S>(c_[s]);
            // SPEC-HAM Felt dissipation: R_s = w_s K_s mu_HC [delta_s]_+^p_s
            d.R = delta[s] > 0.0 ? map.weight[s] * map.stiffness[s] * muHC_ * dp[s] : 0.0;
            d.alongGradient = along;
        }
        return;
    }
    for (std::size_t s = 0; s < ns; ++s) {
        // SPEC-HAM Stulov: h^n = a h^{n-1} + (1 - a) [delta^n]_+^p; F = -K eps h along c_s, in
        // contact only (the felt does not act on a separated hammer).
        h_[s] = decay_ * h_[s] + (1.0 - decay_) * dp[s];
        const double f = delta[s] > 0.0 ? -map.weight[s] * map.stiffness[s] * eps_ * h_[s] : 0.0;
        force_[s] = f;
        if (f != 0.0) {
            for (std::size_t i = 0; i < force.size(); ++i) {
                force[i] += static_cast<S>(f * static_cast<double>(c_[s][i]));
            }
        }
    }
}

template <class S>
double HammerFelt<S>::hereditaryWork(const nl::SavSegment<S>& seg) const noexcept {
    double work = 0.0;
    if (law_ != FeltLaw::Stulov) {
        return work;
    }
    for (std::size_t s = 0; s < c_.size(); ++s) {
        if (force_[s] == 0.0) {
            continue;
        }
        double cv = 0.0;
        for (std::size_t i = 0; i < seg.size(); ++i) {
            cv += static_cast<double>(c_[s][i]) *
                  (static_cast<double>(seg.u()[i]) + static_cast<double>(seg.uPrev()[i]));
        }
        work += force_[s] * 0.5 * cv;  // F c^T (u^{n+1} + u^n) / 2 (power times dt)
    }
    return work;
}

template class HammerPotential<float>;
template class HammerPotential<double>;
template class RigidFeltPotential<float>;
template class RigidFeltPotential<double>;
template class HammerFelt<float>;
template class HammerFelt<double>;

// ---- StruckNote ------------------------------------------------------------------------------

template <class S>
Status StruckNote<S>::configure(const StruckNoteDesc& desc, double fs,
                                bridge::BridgePoint<S>* bridge) {
    const std::size_t ns = desc.strings.size();
    if (ns < 1 || ns > kMaxStruckStrings || !finitePositive(fs) ||
        validate(desc.hammer) != Status::Ok || validate(desc.unaCorda) != Status::Ok ||
        (desc.approachSign != 1.0 && desc.approachSign != -1.0)) {
        return Status::InvalidArgument;
    }
    if (desc.shank.has_value() && validate(*desc.shank) != Status::Ok) {
        return Status::InvalidArgument;
    }
    std::size_t totalModes = 0;
    for (const StringModes* m : desc.strings) {
        if (m == nullptr || m->k.empty() || m->fs != fs) {
            return Status::InvalidArgument;
        }
        totalModes += m->k.size();
    }
    const double dt = 1.0 / fs;
    const bool shank = desc.shank.has_value();
    const std::size_t nLumped = shank ? 2 : 1;
    const std::size_t nz = totalModes + nLumped;

    // Build everything in locals first so that a failure leaves the note unchanged.
    std::vector<String> strings(ns);
    std::vector<StringSlice> slices(ns);
    std::size_t off = 0;
    for (std::size_t s = 0; s < ns; ++s) {
        const StringModes& m = *desc.strings[s];
        String& st = strings[s];
        st.off = off;
        st.n = m.k.size();
        st.rhoA = m.rhoA;
        st.tOverL = m.tension / m.L;
        std::vector<double> g;
        std::vector<double> aV;
        std::vector<double> aHd;
        if (Status e = bridge::liftingOverlaps(m, desc.end, g); e != Status::Ok) {
            return e;
        }
        if (Status e = bridge::forceCoefficients(m, desc.end, bridge::Polarisation::Vertical, aV);
            e != Status::Ok) {
            return e;
        }
        if (Status e =
                bridge::forceCoefficients(m, desc.end, bridge::Polarisation::Horizontal, aHd);
            e != Status::Ok) {
            return e;
        }
        const std::size_t n = st.n;
        st.aV.resize(n);
        st.aH.resize(n);
        st.forceCoef.resize(n);
        st.g.resize(n);
        st.beta.resize(n);
        st.kappa.resize(n);
        st.gamma.resize(n);
        st.qH.assign(n, S{});
        st.uH.assign(n, S{});
        st.fH.assign(n, S{});
        for (std::size_t i = 0; i < n; ++i) {
            st.aV[i] = static_cast<S>(aV[i]);
            st.aH[i] = static_cast<S>(aHd[i]);
            st.g[i] = static_cast<S>(g[i]);
            st.forceCoef[i] = static_cast<S>(m.rhoA * g[i]);
            // SPEC-OSC Difference form of the horizontal polarisation (as bridge::ModalSegment).
            const double sd = m.oscH[i].sigmaHat * dt;
            const double denom = 1.0 + sd;
            st.beta[i] = static_cast<S>((1.0 - sd) / denom);
            st.kappa[i] = static_cast<S>(m.oscH[i].omegaHat2 * dt * dt / denom);
            st.gamma[i] = static_cast<S>(dt * dt / denom);
        }
        slices[s].offset = off;
        slices[s].phiBar.assign(n, 0.0F);
        // SPEC-STR Forces and observations: raised-cosine window of width b_H at x_c = beta_c L.
        string::windowedShapes(m, desc.hammer.strikeRatio * m.L, string::WindowKind::RaisedCosine,
                               desc.hammer.width, slices[s].phiBar);
        off += n;
    }

    const double mHead = desc.hammer.mass;
    double lever = 0.0;
    double mRigid = mHead;
    double mInternal = 0.0;
    osc::OscCoeffsF64 shankCoeffs;
    if (shank) {
        const ShankParams& sp = *desc.shank;
        // SPEC-HAM Flexible shank: normal coordinates of the head-shank pair. X = (m_H x_H + m_S
        // x_S) / M (free, mass M), Y = x_H - x_S (internal, reduced mass mu); x_H = X + (m_S / M)
        // Y.
        mRigid = mHead + sp.mass;
        mInternal = mHead * sp.mass / mRigid;
        lever = sp.mass / mRigid;
        const Status e = osc::compileOscillatorF64(2.0 * std::numbers::pi * sp.frequency, sp.sigma,
                                                   fs, shankCoeffs);
        if (e != Status::Ok) {
            return e == Status::NotImplemented ? Status::InvalidArgument : e;
        }
    }

    nl::SavSegment<S> seg;
    if (const Status e = seg.configure(totalModes, nLumped, nl::kMaxDashpots, fs, desc.sav);
        e != Status::Ok) {
        return e;
    }
    for (std::size_t s = 0; s < ns; ++s) {
        const StringModes& m = *desc.strings[s];
        for (std::size_t i = 0; i < strings[s].n; ++i) {
            // SPEC-OSC Difference form from the stored float64 omegaHat^2 and sigmaHat.
            const double sd = m.oscV[i].sigmaHat * dt;
            const double denom = 1.0 + sd;
            osc::OscCoeffsF64 c;
            c.sigmaHatDt = sd;
            c.omegaHat2Dt2 = m.oscV[i].omegaHat2 * dt * dt;
            c.beta = (1.0 - sd) / denom;
            c.kappa = m.oscV[i].omegaHat2 * dt * dt / denom;
            c.gamma = dt * dt / denom;
            c.omegaHat2 = m.oscV[i].omegaHat2;
            c.sigmaHat = m.oscV[i].sigmaHat;
            if (const Status e = seg.setCoordinate(strings[s].off + i, c, m.rhoA);
                e != Status::Ok) {
                return e;
            }
        }
    }
    if (const Status e = seg.setLumped(totalModes, mRigid); e != Status::Ok) {
        return e;
    }
    if (shank) {
        if (const Status e = seg.setCoordinate(totalModes + 1, shankCoeffs, mInternal);
            e != Status::Ok) {
            return e;
        }
    }
    HammerPotential<S> potential;
    if (const Status e =
            potential.configure(std::move(slices), totalModes, lever, desc.approachSign);
        e != Status::Ok) {
        return e;
    }
    HammerFelt<S> felt;
    if (const Status e = felt.configure(desc.hammer, potential, nz, fs); e != Status::Ok) {
        return e;
    }
    StrikeMap map;
    if (const Status e = strikeMap(desc.hammer, desc.unaCorda, desc.pedal, ns, 0.0, map);
        e != Status::Ok) {
        return e;
    }
    potential.setStrikeMap(map);

    seg_ = std::move(seg);
    potential_ = std::move(potential);
    felt_ = std::move(felt);
    bridge_ = bridge;
    strings_ = std::move(strings);
    passive_.clear();
    force_.assign(nz, S{});
    hammer_ = desc.hammer;
    una_ = desc.unaCorda;
    pedal_ = desc.pedal;
    hasShank_ = shank;
    shankLever_ = lever;
    hammerIdx_ = totalModes;
    sE_ = desc.approachSign;
    dt_ = dt;
    shift_ = 0.0;
    inFlight_ = false;
    prevMax_ = kVeryNegative;
    prevSample_ = 0;
    contactOpen_ = false;
    peak_ = 0.0;
    sample_ = 0;
    releasedEnergy_ = 0.0;
    resetEnergyRemoved_ = 0.0;
    diag_ = HammerDiagnostics{};
    return Status::Ok;
}

template <class S>
Status StruckNote<S>::addPassiveSegment(const StringModes& modes, BridgeEnd end) {
    if (bridge_ == nullptr) {
        return Status::InvalidArgument;
    }
    bridge::ModalSegment<S> seg;
    if (const Status e = seg.configure(modes, end); e != Status::Ok) {
        return e;
    }
    passive_.push_back(std::move(seg));
    return Status::Ok;
}

template <class S>
void StruckNote<S>::reset() noexcept {
    seg_.resetState();
    for (String& st : strings_) {
        std::fill(st.qH.begin(), st.qH.end(), S{});
        std::fill(st.uH.begin(), st.uH.end(), S{});
    }
    for (auto& p : passive_) {
        p.reset();
    }
    potential_.setActive(false);
    felt_.reset();
    inFlight_ = false;
    contactOpen_ = false;
    prevMax_ = kVeryNegative;
    prevSample_ = sample_;
}

template <class S>
double StruckNote<S>::headPosition() const noexcept {
    return potential_.headPosition(std::span<const S>(seg_.q(), seg_.size()));
}

template <class S>
double StruckNote<S>::headVelocity() const noexcept {
    auto u = static_cast<double>(seg_.u()[hammerIdx_]);
    if (hasShank_) {
        u += shankLever_ * static_cast<double>(seg_.u()[hammerIdx_ + 1]);
    }
    return u / dt_;
}

template <class S>
Status StruckNote<S>::setShift(double shift) noexcept {
    StrikeMap map;
    if (const Status e = strikeMap(hammer_, una_, pedal_, strings_.size(), shift, map);
        e != Status::Ok) {
        return e;
    }
    potential_.setStrikeMap(map);
    shift_ = std::clamp(shift, 0.0, 1.0);
    return Status::Ok;
}

template <class S>
Status StruckNote<S>::launch(double nu, double shift) noexcept {
    double vH = 0.0;
    if (const Status e = launchVelocity(nu, shift, hammer_, una_, pedal_, vH); e != Status::Ok) {
        return e;
    }
    if (const Status e = setShift(shift); e != Status::Ok) {
        return e;
    }
    S* const q = seg_.q();
    S* const u = seg_.u();
    const std::span<const S> z(q, seg_.size());
    const double du = vH * dt_;  // SPEC-HAM Velocity map: gap v_H dt, one sample to the string
    const bool continues = inFlight_ && potential_.maxPenetration(z) >= -hammer_.dCheck;
    if (continues) {
        // SPEC-HAM Repetition: within d_check, re-launch from the present position (no jump in
        // x_H); both masses of a shank move at the new v_H with zero spring deflection.
        if (hasShank_) {
            q[hammerIdx_] = static_cast<S>(potential_.headPosition(z));
            q[hammerIdx_ + 1] = S{};
            u[hammerIdx_ + 1] = S{};
        }
        u[hammerIdx_] = static_cast<S>(du);
    } else {
        // SPEC-HAM Repetition: otherwise from the launch gap, measured from the string the
        // hammer reaches first (the highest struck string along s_E).
        double y = kVeryNegative;
        for (std::size_t s = 0; s < strings_.size(); ++s) {
            if (potential_.map().weight[s] > 0.0) {
                y = std::max(y, potential_.stringPosition(z, s));
            }
        }
        q[hammerIdx_] = static_cast<S>(y - du);
        u[hammerIdx_] = static_cast<S>(du);
        if (hasShank_) {
            q[hammerIdx_ + 1] = S{};
            u[hammerIdx_ + 1] = S{};
        }
        felt_.reset();
        inFlight_ = true;
        potential_.setActive(true);
    }
    prevMax_ = potential_.maxPenetration(z);
    prevSample_ = sample_;
    ++diag_.strikes;
    return Status::Ok;
}

template <class S>
void StruckNote<S>::closeContact(double endSample) noexcept {
    const double dur = (endSample - contactStart_) * dt_;
    diag_.lastContactDuration = dur;
    diag_.lastPeakForce = peak_;
    diag_.totalContactTime += dur;
    ++diag_.contacts;
    contactOpen_ = false;
}

template <class S>
void StruckNote<S>::trackContact(double maxDelta) noexcept {
    const auto n = static_cast<double>(sample_);
    if (sample_ > prevSample_) {
        const double p = prevMax_;
        const auto p0 = static_cast<double>(prevSample_);
        if (!contactOpen_ && maxDelta > 0.0 && p <= 0.0) {
            contactStart_ = p0 + (n - p0) * (-p) / (maxDelta - p);  // zero crossing, linear
            contactOpen_ = true;
            peak_ = 0.0;
        } else if (contactOpen_ && maxDelta <= 0.0 && p > 0.0) {
            closeContact(p0 + (n - p0) * p / (p - maxDelta));
        }
    } else if (!contactOpen_ && maxDelta > 0.0) {
        contactStart_ = n;
        contactOpen_ = true;
        peak_ = 0.0;
    }
    prevMax_ = maxDelta;
    prevSample_ = sample_;
}

template <class S>
void StruckNote<S>::release() noexcept {
    // SPEC-HAM Release and check: the hammer leaves the coordinate set. It is frozen (u = 0, held
    // position); the shank's internal state is dropped (this only removes energy).
    S* const q = seg_.q();
    S* const u = seg_.u();
    // The energy the hammer carries away (kinetic energy and the shank's internal energy).
    for (std::size_t i = hammerIdx_; i < seg_.size(); ++i) {
        releasedEnergy_ += seg_.mass()[i] * osc::modeEnergy(seg_.omegaHat2()[i],
                                                            static_cast<double>(seg_.qPrev()[i]),
                                                            static_cast<double>(q[i]),
                                                            static_cast<double>(u[i]), dt_);
    }
    u[hammerIdx_] = S{};
    if (hasShank_) {
        q[hammerIdx_ + 1] = S{};
        u[hammerIdx_ + 1] = S{};
    }
    if (contactOpen_) {
        closeContact(static_cast<double>(sample_));
    }
    inFlight_ = false;
    potential_.setActive(false);
    felt_.reset();
}

template <class S>
void StruckNote<S>::step() noexcept {
    const std::size_t nz = seg_.size();
    std::fill(force_.begin(), force_.end(), S{});
    S accH = S{};
    if (bridge_ != nullptr) {
        // SPEC-EXEC step 2: F_b,s from q^n and w^n, per string and polarisation.
        const S wv = bridge_->wV();
        const S wh = bridge_->wH();
        for (const String& st : strings_) {
            bridge_->addForce(
                bridge::bridgeForce<S, S>(std::span<const S>(st.aV), st.tOverL, seg_.q() + st.off,
                                          wv),
                bridge::bridgeForce<S, S>(std::span<const S>(st.aH), st.tOverL, st.qH.data(), wh));
        }
        for (const auto& p : passive_) {
            bridge_->addForce(p.forceV(wv), p.forceH(wh));
        }
        // SPEC-EXEC steps 3 and 4: advance the point, form delta_tt w^n.
        bridge_->advance();
        const S accV = bridge_->accelV();
        accH = bridge_->accelH();
        // SPEC-EXEC step 5: explicit modal forces -rho A g_n delta_tt w^n (N per coordinate).
        for (const String& st : strings_) {
            for (std::size_t i = 0; i < st.n; ++i) {
                force_[st.off + i] = -st.forceCoef[i] * accV;
            }
        }
    }

    const std::span<const S> z(seg_.q(), nz);
    double vNow = 0.0;
    double gradH = 0.0;
    double psiBefore = 0.0;
    if (inFlight_) {
        trackContact(potential_.maxPenetration(z));
        felt_.update(potential_, z, std::span<S>(force_));
        vNow = potential_.value(z);
        gradH = potential_.hammerGradient(z);
        psiBefore = seg_.psi();
    }

    // SPEC-EXEC step 6: the low-rank solve over the vertical modes of every struck string and the
    // hammer (SPEC-NL: one shared psi).
    const std::array<const nl::Potential<S>*, 1> pots{&potential_};
    const double psiPre = seg_.psi();
    const std::uint32_t resetsPre = seg_.counters().resets;
    nl::savStep<S>(seg_, pots, felt_.dashpots(), std::span<const S>(force_));

    if (seg_.counters().resets != resetsPre) {
        // SPEC-NL drift control 1: on the reset step V = 0, so psi only changed by the reset.
        resetEnergyRemoved_ += 0.5 * (psiPre * psiPre - seg_.psi() * seg_.psi());
    }
    if (inFlight_) {
        if (vNow > 0.0) {
            // Force actually applied to x_H: g_H (psi^{n+1/2} + psi^{n-1/2}) / 2 (SPEC-NL).
            const double gH = gradH / std::sqrt(2.0 * (vNow + seg_.params().eps0));
            peak_ = std::max(peak_, std::abs(gH) * 0.5 * (psiBefore + seg_.psi()));
        }
        // SPEC-HAM Release and check: farther than d_check and moving away.
        const std::span<const S> zNew(seg_.q(), nz);
        if (potential_.maxPenetration(zNew) < -hammer_.dCheck && headVelocity() < 0.0) {
            release();
        }
    }

    // SPEC-EXEC step 7: the horizontal polarisations, driven by the bridge term only.
    if (bridge_ != nullptr) {
        for (String& st : strings_) {
            for (std::size_t i = 0; i < st.n; ++i) {
                st.fH[i] = -st.g[i] * accH;
            }
            osc::stepModes<S, S>(st.qH.data(), st.uH.data(), st.beta.data(), st.kappa.data(),
                                 st.gamma.data(), st.fH.data(), st.n);
        }
        for (auto& p : passive_) {
            p.advance(bridge_->accelV(), accH);
        }
    }
    ++sample_;
}

template class StruckNote<float>;
template class StruckNote<double>;

}  // namespace pme::exciters
