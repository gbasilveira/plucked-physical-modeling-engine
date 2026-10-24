// SPDX-License-Identifier: MIT
// Covers: A5-T2
//
// A5-T2 Hammer against the P0.5 reference solver with elastic felt (phase card A5; PLAN-VAL):
// "against P0.5 with elastic felt, peak force and contact duration within 2%".
//
// System. One plain steel string, pinned, fixed ends (no bridge), L = 0.65 m, f1 = 262 Hz, core
// diameter chosen from the SPEC-STR formulas for B = 2e-4 (printed with the achieved B), losses
// b1 = 0.5 1/s, b2 = 0, b3 = 2e-9 s. Hammer: m_H = 8 g, K = 4.5e9 N/m^p, p = 2.5, mu = 0 (elastic
// felt: the reference has no Stulov law and no una corda weights), strike at x_c = L/8. Three
// hammer speeds 1, 3 and 5 m/s, two windows: point contact (width 0 in the reference, the point
// shape phi_n(x_c) in the engine) and the raised cosine b_H = 10 mm.
//
// Window convention (checked before relying on it). The reference (reference/src/simulation.cpp,
// build_contacts) weights the interior nodes within |x - x_c| < b/2 with (1 + cos(2 pi d / b)) / b
// h and normalises the weights to sum 1; that is the unit-area raised cosine g(s) = (1 + cos(2 pi s
// / b)) / b of full width b, |s| < b/2, that SPEC-STR "Forces and observations" and A2's
// windowedShapes use (their transform is W(k) = sin(u)/u pi^2/(pi^2 - u^2), u = k b / 2, which the
// A2 oracle ORC-05 verifies against quadrature). The reference switches to a linear interpolation
// at strike_x when b < 2 h, so the grid ladder below keeps h <= b/2 on every level (h = 3.25 mm at
// the coarsest, b/2 = 5 mm). The engine side forms phibar_n by quadrature of the same g.
//
// Reference side. `pme_reference_cli render` renders the scenario on three grids with constant
// ratio 2: intervals N0 2^l and simulation rate fs0 2^l (the time step is refined with the grid;
// oversampling 1, so the WAV channels are the raw simulation-rate signals), l = 0, 1, 2
// (PME_A5T2_N0 = 200 and PME_A5T2_FS0 = 96000 by default; env overrides). Observations:
// hammer_penetration and hammer_force. The scheme is second order in space and time, so the
// contact duration t_c and the peak force F_pk are Richardson-extrapolated, q* = q2 + (q2 - q1)/3,
// with observed order log2((q0 - q1)/(q1 - q2)); the size of the correction relative to q* is the
// reference's own error estimate and is reported.
//
// Estimators (the same on both sides): the contact duration is the span between the interpolated
// entry and exit crossings of the penetration delta = 0 (the force is zero up to the crossing and
// grows as delta^p, so its own zero crossing is ill defined), and the peak force is the vertex of
// the parabola through the maximum force sample and its neighbours.
//
// Engine side. The rig of hammer_rig.hpp (the SAV hammer on the modal string, rigid bridge so the
// ends are fixed), f_max = 20 kHz, float32 at 48 kHz (the engine instantiation, checked) and
// float64 at 96 kHz (reported). Sensitivity of the engine to f_max (16 and 20 kHz) and fs (48 and
// 96 kHz) is reported by an engine-only case that exists in every build.
//
// Pass: |F_pk,engine / F_pk,ref - 1| <= 0.02 and |t_c,engine / t_c,ref - 1| <= 0.02 for every
// speed and window, float32 at 48 kHz. The reference case exists only when CMake finds the target
// pme_reference_cli and the build is optimised (PME_A5T2_REFERENCE_CLI defined, see
// CMakeLists.txt).

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <pme/analysis/wav.hpp>
#include <pme/test/scratch.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"

