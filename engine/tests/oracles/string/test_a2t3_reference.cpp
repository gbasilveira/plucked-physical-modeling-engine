// SPDX-License-Identifier: MIT
// Covers: A2-T3
//
// A2-T3 Stiff string compiler against the P0.5 reference solver (phase card A2; PLAN-VAL).
// "Against P0.5, a freely vibrating stiff string matches partial frequencies within 0.05 cent and
// decay rates within 1%."
//
// Strings (pinned, fixed bridge, no bridge coupling): L = 0.64 m, f1 about 262 Hz (261.63 Hz, C4),
// plain steel (rho 7850, E 2e11) with core diameter chosen for B about 4e-4 (d = 1.08 mm) and for
// B about 1e-2 (d = 5.4 mm), b1 = 0.5 1/s, b2 = 0 (the reference accepts only b2 = 0), b3 = 2e-9 s.
//
// Engine side (public API + the A1 float32 ModeBank): compileString at 48 kHz, f_max = +inf, one
// registered point at x_obs = 0.37 L; the vertical modes run in pme::osc::ModeBank from a small
// triangular initial displacement (peak 1 mm at 0.12 L, zero velocity). q_n(0) is the projection
// integral of the triangle on phi_n in float64 (Gauss-Legendre per linear segment, not the
// engine); the exact initial pair (q^0, u^0 = q^0 - q^-1) of the damped solution q(t) =
// q_n(0) e^(-sigma t)(cos w_d t + (sigma / w_d) sin w_d t) is used so that the discrete
// recursion reproduces the continuous mode. y(x_obs) = sum phiBar_n q_n is recorded for 2 s.
//
// Reference side (P0.5, documented interface of docs/agents/briefs/P0.5-implementer.md, "P0.5-R7
// tools"): the CLI `pme_reference_cli render <scenario.json> --features <f.json> --wav <w.wav>`
// renders the same string and initial condition on a finite-difference grid. The WAV holds the
// observation signal at 0.37 L. The CLI is run at three refinement levels (grid intervals N0 2^l
// and oversampling OS0 2^l, l = 0, 1, 2; PME_A2T3_N0 and PME_A2T3_OS0 override the defaults 400 and
// 4), and the partial frequencies and decay rates are Richardson-extrapolated with the scheme's
// second order: x* = x_2 + (x_2 - x_1) / 3. The reference's own error estimate is
// |x*_(1,2) - x*_(0,1)| (two extrapolations), required below 0.01 cent for each compared partial,
// and printed with the per-partial engine-vs-reference errors.
//
// Analysis: pme::analysis::track_partials (damped sinusoid fits, Levenberg-Marquardt) on the
// engine signal and on every reference signal, started from the SPEC frequencies n f0 sqrt(1 + B
// n^2); the first 10 partials (or all below 5 kHz if fewer) are compared.
// Pass: |1200 log2(f_engine / f_ref)| <= 0.05 cent and |sigma_engine / sigma_ref - 1| <= 0.01.
//
// The reference test case exists only when CMake finds the target pme_reference_cli and the build
// is optimised (PME_A2T3_REFERENCE_CLI defined, see CMakeLists.txt). The engine-side case below
// always exists: it checks the engine signal against the SPEC formulas so that a reference
// disagreement can be attributed.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <pme/analysis/partials.hpp>
#include <pme/analysis/wav.hpp>
#include <pme/osc/mode_bank.hpp>
#include <pme/string.hpp>
#include <pme/test/scratch.hpp>
#include <pme/test/tolerance.hpp>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "string_oracle_common.hpp"

