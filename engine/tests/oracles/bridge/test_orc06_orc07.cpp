// SPDX-License-Identifier: MIT
// Covers: ORC-06, ORC-07
//
// ORC-06 Bridge damping (SPEC-ORC, SPEC-BRG "Small-admittance results"): a flexible string on a
// pure conductance G decays, in every kept mode, at
//     sigma = -f0 ln[(1 - Z0 G) / (1 + Z0 G)].
// Tolerance (SPEC-ORC after SI-0021): modes with omega_n dt <= 0.1, 0.1% against the formula (run:
// 1e-4); every kept mode, the measured decay equals the decay of the exact eigenvalue of the
// scheme's one-step coupled map within 1e-4 relative, and the deviation from the formula is
// reported per mode, not bounded (the explicit partitioned order under-estimates the damping by
// about (omega dt)^2).
// ORC-07 Unison coupling: two identical strings on one conductance; in-phase motion decays at
//     4 f0 Z0 G (SPEC's small-admittance value), anti-phase motion only at the intrinsic rate;
//     tolerance 0.1%.
//
// Everything expected is computed here from SPEC-STR / SPEC-BRG with the string's own T and rhoA:
//   rhoA = (pi/4) rho d^2,  T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2,  EI = pi E d^4 / 64,
//   Z0 = sqrt(T rhoA),  f0 = sqrt(T / rhoA) / (2 L),  G = (Z0 G) / Z0.
// Never from the engine's own output. The system under test is the rig (bridge_rig.hpp): float32
// A1 ModeBank states, the bridge coupling in the order of SPEC-BRG "Discrete coupling order".
//
// Method. The string is compiled at 48 kHz with the public pme::compileString (plain steel,
// d = 1 micrometre so that B = pi^2 EI / (T L^2) is below 1e-8: printed by each case; pinned;
// bridge at L). Modes are started at q_n(0) = q0 (exact free-damped initial velocity) and the
// float32 run records q_n(t); each q_n(t) is fitted with pme::analysis::track_partials (a damped
// sinusoid fitted in the frequency domain, one partial per series, started at the SPEC frequency
// n f0 sqrt(1 + B n^2)). Off-resonance contributions of the other coupled modes are at least one
// mode spacing (f0) away, i.e. hundreds of spectral bins, and do not enter the fit.
// The fitted amplitude decay rate sigma is compared with the closed form.
//
// Run (once A4 is merged and the A4 backend is enabled, see CMakeLists.txt):
//   ctest --preset linux-debug -L oracle -R "ORC-0[67]" --output-on-failure

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <map>
#include <pme/test/tolerance.hpp>
#include <string>
#include <vector>

#include "bridge_oracle_common.hpp"