namespace {

namespace ham = pme::oracle::ham;
namespace str = pme::oracle::str;

constexpr double kF1 = 262.0;
constexpr double kBTarget = 2.0e-4;
constexpr double kB1 = 0.5;
constexpr double kB3 = 2.0e-9;
constexpr double kStrike = 1.0 / 8.0;
constexpr double kWidthRaisedCosine = 0.01;
[[maybe_unused]] constexpr double kEngineFMax = 20000.0;
[[maybe_unused]] constexpr double kEngineFs = 48000.0;
constexpr double kSimSeconds = 0.010;                       // contact (about 1 to 2 ms) and rebound
[[maybe_unused]] constexpr double kPassTol = 0.02;          // card A5-T2
[[maybe_unused]] constexpr double kRefEstimateTol = 0.005;  // the reference's own error estimate
constexpr std::array<double, 3> kSpeeds{1.0, 3.0, 5.0};
constexpr std::array<double, 2> kWidths{0.0, kWidthRaisedCosine};

struct Contact {
    double duration{0.0};
    double peak{0.0};
    int intervals{0};
};

/// The estimators of the header comment on a penetration series and a force series (same sampling).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): penetration, then force
Contact estimate(const std::vector<double>& delta, const std::vector<double>& force, double dt) {
    Contact c;
    const ham::ContactInterval ci = ham::firstContact(delta, dt);
    REQUIRE(ci.found);
    c.duration = ci.duration;
    c.intervals = ci.count;
    const auto lo = static_cast<std::size_t>(std::max(0.0, std::floor(ci.entry) - 1.0));
    const auto hi = std::min(force.size(), static_cast<std::size_t>(std::ceil(ci.exit)) + 3);
    c.peak = ham::parabolicPeak(std::span<const double>(force.data() + lo, hi - lo));
    return c;
}

const char* windowName(double width) { return width > 0.0 ? "raised cosine 10 mm" : "point"; }

/// The oracle string and its compiled table at (fs, fMax).
ham::OracleString buildString(double fs, double fMax) {
    ham::OracleString s;
    REQUIRE(
        ham::makeString(kF1, ham::diameterForInharmonicity(kBTarget, kF1), kB1, kB3, fs, fMax, s));
    return s;
}

template <class RigT>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): fs, f_max, speed, width
Contact engineContact(double fs, double fMax, double speed, double width) {
    const ham::OracleString s = buildString(fs, fMax);
    ham::NoteSetup cfg;
    cfg.strings = {s.modes};
    cfg.hammer.mass = ham::kHammerMass;
    cfg.hammer.K = ham::kFeltK;
    cfg.hammer.p = ham::kFeltP;
    cfg.hammer.muHC = 0.0;
    cfg.hammer.width = width;
    cfg.hammer.strikeRatio = kStrike;
    cfg.una.leavingString = -1;
    cfg.hammerSpeed = speed;
    cfg.fs = fs;
    cfg.driftRelaxation = false;
    RigT rig(cfg);
    REQUIRE(rig.ok());
    const auto steps = static_cast<std::size_t>(kSimSeconds * fs);
    std::vector<double> delta;
    std::vector<double> force;
    delta.reserve(steps);
    force.reserve(steps);
    for (std::size_t n = 0; n < steps; ++n) {
        rig.step();
        delta.push_back(rig.penetration());
        force.push_back(rig.contactForce());
    }
    return estimate(delta, force, 1.0 / fs);
}

}  // namespace

