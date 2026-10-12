// SPDX-License-Identifier: MIT
// Covers: A7-T3
//
// A7-T3 Longitudinal precursor against the P0.5 reference solver (phase card A7; PLAN-VAL).
// "Against P0.5, the longitudinal precursor reaches the bridge at the predicted time within 1%."
//
// Physics. A string released at rest from a triangle with its kink at x_p has y_x^2 piecewise
// constant; the longitudinal forcing (EA - T)/2 d/dx (y_x^2) of SPEC-GEO is concentrated at the
// kinks (two of them once the release has split into travelling halves, moving at +-c_T). A
// longitudinal strain front leaves x_p at t = 0 and reaches the bridge end x = L at
//     t_pred = (L - x_p) / c_L,   c_L = sqrt(EA / rhoA)   (core EA, full rhoA),
// long before the transverse kink (at (L - x_p) / c_T, c_T = sqrt(T / rhoA), 14 times later). Until
// then (EA - T)/2 y_x(L)^2 is constant, so the change of F_L(t) - F_L(0) is the longitudinal
// precursor. Because x = L is a fixed end the strain doubles there, and the left-going half of the
// forcing, reflected at x = 0, arrives at t = (L + x_p) / c_L, so the precursor is a rectangle of
// width 2 x_p / c_L (the "plateau" below) rather than a step.
//
// Setup (derived in the test, nothing taken from the engine). Wound bass string, L = 1.9 m, core
// steel d_c = 1.2 mm, outer d_o = 2.9 mm copper winding (rho_w = 8960, eta_w = 0.85, eps_w = 0), so
// rhoA = 0.0506 kg/m, EA = 2.26e5 N, c_L = 2115 m/s; f1 = 40 Hz (T = 1.2e3 N, c_T = 154 m/s);
// b1 = 0.1 1/s, b2 = b3 = 0; triangle of peak 5 mm at x_p = 0.12 L, at rest; tension modulation
// and longitudinal modes on, longitudinal losses 0; fixed ends, rigid bridge (G = 0: no bridge
// motion; the reference has no bridge in a geometric scenario). t_pred is 76 samples at 96 kHz.
// The relative tension change Delta T / T is 0.6%: the default tier of SPEC-GEO applies.
//
// Engine: public compileString at 96 kHz (f_max = infinity), the rig of geometric_rig.hpp with
// M_L = 16 (the pair-sum maximum; the number kept by the bandwidth rule is reported and used).
// The transverse state is the exact modal projection of the triangle (closed form), the
// longitudinal modes start at rest (time-symmetric leapfrog start, GeoRig::startLongitudinalAtRest;
// without it the explicit scheme begins dt / 2 early, an artefact of the initial condition).
//
// Reference: `pme_reference_cli render` with scenario geometric = {tension_modulation,
// longitudinal}, observation longitudinal_bridge_force (F_L = EA xi_x(L) + (EA - T)/2 y_x(L)^2 with
// one-sided second-order slopes). The scheme is second order in h and k, so a refinement ladder of
// three levels l = 0, 1, 2 with N0 2^l intervals and sample rate R0 2^l (oversampling 1: the CLI's
// WAV writer decimates with a low-pass only when oversampling > 1, which would smear the front;
// the Courant number c_L k / h stays constant) gives the arrival times t_l, the observed order
// log2((t_0 - t_1) / (t_1 - t_2)) and the extrapolated t* = t_2 + (t_2 - t_1) / 3. PME_A7T3_N0
// (default 380) and PME_A7T3_FS0 (default 192000 Hz) set the coarsest level.
//
// Estimator (identical on both signals). x(t) = F_L(t) - F_L(0); a zero-phase Blackman-windowed
// sinc low-pass whose cutoff lies between the engine's highest kept longitudinal mode and the next
// one (so both signals have the same bandwidth); the plateau height H is the mean over
// [t_pred + 0.3 w, t_pred + 0.7 w], w = 2 x_p / c_L; the arrival is the first crossing of H / 2,
// linearly interpolated. A band-limited step crosses half its height at the true arrival time, so
// the estimator is unbiased. Ringing (max |y - H| / |H| over the plateau window) and the level of
// the acausal precursor of the filter before t_pred / 2 are reported.
//
// Pass (card): |t_engine / t_pred - 1| <= 0.01 and |t_ref / t_pred - 1| <= 0.01 with t_ref = t*;
// the reference's own error |t* - t_2| / t_pred must be below 1e-3 (brief: a grid fine enough that
// its arrival-time error is below 0.1% of the predicted time). |t_engine / t_ref - 1| and the
// energies (SI-0015) are reported.
//
// The reference case exists only when CMake finds the target pme_reference_cli and the build is
// optimised (PME_A7T3_REFERENCE_CLI defined, see CMakeLists.txt). The engine-side case always
// exists.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <pme/analysis/wav.hpp>
#include <pme/test/tolerance.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "geometric_oracle_common.hpp"
#include "geometric_rig.hpp"

