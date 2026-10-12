// SPDX-License-Identifier: MIT
// Piano hammer and felt (A5-R1 to A5-R7): velocity map, multi-string felt contact as one SAV
// potential, Hunt-Crossley dashpots or Stulov hereditary felt, release and repetition, una corda
// and the optional flexible shank, plus `StruckNote`, the reference composition of one struck note
// in the SPEC-EXEC per-sample order.
//
// SPEC sections implemented: SPEC-HAM (all of "Hammer and action"), SPEC-NL "Coordinates",
// "SAV formulation", "Explicit low-rank solve", "Contracts"; SPEC-EXEC "Per-sample loop of one
// note group" steps 1, 2, 5 and 6 (and the bridge-force/bridge-term separation that lets the
// implicit scheme of steps 3' to 5' be added); SPEC-BRG "Discrete coupling order"; SPEC-STR
// "Forces and observations" (raised-cosine windows); SPEC-CONV (float64 reductions, float32 run
// time states, sign convention s_E).
//
// Internal to the engine (interfaces-v2 declares no hammer API beyond the parameter structs in
// pme/exciters.hpp). `ShankParams` mirrors ICR-0002 (proposed); when that is applied this header
// switches to the public type. Every per-sample member is noexcept and allocation free; every
// `configure` allocates and runs on a non-audio thread.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/modal_segment.hpp"
#include "pme/exciters.hpp"
#include "pme/nonlinear/sav.hpp"
#include "pme/status.hpp"
#include "pme/string.hpp"

