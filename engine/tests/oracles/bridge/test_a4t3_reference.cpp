// SPDX-License-Identifier: MIT
// Covers: A4-T3
//
// A4-T3 Bridge coupling against the P0.5 reference solver (phase card A4; PLAN-VAL).
// "Against P0.5, two strings on a mass-spring-damper bridge match partial frequencies within 0.05
// cent and decay rates within 2%."
//
// System. Two plain steel strings on one bridge point (SPEC-BRG "Unison coupling"), pinned,
// L = 0.65 m, core d = 1 mm, f1 = 196 Hz (string 0) and 270 Hz (string 1), so that B =
// pi^2 EI / (T L^2) is 5.7e-4 and 3.2e-4 (printed), and the low partials of the two strings are
// at least 8 Hz apart (checked below). Loss b1 = 0.5 1/s, b2 = 0 (the reference accepts only
// b2 = 0), b3 = 2e-9 s. No detuneH, bridge at L, theta_b = 0, G_h = 0 (rigid horizontal axis;
// only the vertical polarisation is excited, so the horizontal axis is irrelevant).
//
// Bridge (SPEC-BODY "Local bridge model", one mode): mass m = 0.02 kg, stiffness k_b = m Omega^2,
// Omega = 2 pi 600 Hz (resonance 600 Hz, above the compared partials of the low string and between
// partials of the other), dashpot r_b = 250 N s/m. Admittance (SPEC-CONV e^(+iwt)):
// Y(w) = 1 / (r_b + i (w m - k_b / w)). The test checks, from the small-admittance values of
// SPEC-BRG, that the bridge-induced decay of the first partial of each string is at least
// 3 b1 (sigma = 2 f0 Z0 Re Y: 1.7 and 3.6 1/s against 3 b1 = 1.5) and that Z0 |Y| < 0.1 at every
// compared partial (it is below 0.011).
//
// Engine side: public compileString (48 kHz, f_max = +inf, one registered point x = 0.37 L), the
// rig of bridge_rig.hpp (float32 A1 ModeBank states, mass-spring-damper bridge, SPEC-BRG coupling
// order), both strings released from a triangle of 1 mm peak at 0.12 L at rest (q_n(0) = the
// projection integral of the triangle on phi_n, Gauss-Legendre in float64, not from the engine).
// y_s(x = 0.37 L) = sum phiBar_n q_n is recorded for 2 s.
//
// Reference side (P0.5, reference/scenario.schema.json): two strings with "bridge": 0 and a
// "mass_spring_dashpot" bridge, observed at 0.37 L, rendered by `pme_reference_cli render` at three
// refinement levels (grid intervals N0 2^l and oversampling OS0 2^l, l = 0, 1, 2; PME_A4T3_N0 and
// PME_A4T3_OS0 override 400 and 4). The scheme is second order, so partial frequencies and decay
// rates are Richardson-extrapolated, x* = x_2 + (x_2 - x_1) / 3; the reference's own error
// estimate |x*_(1,2) - x*_(0,1)| is required below 0.01 cent for each compared partial and is
// printed with the per-partial engine-vs-reference errors.
//
// Analysis: pme::analysis::track_partials with the same settings on every signal; the 8 first
// partials of the string observed (the 8 first of the other string are fitted jointly, as they
// are present in the coupled signal, and not compared). Pass: |1200 log2(f_engine / f_ref)| <=
// 0.05 cent and |sigma_engine / sigma_ref - 1| <= 0.02 for every compared partial.
//
// The reference case exists only when CMake finds the target pme_reference_cli and the build is
// optimised (PME_A4T3_REFERENCE_CLI defined, see CMakeLists.txt). The engine-side case always
// exists.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <pme/analysis/wav.hpp>
#include <pme/test/tolerance.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "bridge_oracle_common.hpp"

