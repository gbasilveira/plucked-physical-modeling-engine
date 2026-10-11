// SPDX-License-Identifier: MIT
// SAV framework: segment storage, low-rank solve, one-sample update, energy (A3-R1 to A3-R5).
//
// SPEC sections implemented: SPEC-NL (all of "Nonlinear framework"); SPEC-OSC "Difference form",
// "Discrete energy". Derivation of the signs: docs/dev/nonlinear.md.
#include "pme/nonlinear/sav.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pme::nl {

namespace {

constexpr std::size_t kLane = osc::kBankLane;

[[nodiscard]] std::size_t padTo(std::size_t n) noexcept { return (n + kLane - 1) / kLane * kLane; }

}  // namespace

// ---- SavSegment ----------------------------------------------------------------------------

template <class S>
Status SavSegment<S>::configure(std::size_t nModes, std::size_t nLumped, std::size_t maxDashpots,
                                double fs, const SavParams& params) {
    const std::size_t n = nModes + nLumped;
    if (!(fs > 0.0) || !std::isfinite(fs) || n == 0 || maxDashpots > kMaxDashpots ||
        !(params.eps0 > 0.0) || !(params.driftAfterSeconds >= 0.0) || !(params.driftTau > 0.0)) {
        return Status::InvalidArgument;
    }
    n_ = n;
    nModes_ = nModes;
    padded_ = padTo(n);
    maxDashpots_ = maxDashpots;
    fs_ = fs;
    dt_ = 1.0 / fs;
    params_ = params;
    counters_ = SavCounters{};
    for (auto* arr : {&q_, &u_, &qPrev_, &uPrev_, &beta_, &kappa_, &gamma_, &force_}) {
        arr->allocate(padded_);
    }
    for (auto* arr : {&omegaHat2_, &mass_, &zPrev_, &a_, &g_, &gradTmp_}) {
        arr->allocate(padded_);
    }
    for (std::size_t j = 0; j < kMaxRank; ++j) {
        h_[j].allocate(padded_);
        w_[j].allocate(padded_);
    }
    for (std::size_t i = 0; i < n; ++i) {
        mass_.data()[i] = 1.0;
    }
    deriveConstants();
    resetState();
    return Status::Ok;
}

template <class S>
void SavSegment<S>::deriveConstants() noexcept {
    driftThresholdSamples_ = static_cast<std::uint32_t>(std::llround(params_.driftAfterSeconds * fs_));
    driftDecay_ = std::exp(-dt_ / params_.driftTau);
}

template <class S>
void SavSegment<S>::setParams(const SavParams& params) noexcept {
    if (!(params.eps0 > 0.0) || !(params.driftAfterSeconds >= 0.0) || !(params.driftTau > 0.0)) {
        return;
    }
    params_ = params;
    deriveConstants();
}

template <class S>
Status SavSegment<S>::setCoordinate(std::size_t i, const osc::OscCoeffsF64& c,
                                    double mass) noexcept {
    if (i >= n_ || !(mass > 0.0) || !std::isfinite(mass)) {
        return Status::InvalidArgument;
    }
    beta_.data()[i] = static_cast<S>(c.beta);
    kappa_.data()[i] = static_cast<S>(c.kappa);
    gamma_.data()[i] = static_cast<S>(c.gamma);
    omegaHat2_.data()[i] = c.omegaHat2;
    mass_.data()[i] = mass;
    return Status::Ok;
}

template <class S>
Status SavSegment<S>::setLumped(std::size_t i, double mass) noexcept {
    osc::OscCoeffsF64 c;
    c.beta = 1.0;       // SPEC-NL "Coordinates": omegaHat = sigmaHat = 0 ...
    c.kappa = 0.0;
    c.gamma = dt_ * dt_;  // ... so gamma = dt^2.
    return setCoordinate(i, c, mass);
}

template <class S>
void SavSegment<S>::resetState() noexcept {
    for (std::size_t i = 0; i < padded_; ++i) {
        q_.data()[i] = S{};
        u_.data()[i] = S{};
        qPrev_.data()[i] = S{};
        uPrev_.data()[i] = S{};
    }
    psi_ = std::sqrt(2.0 * params_.eps0);
    vPrev_ = 0.0;
    contactSamples_ = 0;
}

template <class S>
void SavSegment<S>::initialisePsi(std::span<const Potential<S>* const> potentials) noexcept {
    const std::span<const S> z(q_.data(), n_);
    double v = 0.0;
    for (const Potential<S>* p : potentials) {
        v += p->value(z);
    }
    psi_ = std::sqrt(2.0 * (v + params_.eps0));  // SPEC-NL SAV formulation
    vPrev_ = v;
}

