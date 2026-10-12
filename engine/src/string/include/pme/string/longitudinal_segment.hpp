// SPDX-License-Identifier: MIT
// GeometricSegment: one string segment with the default geometric tier (A7-R1, A7-R4, A7-R5,
// A7-R6): both polarisations' modes (on the segment's SAV when tension modulation is on), the
// Kirchhoff-Carrier potential, the one-way longitudinal modes and the bridge hook. C1 reuses it.
//
// SPEC sections implemented: SPEC-GEO (all of "Geometric nonlinearity and longitudinal waves",
// default tier); SPEC-NL "Coordinates", "Discrete scheme", "Skipping", "Drift control"; SPEC-BRG
// "Bridge force", "Discrete coupling order"; SPEC-EXEC per-sample steps 2, 5 to 8; SPEC-OSC
// "Difference form", "Discrete energy".
//
// Per-sample use (SPEC-EXEC):
//   step 2    `addBridgeForces(point)`: F_b,v, F_b,h from q^n and w^n, then the hook
//             kappa_L F_L + kappa_T Delta T through `point.addVerticalForce`;
//   steps 3-4 the caller advances the bridge point;
//   steps 5-8 `advance(point.accelV(), point.accelH())`: modal forces -g_n delta_tt w^n, the SAV
//             step (or the plain difference-form step), then the longitudinal modes with the
//             pair-sum forcing computed from q^n.
// The bridge point is a template parameter (`bridge::BridgePoint` in the engine), so this header
// includes no bridge header; the lifting overlaps g_n and force coefficients a_n are passed in as
// `BridgeCoupling` (from `bridge::liftingOverlaps` and `bridge::forceCoefficients`).
//
// Layout of the SAV coordinates: vertical modes, horizontal modes, then `lumped` coordinates.
// docs/dev/geometric.md.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "pme/instrument.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/nonlinear/tension.hpp"
#include "pme/osc/mode_bank.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"
#include "pme/string/longitudinal.hpp"
#include "pme/string/longitudinal_tier.hpp"

