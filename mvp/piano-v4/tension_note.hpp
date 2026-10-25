// SPDX-License-Identifier: MIT
// 0.5.2: a struck note whose SAV segment carries the Kirchhoff-Carrier tension potential next to
// the hammer potential. MVP-local adaptation of pme::exciters::StruckNote
// (engine/src/exciters/hammer.cpp, hammer.hpp); the engine is not modified. Differences from the
// original: (1) one extra potential, V_KC = sum_s c_s S_s^2 / 4 with S_s = sum_n k_n^2 q_s,n^2
// over the *vertical* modes of string s (SPEC-GEO "Tension modulation", formulas of
// pme::nl::TensionModulationPotential, whose 2N-block layout cannot address one slice of a
// stacked multi-string segment), sharing the segment's single psi (SPEC-NL "SAV formulation");
// (2) Hunt-Crossley dashpots are never merged into the SAV column while the tension is active
// (the gradient is no longer parallel to the felt direction, rank 1 + dashpots <= 4); (3) no
// flexible shank, no passive segments (unused by the piano). With tension off, step() is the
// original arithmetic and the output is bit-for-bit that of StruckNote.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include "pme/bridge/bridge_point.hpp"
#include "pme/exciters/hammer.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/string.hpp"
#include "contact.hpp"