template <class RigF, class RigD>
void sensitivityCase() {
    // Attribution aid for a reference disagreement, not the card's criterion: the engine's own
    // modal-truncation and sample-rate sensitivity of the two estimators. The point contact is the
    // sensitive one (a point load excites every kept mode); the cases are reported, and loosely
    // bounded (5 %) so that a truncation artefact cannot hide behind the 2 % comparison.
    for (const double width : kWidths) {
        for (const double speed : kSpeeds) {
            const Contact base = engineContact<RigF>(48000.0, 20000.0, speed, width);
            const Contact low = engineContact<RigF>(48000.0, 16000.0, speed, width);
            const Contact hi = engineContact<RigD>(96000.0, 20000.0, speed, width);
            const Contact hiLow = engineContact<RigD>(96000.0, 16000.0, speed, width);
            str::report(
                std::string("A5-T2 engine sensitivity, ") + windowName(width) +
                ", v = " + std::to_string(speed) + " m/s: 48 kHz f_max 20 kHz: t_c " +
                str::sci(base.duration) + " s, peak " + str::sci(base.peak) +
                " N; f_max 16 kHz: " + str::sci(low.duration / base.duration - 1.0) + " (t_c), " +
                str::sci(low.peak / base.peak - 1.0) +
                " (peak); 96 kHz f_max 20 kHz: " + str::sci(hi.duration / base.duration - 1.0) +
                " (t_c), " + str::sci(hi.peak / base.peak - 1.0) +
                " (peak); 96 kHz f_max 16 kHz: " + str::sci(hiLow.duration / base.duration - 1.0) +
                " (t_c), " + str::sci(hiLow.peak / base.peak - 1.0) +
                " (peak); contact intervals " + std::to_string(base.intervals));
            CHECK(std::fabs(low.duration / base.duration - 1.0) <= 0.05);
            CHECK(std::fabs(low.peak / base.peak - 1.0) <= 0.05);
            CHECK(std::fabs(hi.duration / base.duration - 1.0) <= 0.05);
            CHECK(std::fabs(hi.peak / base.peak - 1.0) <= 0.05);
        }
    }
}

TEST_CASE(
    "A5-T2 engine side contact duration and peak force sensitivity to f_max and fs (SPEC "
    "scratch scheme)") {
    sensitivityCase<ham::ScratchRig<float>, ham::ScratchRig<double>>();
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE(
    "A5-T2 engine side contact duration and peak force sensitivity to f_max and fs (A5 "
    "StruckNote)") {
    sensitivityCase<ham::A5Rig<float>, ham::A5Rig<double>>();
}
#endif

#ifdef PME_A5T2_REFERENCE_CLI

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
    double simRate;  ///< simulation rate = scenario fs with oversampling 1
};

/// Writes the P0.5 scenario (schema_version 1, reference/scenario.schema.json): one string with
/// fixed ends and a hammer, observations hammer_penetration and hammer_force. This is the single
/// place that knows the scenario field names.
void writeScenario(const fs::path& path, const ham::OracleString& s, double speed, double width,
                   const RefGrid& grid) {
    const auto& sp = s.spec;
    std::ostringstream j;
    j << std::setprecision(17);
    j << "{\n  \"schema_version\": 1,\n  \"name\": \"A5-T2 hammer on a pinned string\",\n";
    j << "  \"fs\": " << grid.simRate << ",\n  \"oversampling\": 1,\n";
    j << "  \"duration\": " << kSimSeconds << ",\n  \"strings\": [\n";
    j << R"(    {"params": {"L": )" << sp.L << ", \"dCore\": " << sp.dCore
      << ", \"rhoCore\": " << sp.rhoCore << ", \"ECore\": " << sp.ECore
      << ", \"dOuter\": " << sp.dOuter << R"(, "rhoWind": 0, "packing": 0, "epsWind": 0)"
      << ", \"f1Target\": " << sp.f1 << ", \"b1\": " << sp.b1 << R"(, "b2": 0, "b3": )" << sp.b3
      << ", \"boundary\": \"pinned\", \"detuneH\": 0, \"bridgeEnd\": \"at_L\"},\n"
      << R"(     "grid": {"intervals": )" << grid.intervals << "}}\n  ],\n";
    j << R"(  "hammers": [{"mass": )" << ham::kHammerMass << R"(, "K": )" << ham::kFeltK
      << R"(, "p": )" << ham::kFeltP << R"(, "mu": 0, "width": )" << width << R"(, "strike_x": )"
      << kStrike * sp.L << R"(, "velocity": )" << speed << ", \"strings\": [0]}],\n";
    j << "  \"observations\": [\n"
      << R"(    {"name": "delta", "kind": "hammer_penetration", "hammer": 0},)" << "\n"
      << R"(    {"name": "force", "kind": "hammer_force", "hammer": 0})" << "\n  ]\n}\n";
    std::ofstream out(path);
    out << j.str();
}