namespace {

namespace brg = pme::oracle::brg;
namespace str = pme::oracle::str;
using brg::kFs;
using brg::kPi;

constexpr double kDuration = 2.0;
constexpr double kObsFraction = 0.37;
constexpr double kPluckFraction = 0.12;
constexpr double kPluckHeight = 1.0e-3;
constexpr double kFreqTolCents = 0.05;  // card A4-T3
constexpr double kDecayTol = 0.02;      // card A4-T3
constexpr int kPartials = 8;
constexpr double kB1 = 0.5;
constexpr double kB3 = 2.0e-9;
constexpr double kDCore = 1.0e-3;
constexpr std::array<double, 2> kF1{196.0, 270.0};

// Bridge: m, k_b, r_b chosen as described in the header comment.
constexpr double kBridgeMass = 0.02;
constexpr double kBridgeResonanceHz = 600.0;
constexpr double kBridgeDamping = 250.0;
const double kBridgeStiffness =
    kBridgeMass * (2.0 * kPi * kBridgeResonanceHz) * (2.0 * kPi * kBridgeResonanceHz);

/// Bridge admittance (velocity per force) under SPEC-CONV's e^(+iwt): 1 / (r + i (w m - k / w)).
std::complex<double> bridgeAdmittance(double omega) {
    return 1.0 /
           std::complex<double>(kBridgeDamping, omega * kBridgeMass - kBridgeStiffness / omega);
}

struct T3Strings {
    std::array<brg::OracleString, 2> s;
};

T3Strings buildStrings() {
    T3Strings t;
    const std::array<double, 1> points{kObsFraction * brg::kLength};
    for (std::size_t i = 0; i < 2; ++i) {
        REQUIRE(brg::makeString(kF1[i], kDCore, kB1, kB3, str::kInf, t.s[i], points));
        REQUIRE(t.s[i].modes.phiBar.size() == 1);
        REQUIRE(t.s[i].modes.omega.size() >= static_cast<std::size_t>(kPartials));
    }
    return t;
}

/// SPEC partial frequencies (undamped) n f0 sqrt(1 + B n^2) of string i, n = 1..kPartials.
std::vector<double> specPartials(const brg::OracleString& s) {
    std::vector<double> f;
    for (int n = 1; n <= kPartials; ++n) {
        f.push_back(str::stiffPartial(s.f0, s.b, n));
    }
    return f;
}

/// Triangular displacement of peak h at xp on [0, L]: the integral of y0 phi_n, float64.
double pluckProjection(double L, double xp, double h, int n) {
    static const str::GaussLegendre rule = str::gaussLegendre(64);
    const auto left = [&](double x) { return h * (x / xp) * str::modeShape(L, n, x); };
    const auto right = [&](double x) { return h * ((L - x) / (L - xp)) * str::modeShape(L, n, x); };
    return str::integrate(left, 0.0, xp, rule) + str::integrate(right, xp, L, rule);
}

/// Engine signals y_0, y_1 at x = 0.37 L for kDuration seconds at kFs.
std::array<std::vector<double>, 2> engineSignals(const T3Strings& t) {
    brg::RigSetup setup;
    setup.segments = {t.s[0].modes, t.s[1].modes};
    setup.ends = {pme::BridgeEnd::AtL, pme::BridgeEnd::AtL};
    setup.model = brg::BridgeModel::lumped(kBridgeMass, kBridgeStiffness, kBridgeDamping);
    brg::Rig rig(setup);
    REQUIRE(rig.ok());
    const double dt = 1.0 / kFs;
    for (std::size_t s = 0; s < 2; ++s) {
        const auto& m = t.s[s].modes;
        for (std::size_t i = 0; i < m.omega.size(); ++i) {
            const int n = static_cast<int>(i) + 1;
            const double q0 =
                pluckProjection(brg::kLength, kPluckFraction * brg::kLength, kPluckHeight, n);
            const double sigma = m.sigma[i];
            const double wd = std::sqrt((m.omega[i] - sigma) * (m.omega[i] + sigma));
            const double qPrev =
                q0 * std::exp(sigma * dt) * (std::cos(wd * dt) - (sigma / wd) * std::sin(wd * dt));
            rig.q(s)[i] = static_cast<float>(q0);
            rig.u(s)[i] = static_cast<float>(q0 - qPrev);
        }
    }
    const auto steps = static_cast<std::size_t>(std::llround(kDuration * kFs));
    std::array<std::vector<double>, 2> y;
    for (auto& v : y) {
        v.assign(steps, 0.0);
    }
    for (std::size_t k = 0; k < steps; ++k) {
        for (std::size_t s = 0; s < 2; ++s) {
            double sum = 0.0;
            const auto& phi = t.s[s].modes.phiBar[0];
            for (std::size_t i = 0; i < phi.size(); ++i) {
                sum += static_cast<double>(phi[i]) * static_cast<double>(rig.q(s)[i]);
            }
            y[s][k] = sum;
        }
        rig.step();
    }
    return y;
}

/// Fits the 16 partials (own 8, then the other string's 8) of one signal from the given starts.
pme::analysis::PartialTrackingResult track(const std::vector<double>& y,
                                           const std::vector<double>& start) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = start;
    opt.envelope_frame_s = 0.0;
    return pme::analysis::track_partials(std::span<const double>(y), kFs, opt);
}