namespace {

namespace brg = pme::oracle::brg;
namespace str = pme::oracle::str;
using brg::kFs;
using brg::kPi;

constexpr double kOrcTol = 1.0e-3;  // SPEC-ORC ORC-06 and ORC-07: 0.1% (run: 1e-4 for ORC-06)
constexpr double kQ0 = 1.0e-3;      // modal start amplitude (linear system: scale is irrelevant)

/// SPEC-BRG: sigma = -f0 ln[(1 - Z0 G) / (1 + Z0 G)] for a flexible string on a conductance.
double conductanceDecay(double f0, double z0g) { return -f0 * std::log((1.0 - z0g) / (1.0 + z0g)); }

/// Runs the rig for `steps` samples and records q_i(t) of segment 0 for i < record.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): physical quantities in SPEC order
std::vector<std::vector<float>> runAndRecord(brg::Rig& rig, std::size_t record, std::size_t steps) {
    std::vector<std::vector<float>> series(record);
    for (auto& s : series) {
        s.resize(steps);
    }
    for (std::size_t t = 0; t < steps; ++t) {
        for (std::size_t i = 0; i < record; ++i) {
            series[i][t] = rig.q(0)[i];
        }
        rig.step();
    }
    return series;
}

std::vector<double> asDouble(const std::vector<float>& v) { return {v.begin(), v.end()}; }

// ---------------------------------------------------------------------------------------------
// ORC-06
// ---------------------------------------------------------------------------------------------

constexpr double kOrc06F1 = 110.0;  // Hz, A2; N = 206 modes at 48 kHz, f_max = +inf
// Run length: the record is cut where the formula's amplitude has fallen by e^-7, because the
// float32 state noise (about 6e-8 of the state) would dominate a longer tail of the fast-decaying
// modes and bias the fit.
constexpr double kOrc06DecayExponents = 7.0;
constexpr double kOrc06MaxDuration = 2.0;  // s
constexpr double kOrc06D = 1.0e-6;         // m: B below 1e-8

struct ModeResult {
    int n{};
    double omegaDt{};  // omega_n dt: the mode's discretisation parameter
    double sigmaMeasured{};
    double sigmaExpected{};  // the SPEC formula
    double sigmaExact{};     // decay rate of the exact eigenvalue of the scheme's one-step map
    double error{};          // sigma_measured / sigma_expected - 1 (deviation from the formula)
    double errorExact{};     // sigma_measured / sigma_exact - 1
};

struct Orc06Run {
    double b{};
    double z0{};
    double f0{};
    std::vector<ModeResult> modes;
};

// Exact eigenvalues of the one-step coupled map (SI-0021). The scheme of SPEC-OSC "Difference
// form" and SPEC-BRG "Discrete coupling order" for one string on a conductance G is linear, so a
// solution q_i^n = Q_i z^n, w^n = W z^n exists iff z is a root of one scalar equation. With
//   mode i:   u^(n+1) = beta u^n - kappa q^n + gamma f^n,  q^(n+1) = q^n + u^(n+1),  u^n = q^n -
//   q^(n-1),
//             f^n = -g delta_tt w^n,  delta_tt w^n = W (z - 1)^2 / (z dt^2) z^n,
//   bridge:   w^(n+1) = w^n + dt G F^n,  F^n = sum a_i q_i^n - (T/L) w^n,
// eliminating Q_i = -gamma_i g_i d / D_i(z), D_i(z) = (z - 1) - beta_i (1 - 1/z) + kappa_i gives
//   h(z) = (z - 1) + dt G T/L + (G / dt) S(z) (z - 1)^2 / z = 0,   S(z) = sum a_i gamma_i g_i /
//   D_i(z).
// The root next to the uncoupled mode root (z^2 + (kappa - 1 - beta) z + beta = 0) is found by the
// secant method; sigma_exact = -ln|z| / dt. beta, kappa, gamma follow SPEC-OSC (SI-0008 form),
// g_n and a_n = rhoA omegaHat_n^2 g_n follow SPEC-BRG, all from the string's own SPEC-derived
// omega_n, sigma_n, in float64. Nothing comes from the engine.
struct SchemeCoeffs {
    double beta, kappa, gamma, omegaHat2;
};

SchemeCoeffs schemeCoeffs(double omega, double sigma) {
    const double dt = 1.0 / kFs;
    const double x = sigma * dt;
    const double wd = std::sqrt((omega - sigma) * (omega + sigma));
    const double sh = std::tanh(x);
    const double s2 = std::sin(0.5 * wd * dt);
    const double sx = std::sinh(0.5 * x);
    const double w2dt2 = 4.0 * (sx * sx + s2 * s2) / std::cosh(x);  // omegaHat^2 dt^2
    return {(1.0 - sh) / (1.0 + sh), w2dt2 / (1.0 + sh), dt * dt / (1.0 + sh), w2dt2 / (dt * dt)};
}

struct ExactModes {
    std::vector<double> sigma;  // decay rates (1/s)
    std::vector<double> hz;     // damped frequencies (Hz), used as start values of the fits
};

ExactModes exactConductanceDecay(const brg::OracleString& s, double conductance) {
    using cplx = std::complex<double>;
    const double dt = 1.0 / kFs;
    const std::size_t count = s.modes.omega.size();
    std::vector<SchemeCoeffs> c(count);
    std::vector<double> g(count);
    std::vector<double> a(count);
    for (std::size_t i = 0; i < count; ++i) {
        c[i] = schemeCoeffs(s.modes.omega[i], s.modes.sigma[i]);
        const double kn = static_cast<double>(i + 1) * kPi / brg::kLength;
        g[i] = std::sqrt(2.0 / brg::kLength) * ((i % 2 == 0) ? 1.0 : -1.0) / kn;  // SPEC-BRG
        a[i] = s.rhoA * c[i].omegaHat2 * g[i];
    }
    const double tOverL = s.tension / brg::kLength;
    // h_k(z) = D_k(z) h(z): the pole of mode k removed, so the root next to its uncoupled root is
    // a smooth, well-conditioned zero.
    const auto hk = [&](std::size_t k, cplx z) {
        cplx sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (i != k) {
                const cplx d = (z - 1.0) - c[i].beta * (1.0 - 1.0 / z) + c[i].kappa;
                sum += a[i] * c[i].gamma * g[i] / d;
            }
        }
        const cplx coupling = (conductance / dt) * (z - 1.0) * (z - 1.0) / z;
        const cplx dk = (z - 1.0) - c[k].beta * (1.0 - 1.0 / z) + c[k].kappa;
        return dk * ((z - 1.0) + dt * conductance * tOverL + coupling * sum) +
               coupling * a[k] * c[k].gamma * g[k];
    };
    ExactModes out;
    out.sigma.resize(count);
    out.hz.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Uncoupled root: z^2 + (kappa - 1 - beta) z + beta = 0, the one with positive imaginary
        // part.
        const double bq = c[i].kappa - 1.0 - c[i].beta;
        const cplx root = 0.5 * (-bq + std::sqrt(cplx(bq * bq - 4.0 * c[i].beta)));
        cplx z = root.imag() >= 0.0 ? root : std::conj(root);
        for (int it = 0; it < 30; ++it) {  // Newton with a central-difference derivative
            const cplx step = z * 1.0e-6;
            const cplx deriv = (hk(i, z + step) - hk(i, z - step)) / (2.0 * step);
            const cplx dz = hk(i, z) / deriv;
            z -= dz;
            if (std::abs(dz) < 1.0e-15) {
                break;
            }
        }
        out.sigma[i] = -std::log(std::abs(z)) / dt;
        out.hz[i] = std::arg(z) / (2.0 * kPi * dt);
    }
    return out;
}