namespace {

namespace str = pme::oracle::str;
using str::kPi;

constexpr double kFs = 48000.0;
constexpr double kDuration = 2.0;
constexpr double kObsFraction = 0.37;
constexpr double kPluckFraction = 0.12;
constexpr double kPluckHeight = 1.0e-3;
constexpr double kFreqTolCents = 0.05;
constexpr double kDecayTol = 0.01;
constexpr int kMaxPartials = 10;
constexpr double kMaxPartialHz = 5000.0;

struct T3String {
    std::string name;
    double dCore;
};

std::vector<T3String> t3Strings() { return {{"B about 4e-4", 1.08e-3}, {"B about 1e-2", 5.4e-3}}; }

str::StringSpec t3Spec(const T3String& s) {
    str::StringSpec spec;
    spec.name = s.name;
    spec.L = 0.64;
    spec.dCore = s.dCore;
    spec.dOuter = s.dCore;
    spec.f1 = 261.6255653005986;  // C4, key 60
    spec.b1 = 0.5;
    spec.b2 = 0.0;
    spec.b3 = 2.0e-9;
    return spec;
}

/// Triangular displacement of peak h at xp on [0, L]: the integral of y0 phi_n, float64, per
/// linear segment with Gauss-Legendre (exact for line x sine up to round-off at these n).
double pluckProjection(double L, double xp, double h, int n) {
    static const str::GaussLegendre rule = str::gaussLegendre(64);
    const auto left = [&](double x) { return h * (x / xp) * str::modeShape(L, n, x); };
    const auto right = [&](double x) { return h * ((L - x) / (L - xp)) * str::modeShape(L, n, x); };
    return str::integrate(left, 0.0, xp, rule) + str::integrate(right, xp, L, rule);
}

/// Runs the engine side and returns y(x_obs) for kDuration seconds at kFs.
std::vector<double> engineSignal(const str::StringSpec& spec, pme::StringModes& modes) {
    const double xObs = kObsFraction * spec.L;
    const std::array<double, 1> points{xObs};
    const pme::Status st = pme::compileString(str::toParams(spec, spec.f1), kFs, str::kInf,
                                              std::span<const double>(points), modes);
    REQUIRE(pme::oracle::str::is(st, pme::Status::Ok));
    const std::size_t N = modes.omega.size();
    REQUIRE(N >= 10);
    REQUIRE(modes.phiBar.size() == 1);
    REQUIRE(modes.phiBar[0].size() == N);

    pme::osc::ModeBank bank;
    bank.resize(N);
    const double dt = 1.0 / kFs;
    for (std::size_t i = 0; i < N; ++i) {
        bank.setMode(i, modes.oscV[i]);
        const int n = static_cast<int>(i) + 1;
        const double q0 = pluckProjection(spec.L, kPluckFraction * spec.L, kPluckHeight, n);
        const double sigma = modes.sigma[i];
        const double wd = std::sqrt((modes.omega[i] - sigma) * (modes.omega[i] + sigma));
        // Zero initial velocity: q(t) = q0 e^(-sigma t) (cos wd t + (sigma / wd) sin wd t).
        const double qPrev =
            q0 * std::exp(sigma * dt) * (std::cos(wd * dt) - (sigma / wd) * std::sin(wd * dt));
        bank.q()[i] = static_cast<float>(q0);
        bank.u()[i] = static_cast<float>(q0 - qPrev);
    }
    const auto steps = static_cast<std::size_t>(std::llround(kDuration * kFs));
    std::vector<float> force(N, 0.0F);
    std::vector<double> y(steps, 0.0);
    for (std::size_t t = 0; t < steps; ++t) {
        double sum = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            sum += static_cast<double>(modes.phiBar[0][i]) * static_cast<double>(bank.q()[i]);
        }
        y[t] = sum;
        bank.step(std::span<const float>(force));
    }
    return y;
}

/// SPEC partial frequencies n f0 sqrt(1 + B n^2) below kMaxPartialHz, at most kMaxPartials.
std::vector<double> specPartials(const str::StringSpec& spec) {
    const double T = str::tensionForPitch(spec, spec.f1);
    const double B = str::inharmonicityOf(spec, T);
    const double f0 = str::f0Of(spec, T);
    std::vector<double> f;
    for (int n = 1; n <= kMaxPartials; ++n) {
        const double fn = str::stiffPartial(f0, B, n);
        if (fn >= kMaxPartialHz) {
            break;
        }
        f.push_back(fn);
    }
    return f;
}

pme::analysis::PartialTrackingResult trackSignal(const std::vector<double>& y, double fs,
                                                 const std::vector<double>& approx) {
    pme::analysis::PartialTrackingOptions opt;
    opt.approx_frequencies_hz = approx;
    opt.envelope_frame_s = 0.0;
    return pme::analysis::track_partials(std::span<const double>(y), fs, opt);
}

}  // namespace

TEST_CASE("A2-T3 engine side rings at the SPEC partials and decays at b1 + b3 w^2") {
    // Cross-check of the engine half of A2-T3 against the SPEC formulas alone (no reference): the
    // fitted partial frequency is the damped frequency of the SPEC mode and sigma = b1 + b3
    // omega^2.
    for (const T3String& s : t3Strings()) {
        const str::StringSpec spec = t3Spec(s);
        pme::StringModes modes;
        const std::vector<double> y = engineSignal(spec, modes);
        const std::vector<double> approx = specPartials(spec);
        REQUIRE(approx.size() >= 5);
        const auto fit = trackSignal(y, kFs, approx);
        REQUIRE(fit.partials.size() == approx.size());
        const double T = str::tensionForPitch(spec, spec.f1);
        const double B = str::inharmonicityOf(spec, T);
        const double f0 = str::f0Of(spec, T);
        for (std::size_t i = 0; i < approx.size(); ++i) {
            const int n = static_cast<int>(i) + 1;
            const double w = 2.0 * kPi * str::stiffPartial(f0, B, n);
            const double sigma = spec.b1 + spec.b3 * w * w;  // SPEC-STR "Losses", b2 = 0
            const double wd = std::sqrt((w - sigma) * (w + sigma));
            const double fExpected = wd / (2.0 * kPi);
            const double cents = pme::test::cents(fit.partials[i].frequency_hz, fExpected);
            const double decayErr = fit.partials[i].sigma_per_s / sigma - 1.0;
            INFO(s.name << ": partial " << n << " f_fit = " << fit.partials[i].frequency_hz
                        << " f_SPEC = " << fExpected << " cents = " << cents << " sigma_fit = "
                        << fit.partials[i].sigma_per_s << " sigma_SPEC = " << sigma);
            CHECK(std::fabs(cents) <= kFreqTolCents);
            CHECK(std::fabs(decayErr) <= kDecayTol);
            str::report("A2-T3 engine side " + s.name + " partial " + std::to_string(n) + ": " +
                        str::sci(cents) + " cent, decay " + str::sci(decayErr));
        }
    }
}