namespace {

namespace geo = pme::oracle::geo;
namespace str = pme::oracle::str;
using geo::kPi;

constexpr double kEngineFs = 96000.0;
constexpr double kDuration = 3.0e-3;       // s
constexpr double kPluckFraction = 0.12;    // x_p / L
constexpr double kPluckHeight = 5.0e-3;    // m
constexpr double kHalfSeconds = 6.0e-3;    // half-length of the low-pass kernel (s)
constexpr double kArrivalTol = 0.01;       // card A7-T3
[[maybe_unused]] constexpr double kRefErrorTol = 1.0e-3;  // brief: reference's own error / t_pred
constexpr int kRequestedLongitudinal = 16;

struct T3String {
    str::StringSpec spec;
    pme::StringParams params;
    double ea{};
    double tension{};
    double rhoA{};
    double cL{};
    double xp{};
    double tPred{};
    double plateau{};
};

T3String buildString() {
    T3String t;
    t.spec.name = "wound bass";
    t.spec.L = 1.9;
    t.spec.dCore = 1.2e-3;
    t.spec.dOuter = 2.9e-3;
    t.spec.rhoCore = 7850.0;
    t.spec.ECore = 2.0e11;
    t.spec.rhoWind = 8960.0;
    t.spec.packing = 0.85;
    t.spec.epsWind = 0.0;
    t.spec.f1 = 40.0;
    t.spec.b1 = 0.1;
    t.spec.b2 = 0.0;
    t.spec.b3 = 0.0;
    t.params = str::toParams(t.spec, t.spec.f1);
    t.ea = geo::coreEA(t.spec);
    t.rhoA = str::linearDensity(t.spec);
    t.tension = str::tensionForPitch(t.spec, t.spec.f1);
    t.cL = std::sqrt(t.ea / t.rhoA);
    t.xp = kPluckFraction * t.spec.L;
    t.tPred = (t.spec.L - t.xp) / t.cL;
    t.plateau = 2.0 * t.xp / t.cL;
    return t;
}

struct EngineResult {
    std::vector<double> signal;  ///< F_L(n dt) - F_L(0), N, n = 0..steps
    int kept{};                  ///< longitudinal modes kept by the bandwidth rule
    double cutoffHz{};           ///< between the last kept mode and the next
    double lastKeptHz{};
    double transverse0{};        ///< transverse energy at t = 0 (J)
    double transverseDrift{};    ///< max |E_T(t) - E_T(0)| / E_T(0)
    double longitudinalEnd{};    ///< E_L at the end (J)
    double transverseEnd{};      ///< E_T at the end (J)
};

EngineResult runEngine(const T3String& t) {
    EngineResult r;
    pme::StringModes modes;
    REQUIRE(str::is(pme::compileString(t.params, kEngineFs, str::kInf, {}, modes),
                    pme::Status::Ok));
    CHECK(pme::test::relative_error(modes.tension, t.tension) <= 1.0e-9);
    geo::GeoSetup setup;
    setup.modes = modes;
    setup.params = t.params;
    setup.longitudinalModes = kRequestedLongitudinal;
    setup.fs = kEngineFs;
    setup.fMax = str::kInf;
    geo::GeoRig rig(setup);
    REQUIRE(rig.ok());
    r.kept = rig.longitudinalKept();
    REQUIRE(r.kept >= 1);
    const double dt = 1.0 / kEngineFs;
    for (std::size_t i = 0; i < modes.omega.size(); ++i) {
        const double q0 = geo::trianglePluckMode(t.spec.L, t.xp, kPluckHeight,
                                                 static_cast<int>(i) + 1);
        const double sigma = modes.sigma[i];
        const double wd = std::sqrt((modes.omega[i] - sigma) * (modes.omega[i] + sigma));
        const double back =
            std::exp(sigma * dt) * (std::cos(wd * dt) - (sigma / wd) * std::sin(wd * dt));
        rig.qV()[i] = static_cast<float>(q0);
        rig.uV()[i] = static_cast<float>(q0 - q0 * back);
    }
    rig.startLongitudinalAtRest();
    const double c1 = t.cL / (2.0 * t.spec.L);  // f_m = m c_L / 2L
    r.lastKeptHz = r.kept * c1;
    r.cutoffHz = (r.kept + 0.5) * c1;
    const auto steps = static_cast<std::size_t>(std::llround(kDuration * kEngineFs));
    r.signal.assign(steps + 1, 0.0);
    const double f0 = rig.bridgeForceL();
    r.transverse0 = rig.transverseEnergy();
    for (std::size_t n = 0; n <= steps; ++n) {
        r.signal[n] = rig.bridgeForceL() - f0;
        r.transverseDrift = std::max(
            r.transverseDrift, std::fabs(rig.transverseEnergy() - r.transverse0) / r.transverse0);
        if (n < steps) {
            rig.step();
        }
    }
    r.longitudinalEnd = rig.longitudinalEnergy();
    r.transverseEnd = rig.transverseEnergy();
    return r;
}

std::string describe(const geo::StepEstimate& e) {
    return "arrival " + str::sci(e.arrival) + " s, H = " + str::sci(e.height) +
           " N, ringing " + str::sci(e.ringing) + ", pre-front level " + str::sci(e.preLevel);
}

}  // namespace