namespace pme::exciters {

/// Largest number of strings one hammer strikes (a trichord); equal to A3's `kMaxDashpots`.
inline constexpr std::size_t kMaxStruckStrings = nl::kMaxDashpots;

/// Flexible shank (SPEC-HAM "Flexible shank", ICR-0002): the head of mass `HammerParams::mass`
/// is joined to a shank mass by a linear spring and dashpot. Absent (`std::nullopt`) = rigid hammer.
struct ShankParams {
    double mass{};       ///< Shank mass m_S (kg), > 0.
    double frequency{};  ///< Internal-mode frequency f_S (Hz), > 0.
    double sigma{};      ///< Internal-mode decay sigma_S (1/s), >= 0.
};

/// How the soft pedal acts (SPEC-HAM "Una corda (grand)", "Soft pedal (upright)").
enum class SoftPedal : std::uint8_t {
    UnaCorda,  ///< Grand: shift s changes strike weights and the felt zone, velocity unchanged.
    Upright,   ///< Upright: v_H scaled by (1 - s rho_up), strike unchanged.
};

// ---- A5-R1: velocity map ---------------------------------------------------------------------

/// v_H = v_min + (v_max - v_min) nu^gamma_v (SPEC-HAM "Velocity map"). `InvalidArgument` (vH
/// unchanged) for nu <= 0 or not finite; nu > 1 is clamped to 1. Thread: any. Allocates: no.
[[nodiscard]] Status hammerVelocity(double nu, const HammerParams& params, double& vH) noexcept;

// ---- A5-R5: una corda ------------------------------------------------------------------------

/// Smoothstep S(s) = 3 t^2 - 2 t^3 with t = clamp((s - s0) / (s1 - s0), 0, 1) (SPEC-HAM "Una corda
/// (grand)"; the cubic is this project's definition of S). Requires s1 > s0. Thread: any.
[[nodiscard]] double smoothstep(double s, double s0, double s1) noexcept;

/// Per-string strike weights and felt values of one strike (SPEC-HAM "Una corda (grand)").
struct StrikeMap {
    std::size_t strings{0};              ///< Number of struck strings (1 to 3).
    std::array<double, 3> weight{};      ///< w_s in [0, 1].
    std::array<double, 3> stiffness{};   ///< K_s(s) in N/m^p.
    std::array<double, 3> exponent{};    ///< p_s(s).
};

/// Strike map for `nStrings` (1 to 3) at shift `shift` (clamped to [0, 1]; not finite is an
/// error). Grand: w_leaving = 1 - S(s) and the others 1 (`UnaCordaParams::leavingString`, -1 or a
/// single string: all 1); K_s(s) = K [1 - s (1 - kappa_uc)], p_s(s) = p + s Delta p_uc for every
/// string. Upright: weights 1, K and p unchanged. `InvalidArgument` (out unchanged) for bad
/// parameters, nStrings outside 1..3 or leavingString >= nStrings. Thread: any. Allocates: no.
[[nodiscard]] Status strikeMap(const HammerParams& hammer, const UnaCordaParams& una,
                               SoftPedal pedal, std::size_t nStrings, double shift,
                               StrikeMap& out) noexcept;

/// v_H of a launch: the velocity map times (1 - s rho_up) for the upright pedal (SPEC-HAM "Soft
/// pedal (upright)"). Errors as `hammerVelocity`. Thread: any. Allocates: no.
[[nodiscard]] Status launchVelocity(double nu, double shift, const HammerParams& hammer,
                                    const UnaCordaParams& una, SoftPedal pedal,
                                    double& vH) noexcept;

/// Check a hammer description (positive mass, K, p, width; 0 < strikeRatio < 1; Stulov tau > 0;
/// velocity map and d_check sane). Thread: any. Allocates: no.
[[nodiscard]] Status validate(const HammerParams& hammer) noexcept;
/// Check una corda parameters (0 <= s0 < s1 <= 1, kappa_uc > 0, 0 <= rho_up < 1).
[[nodiscard]] Status validate(const UnaCordaParams& una) noexcept;
/// Check a shank (mass > 0, frequency > 0, sigma >= 0, all finite).
[[nodiscard]] Status validate(const ShankParams& shank) noexcept;

// ---- A5-R2: the potential --------------------------------------------------------------------

/// One struck string's slice of the stacked coordinates: where its vertical modes start and its
/// window shape vectors phi-bar (float32 storage, SPEC-STR "Contract").
struct StringSlice {
    std::size_t offset{0};      ///< Index of mode 1 in the stacked coordinates.
    std::vector<float> phiBar;  ///< phi-bar_{s,n}, one per mode.
};

/// V_H = sum_s w_s K_s / (p_s + 1) [delta_s]_+^(p_s+1) with delta_s = x_H - s_E sum_n phi-bar_{s,n}
/// q_{s,n} (SPEC-HAM "Contact with each struck string"). Arithmetic is float64. With the shank on,
/// x_H = z[h] + lever z[h + 1] (SPEC-HAM "Flexible shank"): the gradient has two hammer entries.
template <class S>
class HammerPotential final : public nl::Potential<S> {
public:
    /// Set the geometry. `hammerIndex` is the stacked index of x_H (or of the rigid-body
    /// coordinate X with a shank); `lever` is the coefficient of the next coordinate in x_H = X +
    /// lever Y, or 0 for a rigid hammer; `approachSign` is s_E (+1 or -1). `InvalidArgument` for 0
    /// or more than 3 strings, empty or non-finite shape vectors, or a bad sign. The strike map
    /// starts with all weights 0 and the potential inactive. Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(std::vector<StringSlice> strings, std::size_t hammerIndex,
                                   double lever, double approachSign);

    /// Strike weights, K_s(s) and p_s(s) of the next evaluations. Thread: audio. Allocates: no.
    void setStrikeMap(const StrikeMap& map) noexcept { map_ = map; }
    /// An inactive potential is exactly 0 (the hammer has left the coordinate set, A5-R4).
    void setActive(bool active) noexcept { active_ = active; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] double value(std::span<const S> z) const noexcept override;
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override;

    /// x_H of the stacked state (X + lever Y with a shank).
    [[nodiscard]] double headPosition(std::span<const S> z) const noexcept;
    /// s_E sum_n phi-bar_{s,n} q_{s,n}: the window-averaged string position along the hammer axis.
    [[nodiscard]] double stringPosition(std::span<const S> z, std::size_t s) const noexcept;
    /// delta_s = x_H - stringPosition (signed).
    [[nodiscard]] double penetration(std::span<const S> z, std::size_t s) const noexcept;
    /// Largest delta_s over the strings with weight > 0 (a very negative number when none).
    [[nodiscard]] double maxPenetration(std::span<const S> z) const noexcept;
    /// dV/dx_H = sum_s w_s K_s [delta_s]_+^p_s (0 when inactive).
    [[nodiscard]] double hammerGradient(std::span<const S> z) const noexcept;

