// SPDX-License-Identifier: MIT
// Covers: ORC-08, ORC-09
//
// ORC-08 Impact time, hammer-potential case left to A5 by A3 (SPEC-ORC; SPEC-HAM "Oracle").
// Setup: mass 8 g at 3 m/s on a rigid felt contact K [delta]_+^p, p = 2.5, K = 4.5e9 N/m^p (the
// hammer potential with no string modes and a fixed target, delta = x_H), launched at the gap
// v_H dt, 48 kHz in float32 and float64 and 96 kHz in float64, through savStep.
// Expected t_c = 2 (delta_m / v) sqrt(pi) Gamma(1 + 1/(p+1)) / Gamma(1/2 + 1/(p+1)) computed in
// hammer_oracle_common.hpp; rebound -v. Tolerance: |t_c_hat - t_c| <= max(1 % t_c, 1 sample);
// rebound within 0.1 % of -v (SPEC-ORC). Method: t_c is the span between the interpolated entry
// and exit crossings of delta = 0; the rebound is the half-step speed u/dt after separation.
//
// ORC-09 Hammer-string energy (SPEC-ORC; SPEC-NL "Discrete scheme"; docs/dev/bridge.md "Energy of
// the partitioned scheme"). Lossless plain steel strings (b1 = b2 = b3 = 0, d = 1 mm, L = 0.65 m,
// f1 = 262 Hz), pinned, every mode below 16 kHz; hammer 8 g, K = 4.5e9, p = 2.5, mu_HC = 0,
// raised-cosine window b_H = 10 mm at beta_c = 1/8, v_H = 3 m/s; S = double, 48 kHz, drift
// relaxation off, 0.1 s (4800 samples) through the strike and rebound. The total energy is
// computed by the rig from the state (the SPEC-OSC discrete energy of every mode, the hammer's
// m_H H, and psi^2/2 - eps0) and compared with E_0 = m_H v_H^2 / 2, the kinetic energy at launch:
//   max_n |E^n - E_0| / E_0 <= 1e-10 (SPEC-ORC).
// Cases: (1) one string on a rigid bridge, (2) a trichord detuned by 0, +0.5, -0.5 cent with
// w = 1 each on a rigid bridge (r = 1 + 3 solve), both bounded; (3) the trichord on a lossless
// mass-spring bridge (m = 0.3 kg, 300 Hz, r_b = 0) with the bridge-point energy of
// docs/dev/bridge.md added, and (4) the same with theta_b = 0.1 rad: reported, not bounded.
//
// Every case runs on the SPEC scratch scheme (hammer_rig_scratch.hpp) and, when
// PME_ORACLE_HAMMER_A5 is defined, on the A5 StruckNote (hammer_rig_a5.hpp).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"