TEST_CASE("A7-T3 setup meets the card: travel time spans 60 samples and the default tier applies") {
    const T3String t = buildString();
    str::report("A7-T3 string: rhoA = " + str::sci(t.rhoA) + " kg/m, EA = " + str::sci(t.ea) +
                " N, T = " + str::sci(t.tension) + " N, c_L = " + str::sci(t.cL) +
                " m/s, c_T = " + str::sci(std::sqrt(t.tension / t.rhoA)) +
                " m/s, t_pred = " + str::sci(t.tPred) + " s (" +
                str::sci(t.tPred * kEngineFs) + " samples at 96 kHz), plateau " +
                str::sci(t.plateau * kEngineFs) + " samples");
    CHECK(t.rhoA > 0.04);
    CHECK(t.rhoA < 0.06);
    CHECK(t.cL > 2000.0);
    CHECK(t.cL < 2200.0);
    CHECK(t.tPred * kEngineFs >= 60.0);
    CHECK(t.ea > t.tension);
    // The transverse kink needs far longer than the observation window.
    CHECK((t.spec.L - t.xp) / std::sqrt(t.tension / t.rhoA) > 3.0 * kDuration);
    // Default tier: the relative tension change of the initial triangle is small. S of the triangle:
    // slopes h / x_p and h / (L - x_p).
    const double a = kPluckHeight / t.xp;
    const double b = kPluckHeight / (t.spec.L - t.xp);
    const double s = a * a * t.xp + b * b * (t.spec.L - t.xp);
    const double dT = (t.ea - t.tension) * s / (2.0 * t.spec.L);
    str::report("A7-T3 Delta T / T = " + str::sci(dT / t.tension));
    CHECK(dT / t.tension < 0.01);
}

TEST_CASE("A7-T3 engine longitudinal precursor reaches the bridge at the predicted time") {
    const T3String t = buildString();
    const EngineResult e = runEngine(t);
    const std::vector<double> y = geo::lowPassZeroPhase(e.signal, kEngineFs, e.cutoffHz,
                                                        kHalfSeconds);
    const geo::StepEstimate est = geo::estimateArrival(y, kEngineFs, t.tPred, t.plateau);
    REQUIRE(est.arrival > 0.0);
    const double rel = est.arrival / t.tPred - 1.0;
    str::report("A7-T3 engine: kept " + std::to_string(e.kept) + " of " +
                std::to_string(kRequestedLongitudinal) + " longitudinal modes (last kept " +
                str::sci(e.lastKeptHz) + " Hz, low-pass cutoff " + str::sci(e.cutoffHz) +
                " Hz); t_pred " + str::sci(t.tPred) + " s, t_engine/t_pred - 1 = " + str::sci(rel) +
                "; " + describe(est));
    str::report("A7-T3 energy (SI-0015) engine: E_T(0) = " + str::sci(e.transverse0) +
                " J, max |E_T - E_T(0)|/E_T(0) = " + str::sci(e.transverseDrift) +
                ", E_L(end) = " + str::sci(e.longitudinalEnd) + " J, E_L/E_T = " +
                str::sci(e.longitudinalEnd / e.transverseEnd));
    CHECK(std::fabs(rel) <= kArrivalTol);
    // The estimator must see a clear precursor, not noise: the plateau is far above the
    // acausal pre-front ripple of the filter.
    CHECK(std::fabs(est.height) > 0.0);
    CHECK(est.preLevel < 0.25);
}