const Orc06Run& orc06Run(double z0g) {
    static std::map<double, Orc06Run> cache;
    const auto it = cache.find(z0g);
    if (it != cache.end()) {
        return it->second;
    }
    Orc06Run run;
    brg::OracleString s;
    REQUIRE(brg::makeString(kOrc06F1, kOrc06D, 0.0, 0.0, str::kInf, s));
    run.b = s.b;
    run.z0 = s.z0;
    run.f0 = s.f0;
    const std::size_t count = s.modes.omega.size();
    REQUIRE(count >= 100);

    brg::RigSetup setup;
    setup.segments = {s.modes};
    setup.ends = {pme::BridgeEnd::AtL};
    setup.model = brg::BridgeModel::pureConductance(z0g / s.z0);
    brg::Rig rig(setup);
    REQUIRE(rig.ok());
    for (std::size_t i = 0; i < count; ++i) {
        brg::startMode(rig, 0, i, s.modes, kQ0);
    }
    const double duration =
        std::min(kOrc06MaxDuration, kOrc06DecayExponents / conductanceDecay(s.f0, z0g));
    const auto steps = static_cast<std::size_t>(std::llround(duration * kFs));
    const auto series = runAndRecord(rig, count, steps);

    const double expected = conductanceDecay(s.f0, z0g);
    const ExactModes exact = exactConductanceDecay(s, z0g / s.z0);
    for (std::size_t i = 0; i < count; ++i) {
        const int n = static_cast<int>(i) + 1;
        const double fStart = brg::dampedPartialHz(s, n, 0.0);
        // Joint fit of the mode and its two neighbours (present in q_n(t) through the coupling),
        // so that their spectral leakage does not enter the estimate of mode n.
        std::vector<double> starts;
        std::size_t self = 0;
        for (int m = n - 3; m <= n + 3; ++m) {
            if (m >= 1 && static_cast<std::size_t>(m) <= count) {
                if (m == n) {
                    self = starts.size();
                }
                starts.push_back(exact.hz[static_cast<std::size_t>(m) - 1]);
            }
        }
        const auto fit = brg::fitJoint(asDouble(series[i]), starts, self);
        ModeResult r;
        r.n = n;
        r.omegaDt = 2.0 * kPi * fStart / kFs;
        r.sigmaMeasured = fit.sigma_per_s;
        r.sigmaExpected = expected;
        r.sigmaExact = exact.sigma[i];
        r.error = fit.sigma_per_s / expected - 1.0;
        r.errorExact = fit.sigma_per_s / exact.sigma[i] - 1.0;
        run.modes.push_back(r);
    }
    return cache.emplace(z0g, std::move(run)).first->second;
}