namespace {

namespace ham = pme::oracle::ham;
namespace str = pme::oracle::str;

constexpr double kSpeed = 3.0;             // m/s
constexpr double kWidth = 0.01;            // m, b_H
constexpr double kStrike = 1.0 / 8.0;      // beta_c
constexpr double kEnergyTol = 1.0e-10;     // ORC-09
constexpr double kReboundTol = 1.0e-3;     // ORC-08 rebound
constexpr double kTimeTolFraction = 0.01;  // ORC-08 contact time, or one sample
constexpr double kFMax = 16000.0;          // ORC-09: all modes below 16 kHz
constexpr double kFs = 48000.0;

// ---- ORC-08 -----------------------------------------------------------------------------------

template <class RigT>
void orc08Case(double fs) {
    ham::NoteSetup cfg;
    cfg.hammer.mass = ham::kHammerMass;
    cfg.hammer.K = ham::kFeltK;
    cfg.hammer.p = ham::kFeltP;
    cfg.hammer.muHC = 0.0;
    cfg.hammer.width = 0.0;
    cfg.hammer.strikeRatio = 0.0;
    cfg.hammerSpeed = kSpeed;
    cfg.fs = fs;
    RigT rig(cfg);
    REQUIRE(rig.ok());
    const double dt = 1.0 / fs;
    std::vector<double> delta;
    const auto steps = static_cast<std::size_t>(0.03 * fs);  // 30 ms: contact (< 1 ms) and flight
    delta.reserve(steps);
    for (std::size_t n = 0; n < steps; ++n) {
        rig.step();
        delta.push_back(rig.penetration());
    }
    const double tcExact =
        ham::analyticContactTime(ham::kHammerMass, kSpeed, ham::kFeltK, ham::kFeltP);
    const ham::ContactInterval contact = ham::firstContact(delta, dt);
    REQUIRE(contact.found);
    const double rebound = rig.hammerSpeed();
    const double tol = std::max(kTimeTolFraction * tcExact, dt);
    const double err = std::fabs(contact.duration - tcExact);
    const double reboundErr = std::fabs(rebound / (-kSpeed) - 1.0);
    str::report("ORC-08 hammer potential fs=" + std::to_string(fs) + ": t_c expected " +
                str::sci(tcExact) + " measured " + str::sci(contact.duration) + " error " +
                str::sci(err) + " s (" + std::to_string(100.0 * err / tcExact) + " %, " +
                std::to_string(err / dt) + " samples; tolerance " + str::sci(tol) + "), rebound " +
                std::to_string(rebound) + " m/s error " + str::sci(reboundErr) +
                " (tolerance 1e-3), contact intervals " + std::to_string(contact.count) +
                ", psi resets " + std::to_string(rig.resets()));
    CHECK(err <= tol);
    CHECK(reboundErr <= kReboundTol);
    CHECK(contact.count == 1);
    CHECK(rig.resets() == 1);
}

// ---- ORC-09 -----------------------------------------------------------------------------------

struct EnergyRun {
    double maxRelRaw{0.0};          ///< max |E^n - E0| / E0 over the whole run
    double maxRelBeforeReset{0.0};  ///< same over the samples before the first psi reset
    double maxRelAccounted{0.0};    ///< max |E^n + R^n - E0| / E0, R^n = energy removed by resets
    double resetLoss{0.0};          ///< total energy removed by the reset rule, relative to E0
    double resetGain{0.0};          ///< largest energy ADDED by one reset (should be <= 0), / E0
    int resets{0};
    double contactTime{0.0};
    double peakForce{0.0};
    bool finite{true};
    std::size_t modes{0};
};

/// One lossless string per entry of `cents`, tuned f1 = 262 Hz * 2^(cents / 1200).
std::vector<pme::StringModes> losslessStrings(const std::vector<double>& cents) {
    std::vector<pme::StringModes> out;
    for (const double c : cents) {
        ham::OracleString s;
        REQUIRE(
            ham::makeString(262.0 * std::pow(2.0, c / 1200.0), 1.0e-3, 0.0, 0.0, kFs, kFMax, s));
        // "all modes below 16 kHz": the largest kept mode frequency
        REQUIRE(s.modes.omega.back() / (2.0 * ham::kPi) <= kFMax);
        out.push_back(s.modes);
    }
    return out;
}

ham::NoteSetup orc09Setup(const std::vector<double>& cents) {
    ham::NoteSetup cfg;
    cfg.strings = losslessStrings(cents);
    cfg.hammer.mass = ham::kHammerMass;
    cfg.hammer.K = ham::kFeltK;
    cfg.hammer.p = ham::kFeltP;
    cfg.hammer.muHC = 0.0;
    cfg.hammer.width = kWidth;
    cfg.hammer.strikeRatio = kStrike;
    cfg.una.leavingString = -1;
    cfg.shift = 0.0;
    cfg.hammerSpeed = kSpeed;
    cfg.fs = kFs;
    cfg.driftRelaxation = false;
    return cfg;
}

/// Runs 0.1 s and tracks the energy. SPEC-NL "Drift control" 1 (reset at separation, MUST) sets
/// |psi| to sqrt(2 eps0) once the contact has ended; that removes exactly the drift energy
/// psi^2/2 - eps0 held in psi, by design. The run therefore reports (a) the raw deviation, (b) the
/// deviation before the first reset, and (c) the deviation after adding back what the reset rule
/// removed, R = sum over reset events of (psi_before^2 / 2 - eps0): the energy is conserved by
/// the scheme to round-off exactly when (b) and (c) are at round-off.
template <class RigT>
EnergyRun runEnergy(const ham::NoteSetup& cfg) {
    RigT rig(cfg);
    REQUIRE(rig.ok());
    EnergyRun r;
    for (const auto& m : cfg.strings) {
        r.modes += m.omega.size();
    }
    const double e0 = 0.5 * cfg.hammer.mass * cfg.hammerSpeed * cfg.hammerSpeed;
    const auto steps = static_cast<std::size_t>(0.1 * cfg.fs);  // 0.1 s through strike and rebound
    std::vector<double> delta;
    delta.reserve(steps);
    std::vector<double> force;
    force.reserve(steps);
    double removed = 0.0;  // R^n in joules
    double ePrev = e0;
    for (std::size_t n = 0; n < steps; ++n) {
        const double psiBefore = rig.psi();
        const int resetsBefore = rig.resets();
        rig.step();
        const double e = rig.energy();
        r.finite = r.finite && std::isfinite(e);
        if (rig.resets() > resetsBefore) {
            const double stored = 0.5 * psiBefore * psiBefore - cfg.eps0;
            removed += stored;
            ++r.resets;
            r.resetGain = std::max(r.resetGain, (e - ePrev) / e0);
        }
        ePrev = e;
        const double rel = std::fabs(e - e0) / e0;
        r.maxRelRaw = std::max(r.maxRelRaw, rel);
        if (r.resets == 0) {
            r.maxRelBeforeReset = std::max(r.maxRelBeforeReset, rel);
        }
        r.maxRelAccounted = std::max(r.maxRelAccounted, std::fabs(e + removed - e0) / e0);
        delta.push_back(rig.penetration());
        force.push_back(rig.contactForce());
    }
    r.resetLoss = removed / e0;
    const ham::ContactInterval contact = ham::firstContact(delta, 1.0 / cfg.fs);
    r.contactTime = contact.duration;
    r.peakForce = ham::parabolicPeak(force);
    return r;
}

void reportEnergy(const std::string& label, const EnergyRun& r) {
    str::report(
        "ORC-09 " + label + ": " + std::to_string(r.modes) +
        " modes, max |E - E0| / E0 before the first psi reset = " + str::sci(r.maxRelBeforeReset) +
        ", with the reset energy added back = " + str::sci(r.maxRelAccounted) +
        " (tolerance 1e-10); raw over the whole run = " + str::sci(r.maxRelRaw) +
        ", energy removed by " + std::to_string(r.resets) + " reset(s) = " + str::sci(r.resetLoss) +
        " E0; first contact " + str::sci(r.contactTime) + " s, peak force " +
        str::sci(r.peakForce) + " N");
}

template <class RigT>
void orc09Single() {
    const EnergyRun r = runEnergy<RigT>(orc09Setup({0.0}));
    reportEnergy("one string, rigid bridge", r);
    CHECK(r.finite);
    CHECK(r.maxRelBeforeReset <= kEnergyTol);
    CHECK(r.maxRelAccounted <= kEnergyTol);
    CHECK(r.resetGain <= kEnergyTol);  // the reset rule only removes energy (SPEC-NL)
    CHECK(r.contactTime > 0.0);        // the strike happened
}

template <class RigT>
void orc09Trichord() {
    const EnergyRun r = runEnergy<RigT>(orc09Setup({0.0, 0.5, -0.5}));
    reportEnergy("trichord (0, +0.5, -0.5 cent), rigid bridge", r);
    CHECK(r.finite);
    CHECK(r.maxRelBeforeReset <= kEnergyTol);
    CHECK(r.maxRelAccounted <= kEnergyTol);
    CHECK(r.resetGain <= kEnergyTol);
    CHECK(r.contactTime > 0.0);
}

template <class RigT>
void orc09TrichordBridge(double thetaB, double bridgeMass) {
    ham::NoteSetup cfg = orc09Setup({0.0, 0.5, -0.5});
    const double omega = 2.0 * ham::kPi * 300.0;
    cfg.bridge = ham::BridgeModel::massSpringDamper(bridgeMass, bridgeMass * omega * omega, 0.0);
    cfg.thetaB = thetaB;
    const EnergyRun r = runEnergy<RigT>(cfg);
    reportEnergy("trichord on a lossless mass-spring bridge (m = " + std::to_string(bridgeMass) +
                     " kg, 300 Hz), theta_b = " + std::to_string(thetaB) +
                     " (reported, not bounded)",
                 r);
    CHECK(r.finite);
    CHECK(r.contactTime > 0.0);
}

constexpr double kBridgeMassOrc09 = 0.3;  // kg, the brief's bridge

}  // namespace