#ifdef PME_A2T3_REFERENCE_CLI

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

/// Writes a P0.5 scenario (schema_version 1, reference/scenario.schema.json) for the A2-T3 string:
/// the StringParams fields, a triangular initial displacement, an observation point, fs,
/// oversampling, grid intervals and duration, rigid (fixed) bridge end, pinned ends.
/// This is the single place that knows the scenario field names; if the merged
/// reference/scenario.schema.json names a field differently, change it here and nowhere else.
struct RefGrid {
    int intervals;
    int oversampling;
};

void writeScenario(const fs::path& path, const str::StringSpec& s, const RefGrid& grid) {
    std::ostringstream j;
    j << std::setprecision(17);
    j << R"({
  "schema_version": 1,
  "name": "A2-T3 )"
      << s.name << R"(",
  "fs": )"
      << kFs << R"(,
  "oversampling": )"
      << grid.oversampling << R"(,
  "duration": )"
      << kDuration << R"(,
  "strings": [{
    "params": {
      "L": )"
      << s.L << R"(, "dCore": )" << s.dCore << R"(, "rhoCore": )" << s.rhoCore << R"(, "ECore": )"
      << s.ECore << R"(,
      "dOuter": )"
      << s.dOuter << R"(, "rhoWind": 0, "packing": 0, "epsWind": 0,
      "f1Target": )"
      << s.f1 << R"(, "b1": )" << s.b1 << R"(, "b2": 0, "b3": )" << s.b3 << R"(,
      "boundary": "pinned", "detuneH": 0, "bridgeEnd": "at_L"
    },
    "grid": {"intervals": )"
      << grid.intervals << R"(},
    "initial_displacement": {"amplitude": )"
      << kPluckHeight << R"(, "position": )" << kPluckFraction * s.L << R"(}
  }],
  "observations": [{"name": "y", "kind": "string_displacement", "string": 0, "x": )"
      << kObsFraction * s.L << R"(, "polarisation": "v"}]
}
)";
    std::ofstream out(path);
    out << j.str();
}

std::string quote(const std::string& text) { return '"' + text + '"'; }

/// Runs the CLI and returns the observation signal (channel 0) and its sample rate.
std::vector<double> renderReference(const fs::path& dir, const str::StringSpec& s, int level,
                                    const RefGrid& grid, double& sampleRate) {
    const std::string tag = "t3_" + std::to_string(level);
    const fs::path scenario = dir / (tag + ".json");
    const fs::path features = dir / (tag + "_features.json");
    const fs::path wav = dir / (tag + ".wav");
    writeScenario(scenario, s, grid);
    std::string cmd = quote(PME_A2T3_REFERENCE_CLI) + " render " + quote(scenario.string()) +
                      " --features " + quote(features.string()) + " --wav " + quote(wav.string());
#ifdef _WIN32
    cmd = '"' + cmd + '"';  // cmd.exe /c strips one pair of outer quotes
#endif
    INFO("reference command: " << cmd);
    const int rc = std::system(cmd.c_str());  // NOLINT(concurrency-mt-unsafe,cert-env33-c)
    REQUIRE(rc == 0);
    const pme::analysis::WavData data = pme::analysis::read_wav(wav);
    sampleRate = data.sample_rate_hz;
    std::vector<double> y;
    for (std::size_t i = 0; i < data.frames(); ++i) {
        y.push_back(data.samples[i * data.channels]);
    }
    return y;
}

}  // namespace