namespace pme::string {

/// Per-segment enable flags and parameters of the default geometric tier (A7-R5).
struct GeometricConfig {
    bool tensionModulation{false};  ///< Kirchhoff-Carrier tension modulation (A7-R1).
    int longitudinalModes{0};       ///< M_L; 0 = longitudinal modes and F_L off (A7-R2 to A7-R4).
    double kappaL{0.0};             ///< Longitudinal transmission kappa_L (dimensionless).
    double kappaT{0.0};             ///< Tension-modulation transmission kappa_T (dimensionless).
    double b1L{0.0};                ///< Longitudinal loss b_1 (1/s); ICR-0002 amendment, default 0.
    double b3L{0.0};                ///< Longitudinal loss b_3 (s); ICR-0002 amendment, default 0.
    double fMax{20000.0};           ///< Highest kept frequency (Hz) for the longitudinal modes.
    std::size_t lumped{0};          ///< Lumped coordinates (hammer ...) after the 2N modes.
    std::size_t maxDashpots{nl::kMaxDashpots};             ///< Dashpot capacity of the SAV segment.
    nl::SavParams sav{};                                   ///< SAV tunables (SPEC-NL).
    GeometricTier tier{GeometricTier::Auto};               ///< `numerics.geometricTier` (A7-R6).
    GeometricQuality quality{GeometricQuality::Realtime};  ///< `EngineConfig::quality`.
};

/// The flags of a segment description as a config (A7-R5): `SegmentDesc::tensionModulation`,
/// `longitudinalModes`, `kappaL`, `kappaT`. The longitudinal losses have no description fields yet
/// (ICR-0002 amendment) and stay 0.
[[nodiscard]] inline GeometricConfig geometricConfigFrom(const SegmentDesc& desc,
                                                         double fMax) noexcept {
    GeometricConfig c;
    c.tensionModulation = desc.tensionModulation;
    c.longitudinalModes = desc.longitudinalModes;
    c.kappaL = desc.kappaL;
    c.kappaT = desc.kappaT;
    c.fMax = fMax;
    return c;
}

/// Bridge-facing coefficients of the segment (SPEC-BRG): g_n (lifting overlaps) and the
/// force coefficients a_n of both polarisations, float64, N entries each.
struct BridgeCoupling {
    std::vector<double> g;   ///< g_n, `bridge::liftingOverlaps`.
    std::vector<double> aV;  ///< a_n of the vertical polarisation, `bridge::forceCoefficients`.
    std::vector<double> aH;  ///< a_n of the horizontal polarisation.
};

/// Maximum number of potentials one `advance` call combines (the tension potential plus extras).
inline constexpr std::size_t kMaxSegmentPotentials = 8;

/// One segment with the default geometric tier. `S` is the state type (float in the engine,
/// double in energy tests).
template <class S>
class GeometricSegment {
public:
    /// Compile the segment. With no feature enabled (and no lumped coordinates) the segment is
    /// the plain linear segment of SPEC-BRG, bit-identical to `bridge::ModalSegment`.
    ///
    /// `InvalidArgument` for inconsistent sizes, invalid parameters or EA - T <= 0 with a
    /// feature enabled (the segment is unchanged); `NotImplemented` when a feature is enabled and
    /// the tier resolves to Exact (phase A10). Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const StringModes& modes, const StringParams& params,
                                   const BridgeCoupling& coupling, const GeometricConfig& config) {
        const std::size_t n = modes.k.size();
        const bool geometric = config.tensionModulation || config.longitudinalModes > 0;
        if (n == 0 || modes.oscV.size() != n || modes.oscH.size() != n || coupling.g.size() != n ||
            coupling.aV.size() != n || coupling.aH.size() != n || !(modes.fs > 0.0) ||
            config.longitudinalModes < 0) {
            return Status::InvalidArgument;
        }
        if (geometric) {
            GeometricTier resolved{};
            if (const Status s = selectGeometricTier(config.tier, config.quality, resolved);
                s != Status::Ok) {
                return s;
            }
        }
        LongitudinalModes lm;
        if (geometric) {
            if (const Status s =
                    compileLongitudinal(modes, params, config.longitudinalModes, config.b1L,
                                        config.b3L, modes.fs, config.fMax, lm);
                s != Status::Ok) {
                return s;
            }
        }
        nl::TensionModulationPotential<S> potential;
        if (config.tensionModulation) {
            std::vector<double> k2(n);
            for (std::size_t i = 0; i < n; ++i) {
                k2[i] = modes.k[i] * modes.k[i];
            }
            // SPEC-GEO: (EA - T) / (2 L).
            if (const Status s = potential.configure(k2, lm.eaMinusT / (2.0 * modes.L));
                s != Status::Ok) {
                return s;
            }
        }
        const bool useSav = config.tensionModulation || config.lumped > 0;
        nl::SavSegment<S> sav;
        if (useSav) {
            if (const Status s =
                    sav.configure(2 * n, config.lumped, config.maxDashpots, modes.fs, config.sav);
                s != Status::Ok) {
                return s;
            }
        }

        // Nothing below fails: commit.
        n_ = n;
        useSav_ = useSav;
        tension_ = config.tensionModulation;
        longitudinal_ = config.longitudinalModes > 0;
        kappaL_ = config.kappaL;
        kappaT_ = config.kappaT;
        hook_ = (longitudinal_ && config.kappaL != 0.0) || (tension_ && config.kappaT != 0.0);
        rhoA_ = modes.rhoA;
        tOverL_ = modes.tension / modes.L;
        dt_ = 1.0 / modes.fs;
        end_ = params.bridgeEnd;
        g_.assign(n, S{});
        aV_.assign(n, S{});
        aH_.assign(n, S{});
        gD_ = coupling.g;
        for (std::size_t i = 0; i < n; ++i) {
            g_[i] = static_cast<S>(coupling.g[i]);
            aV_[i] = static_cast<S>(coupling.aV[i]);
            aH_[i] = static_cast<S>(coupling.aH[i]);
        }
        stored_.assign(2 * n, osc::OscCoeffsF64{});
        beta_.assign(2 * n, S{});
        kappa_.assign(2 * n, S{});
        gamma_.assign(2 * n, S{});
        omegaHat2_.assign(2 * n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            setCoefficients(i, modes.oscV[i]);
            setCoefficients(n + i, modes.oscH[i]);
        }
        qLin_.assign(useSav ? 0 : 2 * n, S{});
        uLin_.assign(useSav ? 0 : 2 * n, S{});
        force_.assign(n, S{});
        expl_.assign(2 * n + config.lumped, S{});
        sav_ = std::move(sav);
        if (useSav_) {
            for (std::size_t i = 0; i < 2 * n; ++i) {
                osc::OscCoeffsF64 c = f64Coefficients(i);
                (void)sav_.setCoordinate(i, c, rhoA_);
            }
            for (std::size_t i = 0; i < config.lumped; ++i) {
                (void)sav_.setLumped(2 * n + i, 1.0);
            }
        }
        potential_ = std::move(potential);
        lm_ = std::move(lm);
        bank_ = LongitudinalBank<S>{};
        fl_.clear();
        if (longitudinal_) {
            bank_.configure(lm_);
            fl_.assign(bank_.size(), S{});
        }
        return Status::Ok;
    }