/// Start list for the fit of signal `own`: own string's partials first, then the other's.
std::vector<double> startList(const T3Strings& t, std::size_t own) {
    std::vector<double> a = specPartials(t.s[own]);
    const std::vector<double> b = specPartials(t.s[1 - own]);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

TEST_CASE("A4-T3 setup meets the card: bridge decay above 3 b1 and Z0 |Y| below 0.1") {
    // The preconditions of the card, from SPEC-BRG's small-admittance values (SPEC-CONV e^(+iwt)):
    // sigma_bridge = 2 f0 Z0 Re Y(w_1) >= 3 b1 for the first partial of each string; Z0 |Y| < 0.1
    // at every compared partial; the compared partials of different strings are apart.
    const T3Strings t = buildStrings();
    std::vector<double> all;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& s = t.s[i];
        const double w1 = 2.0 * kPi * str::stiffPartial(s.f0, s.b, 1);
        const double sigmaBridge = 2.0 * s.f0 * s.z0 * bridgeAdmittance(w1).real();
        str::report("A4-T3 string " + std::to_string(i) + ": f1 = " + str::sci(s.f0) +
                    " Hz, B = " + str::sci(s.b) + ", Z0 = " + str::sci(s.z0) +
                    " kg/s, bridge decay of partial 1 " + str::sci(sigmaBridge) +
                    " 1/s (3 b1 = " + str::sci(3.0 * kB1) + ")");
        CHECK(sigmaBridge >= 3.0 * kB1);
        for (int n = 1; n <= kPartials; ++n) {
            const double f = str::stiffPartial(s.f0, s.b, n);
            const double zy = s.z0 * std::abs(bridgeAdmittance(2.0 * kPi * f));
            CHECK(zy < 0.1);
            all.push_back(f);
        }
    }
    std::sort(all.begin(), all.end());
    double minGap = 1.0e9;
    for (std::size_t i = 1; i < all.size(); ++i) {
        minGap = std::min(minGap, all[i] - all[i - 1]);
    }
    str::report("A4-T3 smallest gap between compared partials of the two strings: " +
                str::sci(minGap) + " Hz; bridge resonance " + str::sci(kBridgeResonanceHz) + " Hz");
    CHECK(minGap >= 8.0);
}