template class SavSegment<float>;
template class SavSegment<double>;

// ---- low-rank solve --------------------------------------------------------------------------

void lowRankSolve(double* z, const double* a, std::size_t n, std::size_t r, const double* const* h,
                  const double* const* w) noexcept {
    if (r == 0) {
        for (std::size_t i = 0; i < n; ++i) {
            z[i] = a[i];
        }
        return;
    }
    if (r == 1) {
        // SPEC-NL explicit low-rank solve, r = 1: z = a - h (w^T a) / (1 + w^T h).
        double wa = 0.0;
        double wh = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            wa += w[0][i] * a[i];
            wh += w[0][i] * h[0][i];
        }
        const double k = wa / (1.0 + wh);
        for (std::size_t i = 0; i < n; ++i) {
            z[i] = a[i] - h[0][i] * k;
        }
        return;
    }
    // SPEC-NL explicit low-rank solve, r = 2..4: z = a - H (I + W^T H)^{-1} W^T a (Woodbury).
    std::array<std::array<double, kMaxRank + 1>, kMaxRank> m{};  // augmented [M | W^T a]
    for (std::size_t j = 0; j < r; ++j) {
        for (std::size_t k = 0; k < r; ++k) {
            double s = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                s += w[j][i] * h[k][i];
            }
            m[j][k] = s + (j == k ? 1.0 : 0.0);
        }
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            s += w[j][i] * a[i];
        }
        m[j][r] = s;
    }
    // Gaussian elimination with partial pivoting. M has real eigenvalues >= 1 (H and W are
    // positive diagonal scalings of the same directions), so it is never singular.
    for (std::size_t col = 0; col < r; ++col) {
        std::size_t piv = col;
        for (std::size_t row = col + 1; row < r; ++row) {
            if (std::abs(m[row][col]) > std::abs(m[piv][col])) {
                piv = row;
            }
        }
        assert(m[piv][col] != 0.0 && "SPEC-NL: I + W^T H is never singular");
        std::swap(m[piv], m[col]);
        const double pivot = m[col][col];
        for (std::size_t row = col + 1; row < r; ++row) {
            const double factor = m[row][col] / pivot;
            for (std::size_t k = col; k <= r; ++k) {
                m[row][k] -= factor * m[col][k];
            }
        }
    }
    std::array<double, kMaxRank> y{};
    for (std::size_t jj = r; jj-- > 0;) {
        double s = m[jj][r];
        for (std::size_t k = jj + 1; k < r; ++k) {
            s -= m[jj][k] * y[k];
        }
        y[jj] = s / m[jj][jj];
    }
    for (std::size_t i = 0; i < n; ++i) {
        double s = a[i];
        for (std::size_t k = 0; k < r; ++k) {
            s -= h[k][i] * y[k];
        }
        z[i] = s;
    }
}

// ---- the step --------------------------------------------------------------------------------