#ifdef PME_A7T3_REFERENCE_CLI

namespace {

namespace fs = std::filesystem;

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
    int sampleRate;  ///< simulation rate (oversampling 1)
};

/// Writes the P0.5 scenario (schema_version 1, reference/scenario.schema.json): a single fixed-end
/// string with an initial displacement and geometric = {tension_modulation, longitudinal}. This is
/// the single place that knows the scenario field names.
void writeScenario(const fs::path& path, const T3String& t, const RefGrid& grid) {
    const auto& sp = t.spec;
    std::ostringstream j;
    j << std::setprecision(17);
    j << "{\n  \"schema_version\": 1,\n  \"name\": \"A7-T3 longitudinal precursor\",\n";
    j << "  \"fs\": " << grid.sampleRate << ",\n  \"oversampling\": 1,\n";
    j << "  \"duration\": " << kDuration << ",\n  \"strings\": [\n";
    j << R"(    {"params": {"L": )" << sp.L << ", \"dCore\": " << sp.dCore
      << ", \"rhoCore\": " << sp.rhoCore << ", \"ECore\": " << sp.ECore
      << ", \"dOuter\": " << sp.dOuter << ", \"rhoWind\": " << sp.rhoWind
      << ", \"packing\": " << sp.packing << ", \"epsWind\": " << sp.epsWind
      << ", \"f1Target\": " << sp.f1 << ", \"b1\": " << sp.b1 << R"(, "b2": 0, "b3": 0)"
      << R"(, "boundary": "pinned"},)" << "\n"
      << R"(     "grid": {"intervals": )" << grid.intervals << "},\n"
      << R"(     "initial_displacement": {"amplitude": )" << kPluckHeight
      << ", \"position\": " << t.xp << R"(, "angle": 0},)" << "\n"
      << R"(     "geometric": {"tension_modulation": true, "longitudinal": true, )"
      << R"("longitudinal_b1": 0, "longitudinal_b3": 0}})" << "\n  ],\n";
    j << R"(  "observations": [{"name": "fl", "kind": "longitudinal_bridge_force", "string": 0}])"
      << "\n}\n";
    std::ofstream out(path);
    out << j.str();
}

std::string quote(const std::string& text) { return '"' + text + '"'; }

struct RefLevel {
    std::vector<double> signal;  ///< F_L(t) - F_L(0)
    double fs{};
    double energyInitial{};
    double energyDrift{};
};

/// Reads the number following "key": in a JSON text (no parser needed for the energy summary).
double jsonNumber(const std::string& text, const std::string& key) {
    const std::size_t at = text.find('"' + key + '"');
    if (at == std::string::npos) {
        return -1.0;
    }
    const std::size_t colon = text.find(':', at);
    return std::strtod(text.c_str() + colon + 1, nullptr);
}

RefLevel renderReference(const fs::path& dir, const T3String& t, int level, const RefGrid& grid) {
    const std::string tag = "t3_" + std::to_string(level);
    const fs::path scenario = dir / (tag + ".json");
    const fs::path wav = dir / (tag + ".wav");
    const fs::path energy = dir / (tag + "_energy.json");
    writeScenario(scenario, t, grid);
    std::string cmd = quote(PME_A7T3_REFERENCE_CLI) + " render " + quote(scenario.string()) +
                      " --wav " + quote(wav.string()) + " --energy " + quote(energy.string());
#ifdef _WIN32
    cmd = '"' + cmd + '"';  // cmd.exe /c strips one pair of outer quotes
#endif
    INFO("reference command: " << cmd);
    const int rc = std::system(cmd.c_str());  // NOLINT(concurrency-mt-unsafe,cert-env33-c)
    REQUIRE(rc == 0);
    const pme::analysis::WavData data = pme::analysis::read_wav(wav);
    REQUIRE(data.sample_rate_hz == doctest::Approx(static_cast<double>(grid.sampleRate)));
    REQUIRE(data.channels == 1);
    RefLevel r;
    r.fs = data.sample_rate_hz;
    r.signal = data.samples;
    REQUIRE(!r.signal.empty());
    const double f0 = r.signal.front();
    for (double& v : r.signal) {
        v -= f0;
    }
    std::ifstream in(energy);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    r.energyInitial = jsonNumber(text, "initial");
    r.energyDrift = jsonNumber(text, "max_relative_drift");
    return r;
}

}  // namespace