void reportOrc06(double z0g, const Orc06Run& run, const char* what) {
    double worst = 0.0;
    int worstN = 0;
    for (const auto& m : run.modes) {
        if (std::fabs(m.error) > worst) {
            worst = std::fabs(m.error);
            worstN = m.n;
        }
    }
    std::string profile;
    for (const int n : {1, 2, 3, 4, 6, 8, 16, 32, 64, 128, 200}) {
        if (static_cast<std::size_t>(n) <= run.modes.size()) {
            profile += " n=" + std::to_string(n) + ":" +
                       str::sci(run.modes[static_cast<std::size_t>(n) - 1].error);
        }
    }
    str::report("ORC-06 error profile sigma/sigma_SPEC - 1 at" + profile);
    // Every mode, eight per line: deviation from the formula (reported, not bounded) and, after
    // the slash, the deviation from the exact eigenvalue of the scheme.
    std::string line;
    for (const auto& m : run.modes) {
        line += " " + std::to_string(m.n) + ":" + str::sci(m.error) + "/" + str::sci(m.errorExact);
        if (m.n % 6 == 0 || static_cast<std::size_t>(m.n) == run.modes.size()) {
            str::report("ORC-06 Z0G " + std::to_string(z0g) +
                        " per-mode deviation formula/exact:" + line);
            line.clear();
        }
    }
    str::report(std::string("ORC-06 ") + what + " Z0G = " + std::to_string(z0g) +
                ": B = " + str::sci(run.b) + ", Z0 = " + str::sci(run.z0) +
                ", sigma = " + str::sci(run.modes.front().sigmaExpected) + " 1/s, " +
                std::to_string(run.modes.size()) + " modes, worst |error| = " + str::sci(worst) +
                " at mode " + std::to_string(worstN));
}

/// Checks the modes selected by `include` against kOrcTol and fails with the worst mode.
template <class Pred>
void checkOrc06(double z0g, const char* what, const Pred& include) {
    const Orc06Run& run = orc06Run(z0g);
    CHECK(run.b < 1.0e-8);  // flexible: B < 1e-8 (ORC-06 setup)
    reportOrc06(z0g, run, what);
    double worst = 0.0;
    int worstN = 0;
    int failing = 0;
    int firstFailing = 0;
    int checked = 0;
    for (const auto& m : run.modes) {
        if (!include(m)) {
            continue;
        }
        ++checked;
        if (std::fabs(m.error) > worst) {
            worst = std::fabs(m.error);
            worstN = m.n;
        }
        if (!(std::fabs(m.error) <= kOrcTol)) {  // also catches NaN
            ++failing;
            if (firstFailing == 0) {
                firstFailing = m.n;
            }
        }
    }
    REQUIRE(checked > 0);
    INFO("Z0 G = " << z0g << ": " << failing << " of " << checked << " checked modes exceed "
                   << kOrcTol << "; first at mode " << firstFailing << "; worst mode " << worstN
                   << " with |sigma/sigma_SPEC - 1| = " << worst);
    CHECK(failing == 0);
}