template <class S>
void savStep(SavSegment<S>& seg, std::span<const Potential<S>* const> potentials,
             std::span<const Dashpot<S>> dashpots, std::span<const S> explicitForce) noexcept {
    const std::size_t n = seg.n_;
    const double dt = seg.dt_;
    S* const q = seg.q_.data();
    S* const u = seg.u_.data();
    S* const qPrev = seg.qPrev_.data();
    S* const uPrev = seg.uPrev_.data();
    const double* const mass = seg.mass_.data();
    const S* const beta = seg.beta_.data();
    const S* const kappa = seg.kappa_.data();
    const S* const gamma = seg.gamma_.data();
    S* const f = seg.force_.data();

    // Force per unit mass, F^n / m_i (SPEC-NL discrete scheme).
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = (i < explicitForce.size()) ? static_cast<S>(static_cast<double>(explicitForce[i]) / mass[i])
                                          : S{};
    }

    // V^n = sum of the potentials (SPEC-NL SAV formulation: several potentials add).
    const std::span<const S> z(q, n);
    double vNow = 0.0;
    for (const Potential<S>* p : potentials) {
        vNow += p->value(z);
    }
    const std::size_t nDash = std::min(dashpots.size(), seg.maxDashpots_);
    bool anyDamping = false;
    for (std::size_t j = 0; j < nDash; ++j) {
        anyDamping = anyDamping || dashpots[j].R > 0.0;
    }

    // Contact duration counter (for the persistent-contact rule).
    if (vNow > 0.0) {
        if (seg.contactSamples_ < std::numeric_limits<std::uint32_t>::max()) {
            ++seg.contactSamples_;
        }
    } else {
        seg.contactSamples_ = 0;
    }

    // SPEC-NL Drift control 1 (MUST): reset at separation, V^n = V^{n-1} = 0.
    if (vNow == 0.0 && seg.vPrev_ == 0.0) {
        const double psi0 = std::sqrt(2.0 * seg.params_.eps0);
        // psi^{n-1/2} is a linear variable and may be negative; the reset acts when |psi| is above
        // sqrt(2 eps0), which lowers psi^2 and so only removes energy (SPEC-NL Drift control 1).
        if (std::abs(seg.psi_) > psi0) {
            seg.psi_ = psi0;
            ++seg.counters_.resets;
        }
    }
    // SPEC-NL Drift control 2 (SHOULD): relax psi towards sqrt(2(V + eps0)) when above it, once
    // the contact has lasted longer than the threshold.
    if (seg.params_.driftRelaxation && vNow > 0.0 && seg.contactSamples_ > seg.driftThresholdSamples_) {
        const double psiStar = std::sqrt(2.0 * (vNow + seg.params_.eps0));
        const double psiMag = std::abs(seg.psi_);
        if (psiMag > psiStar) {  // lowers |psi|, hence psi^2: only removes energy
            const double relaxed = psiStar + (psiMag - psiStar) * seg.driftDecay_;
            seg.psi_ = seg.psi_ < 0.0 ? -relaxed : relaxed;
            ++seg.counters_.driftCorrections;
        }
    }

    // SPEC-NL Skipping: V^n = 0 and every R^n = 0 -> free oscillator update, no gradient, no solve.
    if (vNow == 0.0 && !anyDamping) {
        osc::stepModes<S, S>(q, u, beta, kappa, gamma, f, n, qPrev, uPrev);  // SPEC-OSC difference form
        seg.vPrev_ = 0.0;
        ++seg.counters_.solvesSkipped;
        return;
    }

    // g^n = grad V^n / sqrt(2 (V^n + eps0)) (SPEC-NL SAV formulation).
    double* const g = seg.g_.data();
    bool gActive = false;
    double gg = 0.0;
    if (vNow > 0.0) {
        const std::span<double> gradOut(g, n);
        const std::span<double> gradTmp(seg.gradTmp_.data(), n);
        bool first = true;
        for (const Potential<S>* p : potentials) {
            if (first) {
                p->gradient(z, gradOut);
                first = false;
            } else {
                p->gradient(z, gradTmp);
                for (std::size_t i = 0; i < n; ++i) {
                    g[i] += gradTmp[i];
                }
            }
        }
        const double invDen = 1.0 / std::sqrt(2.0 * (vNow + seg.params_.eps0));
        for (std::size_t i = 0; i < n; ++i) {
            g[i] *= invDen;
            gg += g[i] * g[i];
        }
        gActive = gg > 0.0;
    }

    // Columns of H and W. SPEC-NL: SAV pair h_g = gamma g / m, w = g / 4; dashpot pair
    // h_c = R gamma c / m, w = c / (2 dt). A dashpot along g (c = lambda g) merges into the SAV
    // column: h_c w_c^T = R lambda^2 h_g g^T / (2 dt), so w_g = g (1/4 + sum R lambda^2 / (2 dt)).
    std::array<double*, kMaxRank> hCol{};
    std::array<double*, kMaxRank> wCol{};
    std::array<const double*, kMaxRank> hConst{};
    std::array<const double*, kMaxRank> wConst{};
    std::size_t r = 0;
    double wScale = 0.25;
    if (gActive) {
        for (std::size_t j = 0; j < nDash; ++j) {
            const Dashpot<S>& d = dashpots[j];
            if (d.R > 0.0 && d.alongGradient) {
                double cg = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    cg += static_cast<double>(d.c[i]) * g[i];
                }
                const double lambda = cg / gg;
                wScale += d.R * lambda * lambda / (2.0 * dt);
            }
        }
        hCol[0] = seg.h_[0].data();
        wCol[0] = seg.w_[0].data();
        for (std::size_t i = 0; i < n; ++i) {
            hCol[0][i] = static_cast<double>(gamma[i]) * g[i] / mass[i];
            wCol[0][i] = g[i] * wScale;
        }
        r = 1;
    }
    for (std::size_t j = 0; j < nDash; ++j) {
        const Dashpot<S>& d = dashpots[j];
        if (!(d.R > 0.0) || (gActive && d.alongGradient)) {
            continue;
        }
        hCol[r] = seg.h_[r].data();
        wCol[r] = seg.w_[r].data();
        for (std::size_t i = 0; i < n; ++i) {
            const double ci = static_cast<double>(d.c[i]);
            hCol[r][i] = d.R * static_cast<double>(gamma[i]) * ci / mass[i];
            wCol[r][i] = ci / (2.0 * dt);
        }
        ++r;
    }
    for (std::size_t j = 0; j < r; ++j) {
        hConst[j] = hCol[j];
        wConst[j] = wCol[j];
    }

    // z^{n-1} = q^n - u^n; d_j = w_j^T z^{n-1} (the known part of each rank-one term).
    double* const zPrev = seg.zPrev_.data();
    for (std::size_t i = 0; i < n; ++i) {
        zPrev[i] = static_cast<double>(q[i]) - static_cast<double>(u[i]);
        qPrev[i] = q[i];
        uPrev[i] = u[i];
    }
    std::array<double, kMaxRank> known{};
    for (std::size_t j = 0; j < r; ++j) {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            s += wCol[j][i] * zPrev[i];
        }
        known[j] = s;
    }

    // a = free update + sum_j h_j (w_j^T z^{n-1}) - h_g psi^{n-1/2}  (SPEC-NL explicit low-rank solve).
    double* const a = seg.a_.data();
    const double psi = seg.psi_;
    for (std::size_t i = 0; i < n; ++i) {
        const S uFree = beta[i] * u[i] - kappa[i] * q[i] + gamma[i] * f[i];  // SPEC-OSC difference form
        double ai = static_cast<double>(q[i]) + static_cast<double>(uFree);
        for (std::size_t j = 0; j < r; ++j) {
            ai += hCol[j][i] * known[j];
        }
        if (gActive) {
            ai -= hCol[0][i] * psi;
        }
        a[i] = ai;
    }

    // z^{n+1} = a - H (I + W^T H)^{-1} W^T a  (Woodbury, SPEC-NL).
    lowRankSolve(a, a, n, r, hConst.data(), wConst.data());

    // u^{n+1} = z^{n+1} - q^n; q^{n+1} = q^n + u^{n+1}; psi^{n+1/2} = psi^{n-1/2} + 1/2 g^T (z^{n+1} - z^{n-1}).
    double dPsi = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const S uNew = static_cast<S>(a[i] - static_cast<double>(q[i]));
        if (gActive) {
            dPsi += g[i] * (static_cast<double>(uNew) + static_cast<double>(uPrev[i]));
        }
        u[i] = uNew;
        q[i] = q[i] + uNew;
    }
    if (gActive) {
        seg.psi_ = psi + 0.5 * dPsi;
    }
    seg.vPrev_ = vNow;
}