TEST_CASE("A4-T3 engine side shows the bridge shift and decay SPEC-BRG predicts to first order") {
    // Attribution aid for a reference disagreement, not the card's criterion: the engine signal
    // against SPEC-BRG's small-admittance results. A string alone on this bridge would show
    // sigma = b1 + b3 w^2 + 2 f0 Z0 Re Y and a shift -2 f0 Z0 Im Y rad/s in angular frequency; the
    // other string and Z0 |Y| up to 0.011 make the first-order values approximate, so the check is
    // loose (25% of the bridge-induced decay, 40% of the shift, both for partial 1 and 2 only).
    const T3Strings t = buildStrings();
    const auto y = engineSignals(t);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto fit = track(y[i], startList(t, i));
        REQUIRE(fit.partials.size() == 2 * static_cast<std::size_t>(kPartials));
        const auto& s = t.s[i];
        for (int n = 1; n <= 2; ++n) {
            const double w = 2.0 * kPi * str::stiffPartial(s.f0, s.b, n);
            const auto yb = bridgeAdmittance(w);
            const double sigmaOwn = kB1 + kB3 * w * w;
            const double sigmaBridge = 2.0 * s.f0 * s.z0 * yb.real();
            const double dOmega = -2.0 * s.f0 * s.z0 * yb.imag();
            const auto& p = fit.partials[static_cast<std::size_t>(n) - 1];
            const double measuredBridge = p.sigma_per_s - sigmaOwn;
            const double measuredShift =
                2.0 * kPi * p.frequency_hz - std::sqrt((w - sigmaOwn) * (w + sigmaOwn));
            INFO("string " << i << " partial " << n << ": bridge decay " << measuredBridge << " vs "
                           << sigmaBridge << ", shift " << measuredShift << " vs " << dOmega
                           << " rad/s");
            CHECK(std::fabs(measuredBridge / sigmaBridge - 1.0) <= 0.25);
            CHECK(std::fabs(measuredShift / dOmega - 1.0) <= 0.40);
        }
    }
}

#ifdef PME_A4T3_REFERENCE_CLI

namespace {

namespace fs = std::filesystem;

constexpr double kRefErrCents = 0.01;  // reference's own error estimate, cents

int envInt(const char* name, int fallback) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return fallback;
    }
    const int value = (*buffer != '\0') ? std::atoi(buffer) : fallback;  // NOLINT(cert-err34-c)
    std::free(buffer);                                                   // NOLINT
    return value;
#else
    const char* v = std::getenv(name);                              // NOLINT(concurrency-mt-unsafe)
    return (v != nullptr && *v != '\0') ? std::atoi(v) : fallback;  // NOLINT(cert-err34-c)
#endif
}

struct RefGrid {
    int intervals;
    int oversampling;
};

/// Writes the P0.5 scenario (schema_version 1, reference/scenario.schema.json): two strings with
/// bridge 0, a mass_spring_dashpot bridge, observations of both strings at 0.37 L. This is the
/// single place that knows the scenario field names.
void writeScenario(const fs::path& path, const T3Strings& t, const RefGrid& grid) {
    std::ostringstream j;
    j << std::setprecision(17);
    j << "{\n  \"schema_version\": 1,\n  \"name\": \"A4-T3 two strings on a mass-spring-damper "
         "bridge\",\n";
    j << "  \"fs\": " << kFs << ",\n  \"oversampling\": " << grid.oversampling << ",\n";
    j << "  \"duration\": " << kDuration << ",\n  \"strings\": [\n";
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& sp = t.s[i].spec;
        j << "    {\"params\": {\"L\": " << sp.L << ", \"dCore\": " << sp.dCore
          << ", \"rhoCore\": " << sp.rhoCore << ", \"ECore\": " << sp.ECore
          << ", \"dOuter\": " << sp.dOuter << ", \"rhoWind\": 0, \"packing\": 0, \"epsWind\": 0"
          << ", \"f1Target\": " << sp.f1 << ", \"b1\": " << sp.b1
          << ", \"b2\": 0, \"b3\": " << sp.b3
          << ", \"boundary\": \"pinned\", \"detuneH\": 0, \"bridgeEnd\": \"at_L\"},\n"
          << "     \"grid\": {\"intervals\": " << grid.intervals << "},\n"
          << "     \"bridge\": 0,\n"
          << "     \"initial_displacement\": {\"amplitude\": " << kPluckHeight
          << ", \"position\": " << kPluckFraction * sp.L << "}}" << (i == 0 ? "," : "") << "\n";
    }
    j << "  ],\n  \"bridges\": [{\"position\": {\"x\": 0, \"y\": 0, \"z\": 0}, \"theta_b\": 0, "
         "\"conductance_h\": 0,\n    \"model\": {\"type\": \"mass_spring_dashpot\", \"mass\": "
      << kBridgeMass << ", \"stiffness\": " << kBridgeStiffness
      << ", \"damping\": " << kBridgeDamping << "}}],\n";
    j << "  \"observations\": [\n";
    for (std::size_t i = 0; i < 2; ++i) {
        j << "    {\"name\": \"y" << i << "\", \"kind\": \"string_displacement\", \"string\": " << i
          << ", \"x\": " << kObsFraction * brg::kLength << ", \"polarisation\": \"v\"}"
          << (i == 0 ? "," : "") << "\n";
    }
    j << "  ]\n}\n";
    std::ofstream out(path);
    out << j.str();
}