// ---- SPEC scratch scheme ----------------------------------------------------------------------

TEST_CASE("ORC-08 impact time with the hammer potential 48 kHz float32 (SPEC scratch scheme)") {
    orc08Case<ham::ScratchRig<float>>(48000.0);
}
TEST_CASE("ORC-08 impact time with the hammer potential 48 kHz float64 (SPEC scratch scheme)") {
    orc08Case<ham::ScratchRig<double>>(48000.0);
}
TEST_CASE("ORC-08 impact time with the hammer potential 96 kHz float64 (SPEC scratch scheme)") {
    orc08Case<ham::ScratchRig<double>>(96000.0);
}
TEST_CASE("ORC-09 sanity analytic contact time of the typical felt") {
    const double tc = ham::analyticContactTime(ham::kHammerMass, kSpeed, ham::kFeltK, ham::kFeltP);
    CHECK(tc > 5.0e-4);
    CHECK(tc < 1.5e-3);
    // contact time scales as v^((1-p)/(1+p)) (SPEC-HAM "Oracle")
    const double tc2 =
        ham::analyticContactTime(ham::kHammerMass, 2.0 * kSpeed, ham::kFeltK, ham::kFeltP);
    CHECK(tc2 / tc ==
          doctest::Approx(std::pow(2.0, (1.0 - ham::kFeltP) / (1.0 + ham::kFeltP))).epsilon(1e-12));
}
TEST_CASE("ORC-09 hammer-string energy one string rigid bridge (SPEC scratch scheme)") {
    orc09Single<ham::ScratchRig<double>>();
}
TEST_CASE("ORC-09 hammer-string energy trichord rigid bridge (SPEC scratch scheme)") {
    orc09Trichord<ham::ScratchRig<double>>();
}
TEST_CASE(
    "ORC-09 hammer-string energy trichord mass-spring bridge reported (SPEC scratch scheme)") {
    orc09TrichordBridge<ham::ScratchRig<double>>(0.0, kBridgeMassOrc09);
    orc09TrichordBridge<ham::ScratchRig<double>>(0.0, 3.0);
    orc09TrichordBridge<ham::ScratchRig<double>>(0.0, 30.0);
}
TEST_CASE("ORC-09 hammer-string energy trichord rotated bridge reported (SPEC scratch scheme)") {
    orc09TrichordBridge<ham::ScratchRig<double>>(0.1, kBridgeMassOrc09);
}