std::string quote(const std::string& text) { return '"' + text + '"'; }

/// Runs the CLI and returns the contact estimates of the simulation-rate signals.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): dir, string, speed, width, level, grid
Contact renderReference(const fs::path& dir, const ham::OracleString& s, double speed, double width,
                        int level, const RefGrid& grid) {
    const std::string tag = "t2_" + std::to_string(level);
    const fs::path scenario = dir / (tag + ".json");
    const fs::path features = dir / (tag + "_features.json");
    const fs::path wav = dir / (tag + ".wav");
    writeScenario(scenario, s, speed, width, grid);
    std::string cmd = quote(PME_A5T2_REFERENCE_CLI) + " render " + quote(scenario.string()) +
                      " --features " + quote(features.string()) + " --wav " + quote(wav.string());
#ifdef _WIN32
    cmd = '"' + cmd + '"';  // cmd.exe /c strips one pair of outer quotes
#endif
    INFO("reference command: " << cmd);
    const int rc = std::system(cmd.c_str());  // NOLINT(concurrency-mt-unsafe,cert-env33-c)
    REQUIRE(rc == 0);
    const pme::analysis::WavData data = pme::analysis::read_wav(wav);
    REQUIRE(data.sample_rate_hz == doctest::Approx(grid.simRate));
    REQUIRE(data.channels == 2);
    std::vector<double> delta;
    std::vector<double> force;
    for (std::size_t f = 0; f < data.frames(); ++f) {
        delta.push_back(data.samples[f * data.channels]);
        force.push_back(data.samples[f * data.channels + 1]);
    }
    return estimate(delta, force, 1.0 / grid.simRate);
}

struct ReferenceValues {
    ham::Ladder duration;
    ham::Ladder peak;
};