    /// Zero all states (modes, longitudinal modes, psi). Thread: audio. Allocates: no.
    void reset() noexcept {
        if (useSav_) {
            sav_.resetState();
        } else {
            for (std::size_t i = 0; i < qLin_.size(); ++i) {
                qLin_[i] = uLin_[i] = S{};
            }
        }
        bank_.reset();
    }

    /// Set psi^{n-1/2} for the present state, with the tension potential and `extra` potentials
    /// (SPEC-NL "SAV formulation"), so the first step starts consistent. No-op without SAV.
    /// Thread: any. Allocates: no.
    void initialisePsi(std::span<const nl::Potential<S>* const> extra = {}) noexcept {
        if (!useSav_) {
            return;
        }
        std::array<const nl::Potential<S>*, kMaxSegmentPotentials> pots{};
        const std::size_t np = collectPotentials(extra, pots);
        sav_.initialisePsi(std::span<const nl::Potential<S>* const>(pots.data(), np));
    }

    /// Start the longitudinal modes from rest with the Taylor start u^0 = -gamma f^0 / 2, f^0
    /// the pair-sum forcing of the present transverse state (call once, after setting the
    /// initial pluck). No-op without longitudinal modes. Thread: any. Allocates: no.
    void startLongitudinal() noexcept {
        if (!longitudinal_) {
            return;
        }
        string::longitudinalForce<S>(lm_, std::span<const S>(qV(), n_),
                                     std::span<const S>(qH(), n_), std::span<S>(fl_));
        bank_.startFromRest(std::span<const S>(fl_));
    }