    [[nodiscard]] std::size_t stringCount() const noexcept { return strings_.size(); }
    [[nodiscard]] const StringSlice& slice(std::size_t s) const noexcept { return strings_[s]; }
    [[nodiscard]] const StrikeMap& map() const noexcept { return map_; }
    [[nodiscard]] std::size_t hammerIndex() const noexcept { return hammerIndex_; }
    [[nodiscard]] double lever() const noexcept { return lever_; }
    [[nodiscard]] double approachSign() const noexcept { return sE_; }

private:
    std::vector<StringSlice> strings_;
    std::size_t hammerIndex_{0};
    double lever_{0.0};
    double sE_{1.0};
    bool active_{false};
    StrikeMap map_{};
};

/// Test-only variant (SPEC-HAM "Oracle", A5-T1, ORC-08): a free mass at stacked index `index`
/// against a fixed target, V = K / (p + 1) [x - target]_+^(p+1); no string modes.
template <class S>
class RigidFeltPotential final : public nl::Potential<S> {
public:
    RigidFeltPotential(double k, double p, std::size_t index, double target = 0.0) noexcept
        : k_(k), p_(p), index_(index), target_(target) {}
    [[nodiscard]] double value(std::span<const S> z) const noexcept override;
    void gradient(std::span<const S> z, std::span<double> grad) const noexcept override;

private:
    double k_;
    double p_;
    std::size_t index_;
    double target_;
};

// ---- A5-R3: felt dissipation -----------------------------------------------------------------

/// Number of dashpot slots the hammer's felt occupies on its segment: one per struck string for
/// Hunt-Crossley, none for Stulov (SPEC-NL allows at most `nl::kMaxDashpots` = 3 per segment).
/// A trichord uses the whole budget (A5-R3). Thread: any. Allocates: no.
[[nodiscard]] std::size_t hammerDashpotCount(const HammerParams& hammer,
                                             std::size_t nStrings) noexcept;
/// Dashpot slots left for other terms (the rank-one damper needs one): kMaxDashpots minus the
/// hammer's count.
[[nodiscard]] std::size_t hammerDashpotBudgetLeft(const HammerParams& hammer,
                                                  std::size_t nStrings) noexcept;

/// The felt's dissipation: Hunt-Crossley dashpots R_s = w_s K_s mu_HC [delta_s]_+^p_s along c_s =
/// d delta_s / d z, or the Stulov hereditary force (SPEC-HAM "Felt dissipation", "Alternative:
/// Stulov hereditary felt"). Call `update` once per sample before `savStep`.
template <class S>
class HammerFelt {
public:
    /// Build the directions c_s over `nz` stacked coordinates from the potential's geometry and
    /// derive a = exp(-dt / tau). `InvalidArgument` for bad parameters. Thread: non-audio.
    /// Allocates: yes.
    [[nodiscard]] Status configure(const HammerParams& hammer, const HammerPotential<S>& potential,
                                   std::size_t nz, double fs);

    /// Zero the hereditary state (a new strike).
    void reset() noexcept;

    /// Set R_s (and `alongGradient`) from the state z^n; for Stulov advance h_s and add the
    /// hereditary force -w_s K_s eps h_s c_s (N per coordinate) into `force` for strings in
    /// contact. Thread: audio. Allocates: no.
    void update(const HammerPotential<S>& potential, std::span<const S> z,
                std::span<S> force) noexcept;

    /// The dashpots for `savStep` (empty for Stulov or when never configured).
    [[nodiscard]] std::span<const nl::Dashpot<S>> dashpots() const noexcept {
        return {dashpots_.data(), count_};
    }
    /// Slots occupied (`hammerDashpotCount`).
    [[nodiscard]] std::size_t dashpotCount() const noexcept { return count_; }
    /// Work done by the hereditary force in the last step, float64 J: sum_s F_s c_s^T (u^{n+1} +
    /// u^n) / 2, so that E^{n+1} - E^n + dissipation = work (A5-R3). 0 for Hunt-Crossley.
    [[nodiscard]] double hereditaryWork(const nl::SavSegment<S>& seg) const noexcept;
    /// Hereditary state h_s of string s (tests).
    [[nodiscard]] double hereditary(std::size_t s) const noexcept { return h_[s]; }

private:
    FeltLaw law_{FeltLaw::HuntCrossley};
    double muHC_{0.0};
    double eps_{0.0};
    double decay_{0.0};
    std::size_t count_{0};
    std::vector<std::vector<S>> c_;
    std::array<nl::Dashpot<S>, kMaxStruckStrings> dashpots_{};
    std::array<double, kMaxStruckStrings> h_{};
    std::array<double, kMaxStruckStrings> force_{};
};