ReferenceValues referenceLadder(const ham::OracleString& s, double speed, double width) {
    const int n0 = envInt("PME_A5T2_N0", 200);
    const int fs0 = envInt("PME_A5T2_FS0", 96000);
    const fs::path dir = pme::test::unique_scratch_dir("pme_a5t2");
    fs::create_directories(dir);
    std::array<Contact, 3> c;
    for (int level = 0; level < 3; ++level) {
        const RefGrid grid{n0 << level, static_cast<double>(fs0) * (1 << level)};
        c[static_cast<std::size_t>(level)] = renderReference(dir, s, speed, width, level, grid);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    ReferenceValues r;
    r.duration = ham::richardson(c[0].duration, c[1].duration, c[2].duration);
    r.peak = ham::richardson(c[0].peak, c[1].peak, c[2].peak);
    str::report(
        std::string("A5-T2 reference ladder, ") + windowName(width) +
        ", v = " + std::to_string(speed) + " m/s, N = " + std::to_string(n0) +
        " x 2^l, fs = " + std::to_string(fs0) + " x 2^l: t_c levels " + str::sci(c[0].duration) +
        ", " + str::sci(c[1].duration) + ", " + str::sci(c[2].duration) + " -> " +
        str::sci(r.duration.value) + " s (observed order " +
        std::to_string(r.duration.observedOrder) + ", correction " +
        str::sci(r.duration.correctionRel) + "); peak levels " + str::sci(c[0].peak) + ", " +
        str::sci(c[1].peak) + ", " + str::sci(c[2].peak) + " -> " + str::sci(r.peak.value) +
        " N (observed order " + std::to_string(r.peak.observedOrder) + ", correction " +
        str::sci(r.peak.correctionRel) + "); contact intervals " + std::to_string(c[2].intervals));
    return r;
}

template <class RigF, class RigD>
void runReferenceCase(double speed, double width) {
    // The reference string is the engine string (same physical description); the table is only
    // needed for its spec.
    const ham::OracleString s = buildString(kEngineFs, kEngineFMax);
    str::report("A5-T2 string: B = " + str::sci(s.b) +
                " (target 2e-4), d = " + str::sci(s.spec.dCore) + " m, f0 = " + str::sci(s.f0) +
                " Hz, " + std::to_string(s.modes.omega.size()) + " modes below 20 kHz at 48 kHz");
    static std::map<std::pair<double, double>, ReferenceValues> cache;  // both backends share it
    const auto key = std::make_pair(speed, width);
    if (cache.find(key) == cache.end()) {
        cache[key] = referenceLadder(s, speed, width);
    }
    const ReferenceValues ref = cache[key];
    const Contact e48 = engineContact<RigF>(kEngineFs, kEngineFMax, speed, width);
    const Contact e96 = engineContact<RigD>(96000.0, kEngineFMax, speed, width);
    const double errPeak48 = e48.peak / ref.peak.value - 1.0;
    const double errDur48 = e48.duration / ref.duration.value - 1.0;
    const double errPeak96 = e96.peak / ref.peak.value - 1.0;
    const double errDur96 = e96.duration / ref.duration.value - 1.0;
    str::report(std::string("A5-T2 ") + windowName(width) + ", v = " + std::to_string(speed) +
                " m/s: 48 kHz float32 peak " + str::sci(e48.peak) + " N (error " +
                str::sci(errPeak48) + "), t_c " + str::sci(e48.duration) + " s (error " +
                str::sci(errDur48) + "); 96 kHz float64 peak error " + str::sci(errPeak96) +
                ", t_c error " + str::sci(errDur96) + " (tolerance 0.02 at 48 kHz)");
    CHECK(std::fabs(errPeak48) <= kPassTol);
    CHECK(std::fabs(errDur48) <= kPassTol);
    // The comparison is only meaningful when the reference itself is converged.
    CHECK(ref.duration.correctionRel <= kRefEstimateTol);
    CHECK(ref.peak.correctionRel <= kRefEstimateTol);
}

}  // namespace

TEST_CASE("A5-T2 point contact 1 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(1.0, 0.0);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 point contact 1 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(1.0, 0.0);
}
#endif
TEST_CASE("A5-T2 point contact 3 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(3.0, 0.0);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 point contact 3 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(3.0, 0.0);
}
#endif
TEST_CASE("A5-T2 point contact 5 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(5.0, 0.0);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 point contact 5 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(5.0, 0.0);
}
#endif
TEST_CASE("A5-T2 raised cosine 10 mm 1 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(1.0, kWidthRaisedCosine);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 raised cosine 10 mm 1 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(1.0, kWidthRaisedCosine);
}
#endif
TEST_CASE("A5-T2 raised cosine 10 mm 3 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(3.0, kWidthRaisedCosine);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 raised cosine 10 mm 3 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(3.0, kWidthRaisedCosine);
}
#endif
TEST_CASE("A5-T2 raised cosine 10 mm 5 m/s vs P0.5 reference solver (SPEC scratch scheme)") {
    runReferenceCase<ham::ScratchRig<float>, ham::ScratchRig<double>>(5.0, kWidthRaisedCosine);
}
#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE("A5-T2 raised cosine 10 mm 5 m/s vs P0.5 reference solver (A5 StruckNote)") {
    runReferenceCase<ham::A5Rig<float>, ham::A5Rig<double>>(5.0, kWidthRaisedCosine);
}
#endif

#endif  // PME_A5T2_REFERENCE_CLI