constexpr double kExactTol = 1.0e-4;  // SPEC-ORC ORC-06 (SI-0021): exact one-step map, relative

/// Every kept mode: the measured decay rate equals the exact eigenvalue decay of the scheme.
void checkExactMap(double z0g) {
    const Orc06Run& run = orc06Run(z0g);
    CHECK(run.b < 1.0e-8);
    reportOrc06(z0g, run, "every kept mode vs exact map");
    double worst = 0.0;
    int worstN = 0;
    int failing = 0;
    for (const auto& m : run.modes) {
        if (!(std::fabs(m.errorExact) <= kExactTol)) {  // also catches NaN
            ++failing;
        }
        if (std::fabs(m.errorExact) > worst) {
            worst = std::fabs(m.errorExact);
            worstN = m.n;
        }
    }
    str::report("ORC-06 Z0G " + std::to_string(z0g) +
                ": worst |sigma_measured / sigma_exact - 1| = " + str::sci(worst) + " at mode " +
                std::to_string(worstN) + " of " + std::to_string(run.modes.size()));
    INFO("Z0 G = " << z0g << ": " << failing << " of " << run.modes.size()
                   << " modes differ from the exact eigenvalue decay by more than " << kExactTol
                   << "; worst mode " << worstN << " with " << worst);
    CHECK(failing == 0);
}

}  // namespace

TEST_CASE("ORC-06 bridge damping every kept mode against the exact one-step map with Z0G = 0.01") {
    // SPEC-ORC ORC-06 (SI-0021): every kept mode within 1e-4 of the exact eigenvalues of the
    // scheme's one-step coupled map; the deviation from the formula is reported per mode.
    checkExactMap(0.01);
}

TEST_CASE("ORC-06 bridge damping every kept mode against the exact one-step map with Z0G = 0.05") {
    checkExactMap(0.05);
}

TEST_CASE("ORC-06 bridge damping modes with omega dt up to 0.1 with Z0G = 0.01") {
    // SPEC-ORC ORC-06 (SI-0021): modes with omega_n dt <= 0.1 against the formula, 0.1%. Above
    // that the explicit partitioned order under-estimates the damping by about (omega dt)^2.
    checkOrc06(0.01, "omega dt <= 0.1", [](const ModeResult& m) { return m.omegaDt <= 0.1; });
}

TEST_CASE("ORC-06 bridge damping modes with omega dt up to 0.1 with Z0G = 0.05") {
    checkOrc06(0.05, "omega dt <= 0.1", [](const ModeResult& m) { return m.omegaDt <= 0.1; });
}

// ---------------------------------------------------------------------------------------------
// ORC-07
// ---------------------------------------------------------------------------------------------

namespace {

constexpr double kOrc07F1 = 55.0;       // Hz (A1): the first 10 modes stay below omega dt = 0.08
constexpr double kOrc07D = 1.0e-6;      // m: B below 1e-8
constexpr double kOrc07B1 = 1.0;        // 1/s intrinsic loss; b2 = b3 = 0: sigma_n = b1 for every n
constexpr double kOrc07Z0G = 0.01;      // Z0 G of ORC-07
constexpr double kOrc07Duration = 3.0;  // s
constexpr int kOrc07Modes = 10;

struct PairRun {
    std::vector<double> sigma;  // fitted sigma of modes 1..kOrc07Modes of string 1
    double f0{};
    double z0{};
    double b{};
};

/// Two identical strings on one conductance; `sign` = +1 starts them in phase, -1 in anti-phase.
PairRun runPair(double sign) {
    brg::OracleString s;
    REQUIRE(brg::makeString(kOrc07F1, kOrc07D, kOrc07B1, 0.0, str::kInf, s));
    PairRun out;
    out.f0 = s.f0;
    out.z0 = s.z0;
    out.b = s.b;
    brg::RigSetup setup;
    setup.segments = {s.modes, s.modes};
    setup.ends = {pme::BridgeEnd::AtL, pme::BridgeEnd::AtL};
    setup.model = brg::BridgeModel::pureConductance(kOrc07Z0G / s.z0);
    brg::Rig rig(setup);
    REQUIRE(rig.ok());
    for (std::size_t i = 0; i < static_cast<std::size_t>(kOrc07Modes); ++i) {
        brg::startMode(rig, 0, i, s.modes, kQ0);
        brg::startMode(rig, 1, i, s.modes, sign * kQ0);
    }
    const auto steps = static_cast<std::size_t>(std::llround(kOrc07Duration * kFs));
    const auto series = runAndRecord(rig, static_cast<std::size_t>(kOrc07Modes), steps);
    for (int n = 1; n <= kOrc07Modes; ++n) {
        // Start at the damped SPEC frequency of the uncoupled mode.
        const double fStart = brg::dampedPartialHz(s, n, kOrc07B1);
        out.sigma.push_back(
            brg::fitOne(asDouble(series[static_cast<std::size_t>(n) - 1]), fStart).sigma_per_s);
    }
    return out;
}

}  // namespace