    // ---- state ---------------------------------------------------------------------------
    /// Mode states of the vertical and horizontal polarisation (N entries each). With SAV the
    /// storage is the SAV segment's; `u` = q^{n+1} - q^n of the last step.
    [[nodiscard]] S* qV() noexcept { return useSav_ ? sav_.q() : qLin_.data(); }
    [[nodiscard]] S* uV() noexcept { return useSav_ ? sav_.u() : uLin_.data(); }
    [[nodiscard]] S* qH() noexcept { return qV() + n_; }
    [[nodiscard]] S* uH() noexcept { return uV() + n_; }
    [[nodiscard]] const S* qV() const noexcept { return useSav_ ? sav_.q() : qLin_.data(); }
    [[nodiscard]] const S* uV() const noexcept { return useSav_ ? sav_.u() : uLin_.data(); }
    [[nodiscard]] const S* qH() const noexcept { return qV() + n_; }
    [[nodiscard]] const S* uH() const noexcept { return uV() + n_; }
    /// Longitudinal coordinates xi_m (kept modes).
    [[nodiscard]] S* xi() noexcept { return bank_.q(); }
    [[nodiscard]] const S* xi() const noexcept { return bank_.q(); }
    [[nodiscard]] const S* longitudinalUs() const noexcept { return bank_.u(); }
    [[nodiscard]] std::size_t modeCount() const noexcept { return n_; }
    [[nodiscard]] std::size_t longitudinalCount() const noexcept { return bank_.size(); }
    [[nodiscard]] bool usesSav() const noexcept { return useSav_; }
    [[nodiscard]] bool tensionModulation() const noexcept { return tension_; }
    [[nodiscard]] bool longitudinal() const noexcept { return longitudinal_; }
    [[nodiscard]] nl::SavSegment<S>& sav() noexcept { return sav_; }
    [[nodiscard]] const nl::SavSegment<S>& sav() const noexcept { return sav_; }
    [[nodiscard]] const nl::TensionModulationPotential<S>& potential() const noexcept {
        return potential_;
    }
    [[nodiscard]] const LongitudinalModes& longitudinalModes() const noexcept { return lm_; }
    /// Pair-sum forcing of the last `advance` (kept entries).
    [[nodiscard]] std::span<const S> longitudinalForceBuffer() const noexcept { return fl_; }

    // ---- bridge side ---------------------------------------------------------------------
    /// F_b,s for the vertical polarisation from q^n and w_v^n (SPEC-BRG "Bridge force"); the
    /// same arithmetic as `bridge::bridgeForce`. Thread: audio. Allocates: no.
    [[nodiscard]] S forceV(S wV) const noexcept { return bridgeForce(aV_, qV(), wV); }
    /// F_b,s for the horizontal polarisation. Thread: audio. Allocates: no.
    [[nodiscard]] S forceH(S wH) const noexcept { return bridgeForce(aH_, qH(), wH); }

    /// Delta T = (EA - T) S / (2 L) in N from q^n (SPEC-GEO); 0 with tension modulation off.
    [[nodiscard]] double deltaT() const noexcept {
        if (!tension_) {
            return 0.0;
        }
        return potential_.deltaT(std::span<const S>(sav_.q(), 2 * n_));
    }

    /// F_L (SPEC-GEO "Longitudinal force at the bridge") from xi^n and q^n; 0 with the
    /// longitudinal modes off. Thread: audio. Allocates: no.
    [[nodiscard]] double longitudinalBridgeForce() const noexcept {
        if (!longitudinal_) {
            return 0.0;
        }
        return string::longitudinalBridgeForce<S>(lm_, std::span<const S>(bank_.q(), bank_.size()),
                                                  std::span<const S>(qV(), n_),
                                                  std::span<const S>(qH(), n_));
    }

    /// kappa_L F_L + kappa_T Delta T (SPEC-GEO "Transmission to the body"). Thread: audio.
    [[nodiscard]] S verticalBridgeExtra() const noexcept {
        double extra = 0.0;
        if (longitudinal_) {
            extra += kappaL_ * longitudinalBridgeForce();
        }
        if (tension_) {
            extra += kappaT_ * deltaT();
        }
        return static_cast<S>(extra);
    }

    /// SPEC-EXEC step 2: add F_b,v and F_b,h of this segment to `point` (`addForce`), then the
    /// hook kappa_L F_L + kappa_T Delta T on the vertical axis (`addVerticalForce`, only when it
    /// is non-zero by construction: a segment with both transmissions off never calls it).
    /// Returns the total vertical force this segment contributed. `Point` is
    /// `bridge::BridgePoint<S>`. Thread: audio. Allocates: no.
    template <class Point>
    S addBridgeForces(Point& point) const noexcept {
        const S fv = forceV(point.wV());
        const S fh = forceH(point.wH());
        point.addForce(fv, fh);
        if (!hook_) {
            return fv;
        }
        const S extra = verticalBridgeExtra();
        point.addVerticalForce(extra);
        return fv + extra;
    }