std::string quote(const std::string& text) { return '"' + text + '"'; }

/// Runs the CLI and returns the two observation signals.
std::array<std::vector<double>, 2> renderReference(const fs::path& dir, const T3Strings& t,
                                                   int level, const RefGrid& grid) {
    const std::string tag = "t3_" + std::to_string(level);
    const fs::path scenario = dir / (tag + ".json");
    const fs::path features = dir / (tag + "_features.json");
    const fs::path wav = dir / (tag + ".wav");
    writeScenario(scenario, t, grid);
    std::string cmd = quote(PME_A4T3_REFERENCE_CLI) + " render " + quote(scenario.string()) +
                      " --features " + quote(features.string()) + " --wav " + quote(wav.string());
#ifdef _WIN32
    cmd = '"' + cmd + '"';  // cmd.exe /c strips one pair of outer quotes
#endif
    INFO("reference command: " << cmd);
    const int rc = std::system(cmd.c_str());  // NOLINT(concurrency-mt-unsafe,cert-env33-c)
    REQUIRE(rc == 0);
    const pme::analysis::WavData data = pme::analysis::read_wav(wav);
    REQUIRE(data.sample_rate_hz == doctest::Approx(kFs));
    REQUIRE(data.channels == 2);
    std::array<std::vector<double>, 2> y;
    for (std::size_t f = 0; f < data.frames(); ++f) {
        y[0].push_back(data.samples[f * data.channels]);
        y[1].push_back(data.samples[f * data.channels + 1]);
    }
    return y;
}

}  // namespace