TEST_CASE("A2-T3 stiff string compiler vs P0.5 reference solver") {
    const int n0 = envInt("PME_A2T3_N0", 400);
    const int os0 = envInt("PME_A2T3_OS0", 4);
    for (const T3String& s : t3Strings()) {
        const str::StringSpec spec = t3Spec(s);
        const std::vector<double> approx = specPartials(spec);
        REQUIRE(approx.size() >= 5);

        pme::StringModes modes;
        const std::vector<double> yEngine = engineSignal(spec, modes);
        const auto engine = trackSignal(yEngine, kFs, approx);
        REQUIRE(engine.partials.size() == approx.size());
        const fs::path dir = pme::test::unique_scratch_dir("pme_a2t3");
        fs::create_directories(dir);
        // Three refinement levels of the reference (grid and time step halve per level).
        std::array<std::vector<double>, 3> refSignal;
        for (int level = 0; level < 3; ++level) {
            double sr = 0.0;
            const auto idx = static_cast<std::size_t>(level);
            refSignal[idx] =
                renderReference(dir, spec, level, RefGrid{n0 << level, os0 << level}, sr);
            REQUIRE(sr == doctest::Approx(kFs));
        }
        // The joint spectral fit converges only from a start within about one spectral bin of each
        // peak, and the coarsest levels are off by more than that at the upper partials. The fits
        // therefore start from the engine's fitted frequencies at the finest level and from the
        // second-order prediction f_engine + 4^(2 - l) (f_2 - f_engine) at level l. Start values
        // only: every compared frequency and decay rate is the converged fit of the reference
        // signal itself.
        std::array<pme::analysis::PartialTrackingResult, 3> ref;
        std::vector<double> engineHz;
        engineHz.reserve(engine.partials.size());
        for (const auto& p : engine.partials) {
            engineHz.push_back(p.frequency_hz);
        }
        ref[2] = trackSignal(refSignal[2], kFs, engineHz);
        REQUIRE(ref[2].partials.size() == approx.size());
        for (int level = 1; level >= 0; --level) {
            const double scale = std::pow(4.0, 2 - level);
            std::vector<double> start;
            start.reserve(approx.size());
            for (std::size_t i = 0; i < approx.size(); ++i) {
                start.push_back(engineHz[i] +
                                scale * (ref[2].partials[i].frequency_hz - engineHz[i]));
            }
            const auto idx = static_cast<std::size_t>(level);
            ref[idx] = trackSignal(refSignal[idx], kFs, start);
            REQUIRE(ref[idx].partials.size() == approx.size());
        }
        std::error_code ec;
        fs::remove_all(dir, ec);

        double worstCents = 0.0;
        double worstDecay = 0.0;
        double worstRefErr = 0.0;
        for (std::size_t i = 0; i < approx.size(); ++i) {
            const double f0v = ref[0].partials[i].frequency_hz;
            const double f1v = ref[1].partials[i].frequency_hz;
            const double f2v = ref[2].partials[i].frequency_hz;
            const double s0 = ref[0].partials[i].sigma_per_s;
            const double s1 = ref[1].partials[i].sigma_per_s;
            const double s2 = ref[2].partials[i].sigma_per_s;
            // Richardson extrapolation with the scheme's second order, from the two finest and the
            // two coarsest levels.
            const double fRef = f2v + (f2v - f1v) / 3.0;
            const double fRefCoarse = f1v + (f1v - f0v) / 3.0;
            const double sRef = s2 + (s2 - s1) / 3.0;
            const double sRefCoarse = s1 + (s1 - s0) / 3.0;
            const double refErrCents = std::fabs(pme::test::cents(fRef, fRefCoarse));
            const double cents = pme::test::cents(engine.partials[i].frequency_hz, fRef);
            const double decay = engine.partials[i].sigma_per_s / sRef - 1.0;
            INFO(s.name << ": partial " << i + 1
                        << " f_engine = " << engine.partials[i].frequency_hz << " f_ref = " << fRef
                        << " (levels " << f0v << ", " << f1v << ", " << f2v << ") sigma_engine = "
                        << engine.partials[i].sigma_per_s << " sigma_ref = " << sRef
                        << " (coarse-pair extrapolation " << sRefCoarse << ")");
            CHECK(std::fabs(cents) <= kFreqTolCents);
            CHECK(std::fabs(decay) <= kDecayTol);
            CHECK(refErrCents <= kRefErrCents);
            worstCents = std::max(worstCents, std::fabs(cents));
            worstDecay = std::max(worstDecay, std::fabs(decay));
            worstRefErr = std::max(worstRefErr, refErrCents);
            str::report("A2-T3 " + s.name + " partial " + std::to_string(i + 1) + ": " +
                        str::sci(cents) + " cent, decay " + str::sci(decay) +
                        ", reference error estimate " + str::sci(refErrCents) + " cent");
        }
        str::report("A2-T3 " + s.name + ": worst " + str::sci(worstCents) + " cent, worst decay " +
                    str::sci(worstDecay) + ", worst reference estimate " + str::sci(worstRefErr) +
                    " cent");
    }
}

#endif  // PME_A2T3_REFERENCE_CLI