    /// SPEC-EXEC steps 5 to 8: advance the segment one sample with the bridge accelerations
    /// delta_tt w^n of its point (`BridgePoint::accelV()` / `accelH()`), plus optional extra
    /// potentials and dashpots on the same SAV (a hammer, A5; they must outlive the call). The
    /// pair-sum forcing is computed from q^n before the transverse update, then the
    /// longitudinal modes advance with it (one-way, SI-0015). Thread: audio. Allocates: no.
    void advance(S accV, S accH, std::span<const nl::Potential<S>* const> extra = {},
                 std::span<const nl::Dashpot<S>> dashpots = {}) noexcept {
        if (longitudinal_) {
            string::longitudinalForce<S>(lm_, std::span<const S>(qV(), n_),
                                         std::span<const S>(qH(), n_), std::span<S>(fl_));
        }
        if (useSav_) {
            for (std::size_t i = 0; i < n_; ++i) {
                // F^n = -rhoA g_n delta_tt w^n in N; savStep divides by the mass rhoA.
                expl_[i] = static_cast<S>(-rhoA_ * gD_[i] * static_cast<double>(accV));
                expl_[n_ + i] = static_cast<S>(-rhoA_ * gD_[i] * static_cast<double>(accH));
            }
            std::array<const nl::Potential<S>*, kMaxSegmentPotentials> pots{};
            const std::size_t np = collectPotentials(extra, pots);
            nl::savStep<S>(sav_, std::span<const nl::Potential<S>* const>(pots.data(), np),
                           dashpots, std::span<const S>(expl_));
        } else {
            // Plain difference form, the arithmetic of bridge::ModalSegment::advance.
            for (std::size_t i = 0; i < n_; ++i) {
                force_[i] = -g_[i] * accV;
            }
            osc::stepModes<S, S>(qLin_.data(), uLin_.data(), beta_.data(), kappa_.data(),
                                 gamma_.data(), force_.data(), n_);
            for (std::size_t i = 0; i < n_; ++i) {
                force_[i] = -g_[i] * accH;
            }
            osc::stepModes<S, S>(qLin_.data() + n_, uLin_.data() + n_, beta_.data() + n_,
                                 kappa_.data() + n_, gamma_.data() + n_, force_.data(), n_);
        }
        if (longitudinal_) {
            bank_.step(std::span<const S>(fl_));
        }
    }

    // ---- energy (float64, SI-0015) ---------------------------------------------------------
    /// Transverse energy of the segment at time level n + 1/2 after `advance`: the modes of both
    /// polarisations with the moving-support shift, the static (T/L) w^n w^{n+1} / 2 terms, and,
    /// with SAV, psi^2 / 2 - eps0 (which carries V_KC). `wVnext`, `dwV`, `wHnext`, `dwH` are
    /// `BridgePoint::wV()` and `incrementV()` after the advance (likewise h); a rigid bridge
    /// passes zeros. Lumped coordinates are not included. Thread: any. Allocates: no.
    [[nodiscard]] double transverseEnergy(double wVnext, double dwV, double wHnext,
                                          double dwH) const noexcept {
        double e = polarisationEnergy(qV(), uV(), omegaHat2_.data(), wVnext, dwV) +
                   polarisationEnergy(qH(), uH(), omegaHat2_.data() + n_, wHnext, dwH);
        if (useSav_ && tension_) {
            e += 0.5 * sav_.psi() * sav_.psi() - sav_.params().eps0;
        }
        return e;
    }
    /// E_L = sum_m rhoA H_m^L (SPEC-GEO, SI-0015): the one-way longitudinal energy, reported
    /// separately; it is not conserved. Thread: any. Allocates: no.
    [[nodiscard]] double longitudinalEnergy() const noexcept {
        return longitudinal_ ? bank_.energy() : 0.0;
    }

private:
    /// F_b = sum_n a_n q_n - (T/L) w_b: the arithmetic of `bridge::bridgeForce` (SPEC-BRG).
    [[nodiscard]] S bridgeForce(const std::vector<S>& a, const S* q, S wb) const noexcept {
        S acc = S{0};
        for (std::size_t i = 0; i < a.size(); ++i) {
            acc += a[i] * q[i];  // SPEC-BRG Bridge force: sum a_n q_n
        }
        return acc - static_cast<S>(tOverL_) * wb;  // SPEC-BRG Bridge force: - (T/L) w_b
    }