template void savStep<float>(SavSegment<float>&, std::span<const Potential<float>* const>,
                             std::span<const Dashpot<float>>, std::span<const float>) noexcept;
template void savStep<double>(SavSegment<double>&, std::span<const Potential<double>* const>,
                              std::span<const Dashpot<double>>, std::span<const double>) noexcept;

// ---- energy ----------------------------------------------------------------------------------

template <class S>
double savEnergy(const SavSegment<S>& seg) noexcept {
    double e = 0.0;
    const std::size_t n = seg.size();
    for (std::size_t i = 0; i < n; ++i) {
        // SPEC-NL: sum m_i H_i, H_i from SPEC-OSC discrete energy.
        e += seg.mass()[i] * osc::modeEnergy(seg.omegaHat2()[i], static_cast<double>(seg.qPrev()[i]),
                                             static_cast<double>(seg.q()[i]),
                                             static_cast<double>(seg.u()[i]), seg.dt());
    }
    return e + 0.5 * seg.psi() * seg.psi() - seg.params().eps0;
}

template <class S>
double savDissipation(const SavSegment<S>& seg, std::span<const Dashpot<S>> dashpots) noexcept {
    const std::size_t n = seg.size();
    const double dt = seg.dt();
    const std::size_t nDash = std::min(dashpots.size(), seg.maxDashpots());
    double total = 0.0;
    for (std::size_t j = 0; j < nDash; ++j) {
        const Dashpot<S>& d = dashpots[j];
        if (!(d.R > 0.0)) {
            continue;
        }
        double cv = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            cv += static_cast<double>(d.c[i]) *
                  ((static_cast<double>(seg.u()[i]) + static_cast<double>(seg.uPrev()[i])) /
                   (2.0 * dt));  // delta_t. z^n
        }
        total += d.R * cv * cv;
    }
    return dt * total;  // SPEC-NL: dashpot power R (c^T delta_t. z)^2 times dt
}

template double savEnergy<float>(const SavSegment<float>&) noexcept;
template double savEnergy<double>(const SavSegment<double>&) noexcept;
template double savDissipation<float>(const SavSegment<float>&,
                                      std::span<const Dashpot<float>>) noexcept;
template double savDissipation<double>(const SavSegment<double>&,
                                       std::span<const Dashpot<double>>) noexcept;

}  // namespace pme::nl