TEST_CASE("ORC-07 unison coupling in-phase decays at 4 f0 Z0 G plus the intrinsic rate") {
    // SPEC-BRG: two identical strings on one conductance, in-phase motion decays at 4 f0 Z0 G.
    // 4 f0 Z0 G is SPEC's small-admittance value for the bridge contribution; its relative
    // difference from the exact -f0 ln[(1 - 2 Z0 G) / (1 + 2 Z0 G)] is (2 Z0 G)^2 / 3 = 1.3e-4 at
    // Z0 G = 0.01, below the tolerance. The intrinsic loss b1 adds exactly (sigma_n = b1 + bridge
    // part), so two readings are checked, both at 0.1%: the bridge part sigma_measured - b1
    // against 4 f0 Z0 G (the stricter one, since b1 is not small against it), and the total
    // against b1 + 4 f0 Z0 G.
    const PairRun run = runPair(+1.0);
    const double bridge = 4.0 * run.f0 * kOrc07Z0G;
    CHECK(run.b < 1.0e-8);
    double worstPart = 0.0;
    double worstTotal = 0.0;
    for (int n = 1; n <= kOrc07Modes; ++n) {
        const double measured = run.sigma[static_cast<std::size_t>(n) - 1];
        const double part = (measured - kOrc07B1) / bridge - 1.0;
        const double total = measured / (kOrc07B1 + bridge) - 1.0;
        worstPart = std::max(worstPart, std::fabs(part));
        worstTotal = std::max(worstTotal, std::fabs(total));
        INFO("mode " << n << ": sigma = " << measured
                     << ", 4 f0 Z0 G + b1 = " << bridge + kOrc07B1);
        CHECK(std::fabs(part) <= kOrcTol);
        CHECK(std::fabs(total) <= kOrcTol);
    }
    str::report("ORC-07 in-phase: 4 f0 Z0 G = " + str::sci(bridge) +
                " 1/s, B = " + str::sci(run.b) + ", worst bridge-part error " +
                str::sci(worstPart) + ", worst total error " + str::sci(worstTotal) + " over " +
                std::to_string(kOrc07Modes) + " modes");
}

TEST_CASE("ORC-07 unison coupling anti-phase decays at the intrinsic rate only") {
    // SPEC-BRG: in anti-phase the two forces cancel, the bridge does not move, and the modes
    // decay at b1 alone (sigma_n = b1 for every n since b2 = b3 = 0).
    const PairRun run = runPair(-1.0);
    double worst = 0.0;
    for (int n = 1; n <= kOrc07Modes; ++n) {
        const double measured = run.sigma[static_cast<std::size_t>(n) - 1];
        const double err = measured / kOrc07B1 - 1.0;
        worst = std::max(worst, std::fabs(err));
        INFO("mode " << n << ": sigma = " << measured << ", b1 = " << kOrc07B1);
        CHECK(std::fabs(err) <= kOrcTol);
    }
    str::report("ORC-07 anti-phase: worst |sigma / b1 - 1| = " + str::sci(worst) + " over " +
                std::to_string(kOrc07Modes) + " modes");
}