    [[nodiscard]] std::size_t collectPotentials(
        std::span<const nl::Potential<S>* const> extra,
        std::array<const nl::Potential<S>*, kMaxSegmentPotentials>& pots) const noexcept {
        std::size_t np = 0;
        if (tension_) {
            pots[np++] = &potential_;
        }
        for (const nl::Potential<S>* p : extra) {
            if (np < pots.size()) {
                pots[np++] = p;
            }
        }
        return np;
    }

    void setCoefficients(std::size_t i, const OscCoeffs& osc) noexcept {
        // SPEC-OSC Difference form from the stored float64 omegaHat^2 and sigmaHat (as A4).
        const osc::OscCoeffsF64 c = f64From(osc);
        beta_[i] = static_cast<S>(c.beta);
        kappa_[i] = static_cast<S>(c.kappa);
        gamma_[i] = static_cast<S>(c.gamma);
        omegaHat2_[i] = c.omegaHat2;
        stored_[i] = c;
    }

    [[nodiscard]] osc::OscCoeffsF64 f64From(const OscCoeffs& osc) const noexcept {
        osc::OscCoeffsF64 c;
        const double sd = osc.sigmaHat * dt_;
        const double denom = 1.0 + sd;
        c.sigmaHatDt = sd;
        c.omegaHat2Dt2 = osc.omegaHat2 * dt_ * dt_;
        c.beta = (1.0 - sd) / denom;
        c.kappa = osc.omegaHat2 * dt_ * dt_ / denom;
        c.gamma = dt_ * dt_ / denom;
        c.omegaHat2 = osc.omegaHat2;
        c.sigmaHat = osc.sigmaHat;
        return c;
    }
    [[nodiscard]] osc::OscCoeffsF64 f64Coefficients(std::size_t i) const noexcept {
        return stored_[i];
    }

    [[nodiscard]] double polarisationEnergy(const S* q, const S* u, const double* omegaHat2,
                                            double wNext, double dw) const noexcept {
        double sum = 0.0;
        for (std::size_t i = 0; i < n_; ++i) {
            const auto qNext = static_cast<double>(q[i]);
            const auto uNext = static_cast<double>(u[i]);
            const double v = (uNext + static_cast<double>(g_[i]) * dw) / dt_;
            sum += 0.5 * v * v + 0.5 * omegaHat2[i] * (qNext - uNext) * qNext;
        }
        return rhoA_ * sum + 0.5 * tOverL_ * (wNext - dw) * wNext;
    }

    std::size_t n_{0};
    bool useSav_{false};
    bool tension_{false};
    bool longitudinal_{false};
    bool hook_{false};
    double kappaL_{0.0};
    double kappaT_{0.0};
    double rhoA_{0.0};
    double tOverL_{0.0};
    double dt_{1.0};
    BridgeEnd end_{BridgeEnd::AtL};
    std::vector<S> g_, aV_, aH_, beta_, kappa_, gamma_, qLin_, uLin_, force_, expl_, fl_;
    std::vector<double> gD_, omegaHat2_;
    std::vector<osc::OscCoeffsF64> stored_;
    nl::SavSegment<S> sav_;
    nl::TensionModulationPotential<S> potential_;
    LongitudinalModes lm_;
    LongitudinalBank<S> bank_;
};

}  // namespace pme::string