TEST_CASE("A7-T3 longitudinal precursor of engine and P0.5 reference reach the bridge at t_pred") {
    const T3String t = buildString();
    const EngineResult e = runEngine(t);
    const geo::StepEstimate engineEst = geo::estimateArrival(
        geo::lowPassZeroPhase(e.signal, kEngineFs, e.cutoffHz, kHalfSeconds), kEngineFs, t.tPred,
        t.plateau);
    REQUIRE(engineEst.arrival > 0.0);

    const int n0 = envInt("PME_A7T3_N0", 380);
    const int fs0 = envInt("PME_A7T3_FS0", 192000);
    const fs::path dir = fs::temp_directory_path() / ("pme_a7t3_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    std::array<geo::StepEstimate, 3> est{};
    std::array<RefLevel, 3> levels;
    for (int level = 0; level < 3; ++level) {
        const RefGrid grid{n0 << level, fs0 << level};
        levels[static_cast<std::size_t>(level)] = renderReference(dir, t, level, grid);
        const RefLevel& rl = levels[static_cast<std::size_t>(level)];
        const double cutoff = e.cutoffHz;  // the engine's bandwidth, on every level
        est[static_cast<std::size_t>(level)] = geo::estimateArrival(
            geo::lowPassZeroPhase(rl.signal, rl.fs, cutoff, kHalfSeconds), rl.fs, t.tPred,
            t.plateau);
        REQUIRE(est[static_cast<std::size_t>(level)].arrival > 0.0);
        str::report("A7-T3 reference level " + std::to_string(level) + " (N = " +
                    std::to_string(grid.intervals) + ", " + std::to_string(grid.sampleRate) +
                    " Hz): " + describe(est[static_cast<std::size_t>(level)]) +
                    ", t/t_pred - 1 = " +
                    str::sci(est[static_cast<std::size_t>(level)].arrival / t.tPred - 1.0));
    }
    std::error_code ec;
    fs::remove_all(dir, ec);

    const double t0 = est[0].arrival;
    const double t1 = est[1].arrival;
    const double t2 = est[2].arrival;
    const double order = ((t1 - t2) != 0.0 && (t0 - t1) / (t1 - t2) > 0.0)
                             ? std::log2((t0 - t1) / (t1 - t2))
                             : 0.0;
    const double tStar = t2 + (t2 - t1) / 3.0;  // second-order Richardson extrapolation
    const double refOwnError = std::fabs(tStar - t2) / t.tPred;
    const double relEngine = engineEst.arrival / t.tPred - 1.0;
    const double relRef = tStar / t.tPred - 1.0;
    const double relBoth = engineEst.arrival / tStar - 1.0;
    str::report("A7-T3 summary: t_pred " + str::sci(t.tPred) + " s, t_engine " +
                str::sci(engineEst.arrival) + " s (" + std::to_string(e.kept) + " longitudinal modes kept, cutoff " +
                str::sci(e.cutoffHz) + " Hz), t_ref (extrapolated) " + str::sci(tStar) +
                " s; engine/t_pred - 1 = " + str::sci(relEngine) + ", ref/t_pred - 1 = " +
                str::sci(relRef) + ", engine/ref - 1 = " + str::sci(relBoth) +
                "; reference observed order " + str::sci(order) + " on N = " +
                std::to_string(n0) + "..." + std::to_string(n0 << 2) + ", reference own error " +
                str::sci(refOwnError) + " of t_pred; engine ringing " +
                str::sci(engineEst.ringing) + ", reference ringing " + str::sci(est[2].ringing));
    str::report("A7-T3 plateau height: engine " + str::sci(engineEst.height) + " N, reference " +
                str::sci(est[2].height) + " N");
    str::report("A7-T3 energy (SI-0015): reference E(0) = " + str::sci(levels[2].energyInitial) +
                " J (transverse plus V_KC), max relative drift " +
                str::sci(levels[2].energyDrift) + "; engine E_T(0) = " + str::sci(e.transverse0) +
                " J (relative difference " +
                str::sci(e.transverse0 / levels[2].energyInitial - 1.0) +
                "), engine max |dE_T|/E_T = " + str::sci(e.transverseDrift) +
                ", E_L/E_T at the end " + str::sci(e.longitudinalEnd / e.transverseEnd));
    CHECK(std::fabs(relRef) <= kArrivalTol);
    CHECK(refOwnError <= kRefErrorTol);
    CHECK(std::fabs(relEngine) <= kArrivalTol);
}

#endif  // PME_A7T3_REFERENCE_CLI