// ---- A5-R7: diagnostics ----------------------------------------------------------------------

/// Hammer diagnostics (A5-R7): plain values that the runtime reads and clears on the audio
/// thread, like `nl::SavCounters`.
struct HammerDiagnostics {
    std::uint32_t strikes{0};         ///< Launches (each `launch` call counts one strike).
    std::uint32_t contacts{0};        ///< Completed contact intervals (a double strike has two).
    double lastContactDuration{0.0};  ///< s: first to last sample with any delta > 0, sub-sample.
    double lastPeakForce{0.0};        ///< N: peak |g_H| (psi^{n+1/2} + psi^{n-1/2}) / 2 of it.
    double totalContactTime{0.0};     ///< s: sum of the durations since the last clear.
};

// ---- StruckNote: reference composition -------------------------------------------------------

/// Description of one struck note.
struct StruckNoteDesc {
    std::vector<const StringModes*> strings;  ///< The struck strings, 1 to 3, in string order.
    BridgeEnd end{BridgeEnd::AtL};            ///< Bridge end of the struck segments.
    HammerParams hammer{};                    ///< Felt, window, velocity map, d_check.
    UnaCordaParams unaCorda{};                ///< Una corda / soft pedal parameters.
    SoftPedal pedal{SoftPedal::UnaCorda};     ///< Grand or upright.
    double approachSign{1.0};                 ///< s_E (+1 or -1), set per instrument.
    std::optional<ShankParams> shank{};       ///< Flexible shank; absent = rigid hammer.
    nl::SavParams sav{};                      ///< SAV tunables (drift rules).
};

/// One struck note on a bridge point, run in the SPEC-EXEC per-sample order:
///   1. launch events (`launch`, `setShift`, called by the owner before `step`);
///   2. F_b,s of every string from q^n and w^n (`bridge::bridgeForce`), added to the point;
///   3./4. `BridgePoint::advance` and delta_tt w^n;
///   5. explicit modal forces -rho A g_n delta_tt w^n (N per coordinate) and the Stulov force;
///   6. `nl::savStep` over the vertical modes of every struck string and the hammer;
///   7. the horizontal polarisations (bridge term only) and any passive segments.
/// The bridge force and the bridge term stay separate inputs, so that A9's implicit scheme (steps
/// 3' to 5': the low-rank solve applied to a second right-hand side) can be added later. The
/// bridge point is owned by the caller (C1) and may be null (strings fixed at the bridge end).
/// SPEC-NL: all struck strings share one SAV (one psi).
template <class S>
class StruckNote {
public:
    /// Build the note at sample rate `fs`. `bridge` (not owned, may be null) is the note group's
    /// bridge point. `InvalidArgument` for bad parameters, a null or empty string, a string
    /// compiled for another sample rate, or an oscillator failing the SPEC-OSC bandwidth rule
    /// (shank). Thread: non-audio. Allocates: yes.
    [[nodiscard]] Status configure(const StruckNoteDesc& desc, double fs,
                                   bridge::BridgePoint<S>* bridge);
    /// Attach a passive segment (a rear or duplex string) to the bridge point; it takes part in
    /// the coupling order. `InvalidArgument` without a bridge point or on bad modes. Thread:
    /// non-audio. Allocates: yes.
    [[nodiscard]] Status addPassiveSegment(const StringModes& modes, BridgeEnd end);

    /// Launch the hammer (SPEC-HAM "Velocity map", "Repetition"): v_H from `nu` and `shift`; a
    /// hammer in flight within d_check of the string continues from its position with the new
    /// velocity, otherwise it starts at the gap v_H dt from the window-averaged string position
    /// (the highest struck string), so it touches the string one sample later. Counts a strike.
    /// `InvalidArgument` for a bad nu or shift. Thread: audio. Allocates: no.
    [[nodiscard]] Status launch(double nu, double shift) noexcept;
    /// Change the una corda shift (continuous; call per sample while it ramps). Recomputes the
    /// strike weights and felt values. Thread: audio. Allocates: no.
    [[nodiscard]] Status setShift(double shift) noexcept;