namespace piano_mvp_v4 {
// V = sum_s c_s S_s^2 / 4, S_s = sum_n k2_n q_n^2 over string s's vertical modes (SPEC-GEO).
class VerticalTensionPotential final : public pme::nl::Potential<double> {
public:
    struct Slice { std::size_t offset{}; std::vector<double> k2; double c{}; };
    void configure(std::vector<Slice> s) { slices_ = std::move(s); }
    bool empty() const noexcept { return slices_.empty(); }
    // S_s (m) of string s.
    double sum(std::span<const double> z, std::size_t s) const noexcept;
    // Delta T_s = c_s S_s (N).
    double deltaT(std::span<const double> z, std::size_t s) const noexcept { return slices_[s].c * sum(z, s); }
    double value(std::span<const double> z) const noexcept override;
    void gradient(std::span<const double> z, std::span<double> grad) const noexcept override;
    std::size_t strings() const noexcept { return slices_.size(); }
    // 0.5.4: fast reassociated sums (kern::sumSqFast) instead of the sequential reference sum.
    void setFast(bool f) noexcept { fast_ = f; }
    // Modes beyond limit[s] are zero (truncated voice): the sum stops there.
    void setLimits(const std::size_t* limit) noexcept { limits_ = limit; }
private:
    bool fast_{false};
    const std::size_t* limits_{nullptr};
    std::vector<Slice> slices_;
};

class TensionStruckNote {
public:
    // `axialStiffness[s]` = EA of string s in N (0 or empty: no tension modulation for the note).
    pme::Status configure(const pme::exciters::StruckNoteDesc& desc, double fs,
                          pme::bridge::BridgePoint<double>* bridge, std::span<const double> axialStiffness = {});
    pme::Status launch(double nu, double shift) noexcept;
    pme::Status setShift(double shift) noexcept;
    void step() noexcept;
    void reset() noexcept;
    // Tension potential below this value (J) is treated as zero (the skip rule applies again);
    // 0 disables the gate (energy tests). Default 1e-14 J = 1% of eps0, DeltaT < ~2e-5 N.
    void setTensionFloor(double joules) noexcept { floor_ = joules; }
    // After the owner scaled the string state (damper), lower |psi| to sqrt(2(V + eps0)) if above
    // (only removes energy; SPEC-NL Drift control 2 applied immediately).
    void clampPsi() noexcept;
    // ---- 0.5.5 imperfections ----
    // Per-string contact strength factor (multiplies the strike weight w_s, > 0) and contact lag (s, >= 0:
    // string s is touched about `lag` later than the first one; the gap is lag x hammer speed, so
    // the timing difference does not grow at soft touch). 1 and 0 = 0.5.4. Takes effect at the next launch(). The potential stays convex and passive for any positive factor.
    void setContactImperfection(const std::array<double, 3>& strength, const std::array<double, 3>& lag) noexcept {
        strength_ = strength; lag_ = lag;
    }
    // Retune string s of this note: every vertical and horizontal mode frequency of the compiled string
    // `m` is multiplied by ratio[i] (squares enter the oscillator: omegaHat^2 -> omegaHat^2 ratio^2,
    // damping unchanged). ratio[i] == 1 for all i restores the compiled string bit for bit. Keeps the
    // state; no allocation. `m` must be the string passed to configure().
    void retuneString(std::size_t s, const pme::StringModes& m, std::span<const double> ratio) noexcept;
    // ---- 0.5.4 ----
    // Runtime gate for the tension potential (a note configured with tension behaves as 0.5.1 when
    // off; applied by the owner at note-on, not mid-note).
    void setTensionEnabled(bool on) noexcept { tensionOn_ = on; }
    bool tensionEnabled() const noexcept { return tensionOn_ && !tension_.empty(); }
    // Fast reassociated sums in the bridge-force and tension dot products (not bit-identical).
    void setFastKernels(bool f) noexcept { fast_ = f; dotValid_ = false; tension_.setFast(f); }
    // Tension potential floor (J): below it the cheap free update is used (default 1e-14).
    double tensionFloor() const noexcept { return floor_; }
    // Copy of the compiled float bridge coefficients of string s (SegmentBridgeTerms::a), widened.
    void setTermCoefficients(std::size_t s, std::span<const float> a, double tOverL);
    // Total vertical bridge force of all strings for bridge displacement w (what Piano used to sum
    // from SegmentBridgeTerms): exact sequential sums unless fast kernels are on.
    double bridgeForceNow(double w) noexcept;
    // Mode truncation: after every `truncate(peak)` call the highest modes whose contribution
    // |a_n| sqrt(q^2 + (u/(omega dt))^2) is below `relative * peak` are set to zero and skipped
    // until the next launch(). Zeroing only removes energy. relative 0 disables.
    void setTruncation(double relative) noexcept { truncRel_ = relative; }
    // The owner changed string state behind the note's back (hand-over from an unstruck string).
    void invalidateForceCache() noexcept { dotValid_ = false; }
    void truncate(double peakForce) noexcept;
    std::size_t activeModes(std::size_t s) const noexcept { return nAct_[s]; }
    bool truncated() const noexcept {
        for (std::size_t s = 0; s < strings_.size(); ++s) if (nAct_[s] < strings_[s].n) return true;
        return false;
    }
    // Number of vertical mode updates done by one step (CPU accounting).
    std::size_t activeModeTotal() const noexcept {
        std::size_t t = 0; for (std::size_t s = 0; s < strings_.size(); ++s) t += nAct_[s]; return t;
    }
    pme::nl::SavSegment<double>& segment() noexcept { return seg_; }
    const pme::nl::SavSegment<double>& segment() const noexcept { return seg_; }
    bool inFlight() const noexcept { return inFlight_; }
    bool tensionConfigured() const noexcept { return !tension_.empty(); }
    std::size_t stringCount() const noexcept { return strings_.size(); }
    std::size_t modeOffset(std::size_t s) const noexcept { return strings_[s].off; }
    std::size_t modeCount(std::size_t s) const noexcept { return strings_[s].n; }
    double headPosition() const noexcept;
    double headVelocity() const noexcept;
    const pme::exciters::HammerDiagnostics& diagnostics() const noexcept { return diag_; }
    void clearDiagnostics() noexcept { diag_ = pme::exciters::HammerDiagnostics{}; }
    double releasedEnergy() const noexcept { return releasedEnergy_; }
    double resetEnergyRemoved() const noexcept { return resetEnergyRemoved_; }
    double energy() const noexcept { return pme::nl::savEnergy(seg_); }
    // Extra tension DeltaT of string s (N) and S_s, for diagnostics.
    double deltaT(std::size_t s) const noexcept {
        return tension_.empty() ? 0.0 : tension_.deltaT(std::span<const double>(seg_.q(), seg_.size()), s);
    }
private:
    struct String {
        std::size_t off{}, n{};
        double rhoA{}, tOverL{};
        std::vector<double> aV, aH, forceCoef, g, beta, kappa, gamma, qH, uH, fH, wdt, aTerm, gf;
        double tOverLTerm{};
    };
    void zeroTruncated() noexcept;
    // Fast hammer-flight SAV step (single hammer potential, rank one); false: use the engine step.
    bool flightFast(double vNow, std::span<const pme::nl::Dashpot<double>> dashpots) noexcept;
    std::vector<double> fg_, fz_, fa_, invMass_;
    std::array<std::vector<double>, 4> fh_, fw_;
    std::uint32_t fastContact_{0}, fastDriftThreshold_{0};
    double fastDriftDecay_{0};
    bool tryBypass(double accV, double accH, double* vTensionOut) noexcept;
    void stepHorizontal(double accH) noexcept;
    void closeContact(double endSample) noexcept;
    void release() noexcept;
    void trackContact(double maxDelta) noexcept;
    using P = ContactPotential;
    pme::nl::SavSegment<double> seg_;
    P potential_;
    ContactFelt felt_;
    VerticalTensionPotential tension_;
    std::array<pme::nl::Dashpot<double>, pme::nl::kMaxDashpots> dash_{};
    pme::bridge::BridgePoint<double>* bridge_{nullptr};
    std::vector<String> strings_;
    std::vector<double> force_;
    pme::HammerParams hammer_{};
    pme::UnaCordaParams una_{};
    pme::exciters::SoftPedal pedal_{pme::exciters::SoftPedal::UnaCorda};
    std::size_t hammerIdx_{0};
    std::array<double, 3> strength_{1.0, 1.0, 1.0}, lag_{};
    double sE_{1.0}, dt_{0.0}, shift_{0.0}, floor_{1e-14};
    bool tensionOn_{true}, fast_{false}, hZero_{true}, dotValid_{false};
    std::array<double, 3> dotCache_{};
    double truncRel_{0.0}, psi0_{0.0};
    std::array<std::size_t, 3> nAct_{};   // active (not truncated) vertical modes per string
    bool inFlight_{false};
    double prevMax_{-1.0e300};
    std::uint64_t prevSample_{0};
    double contactStart_{0.0};
    bool contactOpen_{false};
    double peak_{0.0};
    std::uint64_t sample_{0};
    double releasedEnergy_{0.0}, resetEnergyRemoved_{0.0};
    pme::exciters::HammerDiagnostics diag_{};
};
}