#ifdef PME_ORACLE_HAMMER_A5
// ---- A5 StruckNote ----------------------------------------------------------------------------

TEST_CASE("ORC-08 impact time with the hammer potential 48 kHz float32 (A5 StruckNote)") {
    orc08Case<ham::A5Rig<float>>(48000.0);
}
TEST_CASE("ORC-08 impact time with the hammer potential 48 kHz float64 (A5 StruckNote)") {
    orc08Case<ham::A5Rig<double>>(48000.0);
}
TEST_CASE("ORC-08 impact time with the hammer potential 96 kHz float64 (A5 StruckNote)") {
    orc08Case<ham::A5Rig<double>>(96000.0);
}
TEST_CASE("ORC-09 hammer-string energy one string rigid bridge (A5 StruckNote)") {
    orc09Single<ham::A5Rig<double>>();
}
TEST_CASE("ORC-09 hammer-string energy trichord rigid bridge (A5 StruckNote)") {
    orc09Trichord<ham::A5Rig<double>>();
}
TEST_CASE("ORC-09 hammer-string energy trichord mass-spring bridge reported (A5 StruckNote)") {
    orc09TrichordBridge<ham::A5Rig<double>>(0.0, kBridgeMassOrc09);
    orc09TrichordBridge<ham::A5Rig<double>>(0.0, 3.0);
    orc09TrichordBridge<ham::A5Rig<double>>(0.0, 30.0);
}
TEST_CASE("ORC-09 hammer-string energy trichord rotated bridge reported (A5 StruckNote)") {
    orc09TrichordBridge<ham::A5Rig<double>>(0.1, kBridgeMassOrc09);
}
#endif
