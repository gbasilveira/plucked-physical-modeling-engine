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

namespace piano_mvp_v4 {
// V = sum_s c_s S_s^2 / 4, S_s = sum_n k2_n q_n^2 over string s's vertical modes (SPEC-GEO).
class VerticalTensionPotential final : public pme::nl::Potential<double> {
public:
    struct Slice { std::size_t offset{}; std::vector<double> k2; double c{}; };
    void configure(std::vector<Slice> s) { slices_ = std::move(s); }
    bool empty() const noexcept { return slices_.empty(); }
    // S_s (m) of string s.
    double sum(std::span<const double> z, std::size_t s) const noexcept {
        const auto& sl = slices_[s]; double acc = 0;
        for (std::size_t n = 0; n < sl.k2.size(); ++n) acc += sl.k2[n] * z[sl.offset + n] * z[sl.offset + n];
        return acc;
    }
    // Delta T_s = c_s S_s (N).
    double deltaT(std::span<const double> z, std::size_t s) const noexcept { return slices_[s].c * sum(z, s); }
    double value(std::span<const double> z) const noexcept override {
        double v = 0;
        for (std::size_t s = 0; s < slices_.size(); ++s) { const double S = sum(z, s); v += 0.25 * slices_[s].c * S * S; }
        return v;
    }
    void gradient(std::span<const double> z, std::span<double> grad) const noexcept override {
        for (auto& g : grad) g = 0;
        for (std::size_t s = 0; s < slices_.size(); ++s) {
            const auto& sl = slices_[s];
            const double dT = sl.c * sum(z, s);
            for (std::size_t n = 0; n < sl.k2.size(); ++n) grad[sl.offset + n] = dT * sl.k2[n] * z[sl.offset + n];
        }
    }
    std::size_t strings() const noexcept { return slices_.size(); }
private:
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
        std::vector<double> aV, aH, forceCoef, g, beta, kappa, gamma, qH, uH, fH;
    };
    void closeContact(double endSample) noexcept;
    void release() noexcept;
    void trackContact(double maxDelta) noexcept;
    using P = pme::exciters::HammerPotential<double>;
    pme::nl::SavSegment<double> seg_;
    P potential_;
    pme::exciters::HammerFelt<double> felt_;
    VerticalTensionPotential tension_;
    std::array<pme::nl::Dashpot<double>, pme::nl::kMaxDashpots> dash_{};
    pme::bridge::BridgePoint<double>* bridge_{nullptr};
    std::vector<String> strings_;
    std::vector<double> force_;
    pme::HammerParams hammer_{};
    pme::UnaCordaParams una_{};
    pme::exciters::SoftPedal pedal_{pme::exciters::SoftPedal::UnaCorda};
    std::size_t hammerIdx_{0};
    double sE_{1.0}, dt_{0.0}, shift_{0.0}, floor_{1e-14};
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