TEST_CASE("A4-T3 two strings on a mass-spring-damper bridge vs P0.5 reference solver") {
    const int n0 = envInt("PME_A4T3_N0", 400);
    const int os0 = envInt("PME_A4T3_OS0", 4);
    const T3Strings t = buildStrings();
    const auto engineY = engineSignals(t);

    const fs::path dir = fs::temp_directory_path() / ("pme_a4t3_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    std::array<std::array<std::vector<double>, 2>, 3> refY;
    for (int level = 0; level < 3; ++level) {
        refY[static_cast<std::size_t>(level)] =
            renderReference(dir, t, level, RefGrid{n0 << level, os0 << level});
    }
    std::error_code ec;
    fs::remove_all(dir, ec);

    double worstCentsAll = 0.0;
    double worstDecayAll = 0.0;
    double worstRefAll = 0.0;
    for (std::size_t obs = 0; obs < 2; ++obs) {
        const std::vector<double> start = startList(t, obs);
        const auto engine = track(engineY[obs], start);
        REQUIRE(engine.partials.size() == start.size());
        std::vector<double> engineHz;
        for (const auto& p : engine.partials) {
            engineHz.push_back(p.frequency_hz);
        }
        // The joint fit converges only from a start within about one spectral bin of each peak;
        // the coarse levels are off by more than that at the upper partials, so they start from
        // the engine's fitted frequencies moved by the second-order prediction
        // f_engine + 4^(2 - l) (f_2 - f_engine). Start values only: every compared number is the
        // converged fit of the reference signal itself.
        std::array<pme::analysis::PartialTrackingResult, 3> ref;
        ref[2] = track(refY[2][obs], engineHz);
        REQUIRE(ref[2].partials.size() == start.size());
        for (int level = 1; level >= 0; --level) {
            const double scale = std::pow(4.0, 2 - level);
            std::vector<double> s;
            for (std::size_t i = 0; i < engineHz.size(); ++i) {
                s.push_back(engineHz[i] + scale * (ref[2].partials[i].frequency_hz - engineHz[i]));
            }
            ref[static_cast<std::size_t>(level)] =
                track(refY[static_cast<std::size_t>(level)][obs], s);
        }
        double worstCents = 0.0;
        double worstDecay = 0.0;
        double worstRef = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kPartials); ++i) {
            const double f0v = ref[0].partials[i].frequency_hz;
            const double f1v = ref[1].partials[i].frequency_hz;
            const double f2v = ref[2].partials[i].frequency_hz;
            const double s0 = ref[0].partials[i].sigma_per_s;
            const double s1 = ref[1].partials[i].sigma_per_s;
            const double s2 = ref[2].partials[i].sigma_per_s;
            // Richardson extrapolation with the scheme's second order, from the two finest and
            // the two coarsest levels.
            const double fRef = f2v + (f2v - f1v) / 3.0;
            const double fRefCoarse = f1v + (f1v - f0v) / 3.0;
            const double sRef = s2 + (s2 - s1) / 3.0;
            const double sRefCoarse = s1 + (s1 - s0) / 3.0;
            const double refErrCents = std::fabs(pme::test::cents(fRef, fRefCoarse));
            const double cents = pme::test::cents(engine.partials[i].frequency_hz, fRef);
            const double decay = engine.partials[i].sigma_per_s / sRef - 1.0;
            INFO("string " << obs << " partial " << i + 1
                           << ": f_engine = " << engine.partials[i].frequency_hz
                           << " f_ref = " << fRef << " (levels " << f0v << ", " << f1v << ", "
                           << f2v << ") sigma_engine = " << engine.partials[i].sigma_per_s
                           << " sigma_ref = " << sRef << " (coarse-pair extrapolation "
                           << sRefCoarse << ")");
            CHECK(std::fabs(cents) <= kFreqTolCents);
            CHECK(std::fabs(decay) <= kDecayTol);
            CHECK(refErrCents <= kRefErrCents);
            worstCents = std::max(worstCents, std::fabs(cents));
            worstDecay = std::max(worstDecay, std::fabs(decay));
            worstRef = std::max(worstRef, refErrCents);
            str::report("A4-T3 string " + std::to_string(obs) + " partial " +
                        std::to_string(i + 1) + ": " + str::sci(cents) + " cent, decay " +
                        str::sci(decay) + ", reference error estimate " + str::sci(refErrCents) +
                        " cent");
        }
        str::report("A4-T3 string " + std::to_string(obs) + ": worst " + str::sci(worstCents) +
                    " cent, worst decay " + str::sci(worstDecay) + ", worst reference estimate " +
                    str::sci(worstRef) + " cent");
        worstCentsAll = std::max(worstCentsAll, worstCents);
        worstDecayAll = std::max(worstDecayAll, worstDecay);
        worstRefAll = std::max(worstRefAll, worstRef);
    }
    str::report("A4-T3 overall: worst " + str::sci(worstCentsAll) + " cent, worst decay " +
                str::sci(worstDecayAll) + ", worst reference estimate " + str::sci(worstRefAll) +
                " cent");
}

#endif  // PME_A4T3_REFERENCE_CLI