    /// One sample (steps 2 to 7). Thread: audio. Allocates: no.
    void step() noexcept;

    /// Reset all states to rest (strings, hammer, bridge point not touched), keeping the
    /// configuration and the diagnostics.
    void reset() noexcept;

    // Access for tests and the runtime.
    [[nodiscard]] nl::SavSegment<S>& segment() noexcept { return seg_; }
    [[nodiscard]] const nl::SavSegment<S>& segment() const noexcept { return seg_; }
    [[nodiscard]] const HammerPotential<S>& potential() const noexcept { return potential_; }
    [[nodiscard]] const HammerFelt<S>& felt() const noexcept { return felt_; }
    [[nodiscard]] bool inFlight() const noexcept { return inFlight_; }
    [[nodiscard]] std::size_t stringCount() const noexcept { return strings_.size(); }
    /// Index of the first vertical mode of string s in the stacked state, and its mode count.
    [[nodiscard]] std::size_t modeOffset(std::size_t s) const noexcept { return strings_[s].off; }
    [[nodiscard]] std::size_t modeCount(std::size_t s) const noexcept { return strings_[s].n; }
    /// Head position x_H and head velocity (m/s, half-step) of the current state.
    [[nodiscard]] double headPosition() const noexcept;
    [[nodiscard]] double headVelocity() const noexcept;
    /// Horizontal mode states of string s (zero unless a bridge point couples them).
    [[nodiscard]] const S* qH(std::size_t s) const noexcept { return strings_[s].qH.data(); }
    [[nodiscard]] const HammerDiagnostics& diagnostics() const noexcept { return diag_; }
    /// Clear the diagnostics (the audio-thread reader does this after reading).
    void clearDiagnostics() noexcept { diag_ = HammerDiagnostics{}; }
    /// Una corda shift in effect (clamped to [0, 1]).
    [[nodiscard]] double shift() const noexcept { return shift_; }
    /// Energy carried away by released hammers so far (J, float64): their kinetic energy and the
    /// shank's internal energy at the release (SPEC-HAM "Release and check").
    [[nodiscard]] double releasedEnergy() const noexcept { return releasedEnergy_; }
    /// Conserved energy of the SAV segment (strings' vertical modes, hammer, psi), float64.
    [[nodiscard]] double energy() const noexcept { return nl::savEnergy(seg_); }

private:
    struct String {
        std::size_t off{0};
        std::size_t n{0};
        double rhoA{0.0};
        double tOverL{0.0};
        std::vector<S> aV, aH, forceCoef;  // forceCoef = rhoA g_n (N per unit acceleration)
        std::vector<S> g;                  // g_n for the horizontal modal force (per unit mass)
        std::vector<S> beta, kappa, gamma, qH, uH, fH;
    };

    void closeContact(double endSample) noexcept;
    void release() noexcept;
    void trackContact(double maxDelta) noexcept;

    nl::SavSegment<S> seg_;
    HammerPotential<S> potential_;
    HammerFelt<S> felt_;
    bridge::BridgePoint<S>* bridge_{nullptr};
    std::vector<String> strings_;
    std::vector<bridge::ModalSegment<S>> passive_;
    std::vector<S> force_;
    HammerParams hammer_{};
    UnaCordaParams una_{};
    SoftPedal pedal_{SoftPedal::UnaCorda};
    bool hasShank_{false};
    double shankLever_{0.0};
    std::size_t hammerIdx_{0};
    double sE_{1.0};
    double dt_{0.0};
    double shift_{0.0};
    bool inFlight_{false};
    // Contact bookkeeping (A5-R7).
    double prevMax_{-1.0e300};
    std::uint64_t prevSample_{0};
    double contactStart_{0.0};
    bool contactOpen_{false};
    double peak_{0.0};
    std::uint64_t sample_{0};
    double releasedEnergy_{0.0};
    HammerDiagnostics diag_{};
};

extern template class HammerPotential<float>;
extern template class HammerPotential<double>;
extern template class RigidFeltPotential<float>;
extern template class RigidFeltPotential<double>;
extern template class HammerFelt<float>;
extern template class HammerFelt<double>;
extern template class StruckNote<float>;
extern template class StruckNote<double>;

}  // namespace pme::exciters
