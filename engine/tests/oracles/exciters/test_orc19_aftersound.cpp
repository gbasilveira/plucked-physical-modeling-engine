// SPDX-License-Identifier: MIT
// Covers: ORC-19
//
// ORC-19 Una corda aftersound (SPEC-ORC; SPEC-HAM "Una corda (grand)"; SPEC-BRG "Unison
// coupling"): "Trichord, s = 1. The passive string's bridge force starts at zero and grows over
// time, as in Miranda Valiente et al. (2024) [https://eprints.soton.ac.uk/499445]. Qualitative,
// plus regression against the first accepted run."
//
// Setup. A trichord of three identical plain steel strings (L = 0.65 m, core d = 1.2 mm,
// f1 = 220 Hz, B about 4e-4, intrinsic loss b1 = 0.3 1/s, b2 = 0, b3 = 2e-9 s, f_max = 16 kHz) on
// one bridge point: a mass-spring-damper of m = 0.3 kg and resonance 250 Hz (k_b = m Omega^2) with
// the dashpot r_b chosen so that Z0 |Y(f0)| = 0.02 at the string's own f0, Z0 = sqrt(T rhoA),
// Y = 1 / (r_b + i (w m - k_b / w)) (SPEC-CONV e^(+iwt)): r_b = sqrt((Z0 / 0.02)^2 - X^2),
// X = w0 m - k_b / w0 (the values are printed); theta_b = 0.1 rad, G_h = 0. Una corda s = 1 with
// leavingString = 0: S(1) = 1, so w_0 = 0 and strings 1 and 2 are struck with w = 1 and the felt
// K_s = K [1 - s (1 - kappa_uc)] = 0.6 K (kappa_uc = 0.6, Delta p_uc = 0). Hammer: 8 g at 3 m/s,
// K = 4.5e9 N/m^p, p = 2.5, mu_HC = 0.2 s/m, raised cosine b_H = 10 mm at beta_c = 1/8. Float32
// states (the engine instantiation), 48 kHz, 3 s, drift relaxation at its default (on).
//
// Observable. F_b,0(t): the bridge force of the passive string 0, vertical polarisation,
// F_b = sum_n a_n q_n - (T/L) w_b with a_n = rhoA omegaHat_n^2 g_n (SPEC-BRG "Bridge force"),
// from its own modes and the bridge displacement.
//
// Qualitative checks.
//   1. F_b,0^n is exactly 0 for every sample n up to and including the first sample at which the
//      bridge has moved (w^n = 0 and its own modes are at rest); it is non-zero from the next
//      sample on.
//   2. (SI-0030) The RMS of F_b,0 over the first 2 ms is at least 60 dB below the struck string 1's
//      first 50 ms window; the RMS envelope of F_b,0 in non-overlapping 50 ms windows rises by at
//      least 6 dB above its own first window within 0.5 s (best window up to 0.55 s; the measured
//      rise is reported); the envelope's maximum lies after 0.1 s. SHOULD, reported, not gating:
//      the rise within 100 ms grows monotonically with Z0|Y| over 0.005, 0.01, 0.02.
//   3. The struck strings' bridge forces start immediately: their first non-zero sample is within
//      4 samples of the launch (the first contact sample is n = 2).
//
// Regression. The envelope (window centres and RMS values over the first 3 s) is compared with
// data/orc19_envelope.json, a PROVISIONAL first run (pending owner acceptance), window by window:
// |20 log10(rms / golden)|
// <= 0.5 dB (allows cross-platform float32 differences). The file is written from the SPEC
// A5 StruckNote run with PME_ORC19_WRITE_GOLDEN=<path>; provisional until the owner accepts it.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"

namespace {

namespace ham = pme::oracle::ham;
namespace str = pme::oracle::str;

constexpr double kFs = 48000.0;
constexpr double kSeconds = 3.0;
constexpr double kWindowSeconds = 0.05;
constexpr double kRiseDb = 6.0;    // growth check: envelope at 0.5 s over the first window
constexpr double kStartDb = 60.0;  // SI-0030: passive start below struck first window
constexpr double kStartSeconds = 0.002;
constexpr double kRiseCheckTime = 0.5;  // s
constexpr double kPeakAfter = 0.1;      // s: the envelope maximum lies after this time
constexpr std::size_t kImmediateSamples = 4;
constexpr double kGoldenToleranceDb = 0.5;  // per window
constexpr double kF1 = 220.0;
constexpr double kDCore = 1.2e-3;
constexpr double kB1 = 0.3;
constexpr double kB3 = 2.0e-9;
constexpr double kFMax = 16000.0;
constexpr double kBridgeMass = 0.3;
constexpr double kBridgeHz = 250.0;
constexpr double kDefaultZ0Y = 0.02;
constexpr double kThetaB = 0.1;
constexpr double kMuHC = 0.2;
constexpr double kSpeed = 3.0;

struct Orc19Data {
    std::vector<double> f0, f1, f2;  ///< F_b,s^n, vertical, strings 0 (passive), 1, 2
    std::vector<double> w;           ///< w_V^(n+1)
    double bridgeDamping{0.0};
    double z0{0.0};
    double f0Hz{0.0};
};

template <class RigT>
Orc19Data runOrc19(double targetZ0Y = kDefaultZ0Y) {
    ham::OracleString s;
    REQUIRE(ham::makeString(kF1, kDCore, kB1, kB3, kFs, kFMax, s));
    // Bridge dashpot from Z0 |Y(f0)| = 0.02 (SPEC-BRG small-admittance quantity, SPEC-CONV
    // e^(+iwt)).
    const double w0 = 2.0 * ham::kPi * s.f0;
    const double omegaB = 2.0 * ham::kPi * kBridgeHz;
    const double kb = kBridgeMass * omegaB * omegaB;
    const double x = w0 * kBridgeMass - kb / w0;
    const double zMag = s.z0 / targetZ0Y;
    REQUIRE(zMag > std::fabs(x));
    const double rb = std::sqrt(zMag * zMag - x * x);

    ham::NoteSetup cfg;
    cfg.strings = {s.modes, s.modes, s.modes};
    cfg.hammer.mass = ham::kHammerMass;
    cfg.hammer.K = ham::kFeltK;
    cfg.hammer.p = ham::kFeltP;
    cfg.hammer.muHC = kMuHC;
    cfg.hammer.width = 0.01;
    cfg.hammer.strikeRatio = 1.0 / 8.0;
    cfg.una.leavingString = 0;
    cfg.shift = 1.0;
    cfg.hammerSpeed = kSpeed;
    cfg.bridge = ham::BridgeModel::massSpringDamper(kBridgeMass, kb, rb);
    cfg.thetaB = kThetaB;
    cfg.fs = kFs;
    cfg.driftRelaxation = true;

    RigT rig(cfg);
    REQUIRE(rig.ok());
    Orc19Data d;
    d.bridgeDamping = rb;
    d.z0 = s.z0;
    d.f0Hz = s.f0;
    const auto steps = static_cast<std::size_t>(kSeconds * kFs);
    d.f0.reserve(steps);
    d.f1.reserve(steps);
    d.f2.reserve(steps);
    d.w.reserve(steps);
    for (std::size_t n = 0; n < steps; ++n) {
        rig.step();
        d.f0.push_back(rig.bridgeForceV(0));
        d.f1.push_back(rig.bridgeForceV(1));
        d.f2.push_back(rig.bridgeForceV(2));
        d.w.push_back(rig.bridgeW());
    }
    return d;
}

struct Envelope {
    std::vector<double> centres;  ///< window centre times (s)
    std::vector<double> rms;      ///< RMS of F_b,0 in the window (N)
};

Envelope envelope(const std::vector<double>& f) {
    Envelope e;
    const auto len = static_cast<std::size_t>(kWindowSeconds * kFs);
    for (std::size_t start = 0; start + len <= f.size(); start += len) {
        double sum = 0.0;
        for (std::size_t i = start; i < start + len; ++i) {
            sum += f[i] * f[i];
        }
        e.centres.push_back((static_cast<double>(start) + 0.5 * static_cast<double>(len)) / kFs);
        e.rms.push_back(std::sqrt(sum / static_cast<double>(len)));
    }
    return e;
}

double db(double x) { return 20.0 * std::log10(x); }

/// Numbers of the JSON array stored under `key` (the golden file has flat arrays of numbers).
std::vector<double> readArray(const std::string& text, const std::string& key) {
    std::vector<double> v;
    const std::size_t at = text.find("\"" + key + "\"");
    if (at == std::string::npos) {
        return v;
    }
    const std::size_t open = text.find('[', at);
    const std::size_t close = text.find(']', open);
    if (open == std::string::npos || close == std::string::npos) {
        return v;
    }
    const char* p = text.c_str() + open + 1;
    const char* end = text.c_str() + close;
    while (p < end) {
        char* next = nullptr;
        const double x = std::strtod(p, &next);
        if (next == p) {
            ++p;
            continue;
        }
        v.push_back(x);
        p = next;
    }
    return v;
}

/// Value of environment variable `name`, or an empty string when unset. std::getenv is deprecated
/// on MSVC (-Werror,-Wdeprecated-declarations), so Windows uses _dupenv_s.
std::string envString(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return value;
#else
    const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
    return v != nullptr ? std::string(v) : std::string();
#endif
}

void writeGolden(const std::string& path, const Envelope& e) {
    std::ofstream out(path);
    out << "{\n";
    out << "  \"_comment\": \"ORC-19 una corda aftersound, envelope of the passive string's "
           "vertical "
           "bridge force F_b,0 (N): RMS in non-overlapping 50 ms windows over 3 s. This is the "
           "PROVISIONAL first run (pending the owner's acceptance), written by the A5 "
           "StruckNote (the SPEC scratch scheme agrees to 1e-4 dB), float32 at 48 kHz; see "
           "test_orc19_aftersound.cpp for the setup. "
           "Comparison: 0.5 dB per window.\",\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"window_seconds\": " << kWindowSeconds << ",\n";
    out << "  \"tolerance_db\": " << kGoldenToleranceDb << ",\n";
    out << "  \"window_centres_s\": [";
    out << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < e.centres.size(); ++i) {
        out << (i != 0 ? ", " : "") << e.centres[i];
    }
    out << "],\n  \"rms_newtons\": [";
    out << std::scientific << std::setprecision(9);
    for (std::size_t i = 0; i < e.rms.size(); ++i) {
        out << (i != 0 ? ", " : "") << e.rms[i];
    }
    out << "]\n}\n";
}

/// Everything the checks read from one run.
struct Orc19Metrics {
    Orc19Data data;
    Envelope env;
    std::size_t firstMove{0};     ///< first n with w^(n+1) != 0
    std::size_t firstStruck1{0};  ///< first n with F_b,1^n != 0
    std::size_t firstStruck2{0};
    std::size_t riseWindow{0};  ///< window containing t = 0.5 s
    std::size_t peakWindow{0};
    double riseDb{0.0};   ///< envelope at 0.5 s over the first window, dB
    double startDb{0.0};  ///< struck string first window over the passive first 2 ms RMS, dB
};

std::size_t firstNonZero(const std::vector<double>& v) {
    for (std::size_t n = 0; n < v.size(); ++n) {
        if (v[n] != 0.0) {
            return n;
        }
    }
    return v.size();
}

template <class RigT>
Orc19Metrics measure(const char* label) {
    Orc19Metrics m;
    m.data = runOrc19<RigT>();
    const Orc19Data& d = m.data;
    str::report(std::string("ORC-19 ") + label + ": f0 = " + str::sci(d.f0Hz) +
                " Hz, Z0 = " + str::sci(d.z0) + " kg/s, bridge r_b = " + str::sci(d.bridgeDamping) +
                " N s/m (Z0 |Y(f0)| = 0.02), m = 0.3 kg, 250 Hz, theta_b = 0.1 rad");
    m.firstMove = firstNonZero(d.w);
    m.firstStruck1 = firstNonZero(d.f1);
    m.firstStruck2 = firstNonZero(d.f2);
    m.env = envelope(d.f0);
    REQUIRE(m.env.rms.size() == 60);
    REQUIRE(m.env.rms[0] > 0.0);
    m.riseWindow = static_cast<std::size_t>(kRiseCheckTime / kWindowSeconds);  // 0.5 to 0.55 s
    m.riseDb = -1.0e300;
    for (std::size_t i = 1; i <= m.riseWindow; ++i) {  // "within 0.5 s": best window up to 0.55 s
        m.riseDb = std::max(m.riseDb, db(m.env.rms[i] / m.env.rms[0]));
    }
    for (std::size_t i = 1; i < m.env.rms.size(); ++i) {
        if (m.env.rms[i] > m.env.rms[m.peakWindow]) {
            m.peakWindow = i;
        }
    }
    const auto head = static_cast<std::size_t>(kStartSeconds * kFs);
    double sum = 0.0;
    for (std::size_t n = 0; n < head; ++n) {
        sum += d.f0[n] * d.f0[n];
    }
    // SI-0030: the passive force starts at least 60 dB below the struck strings' first window.
    const Envelope struckFirst = envelope(d.f1);
    m.startDb = db(struckFirst.rms[0] / std::sqrt(sum / static_cast<double>(head)));
    str::report(std::string("ORC-19 ") + label + ": the bridge first moves at sample " +
                std::to_string(m.firstMove + 1) + ", the passive F_b,0 is non-zero from sample " +
                std::to_string(m.firstMove + 1) + ", struck strings from samples " +
                std::to_string(m.firstStruck1) + " and " + std::to_string(m.firstStruck2) +
                "; envelope: first window " + str::sci(m.env.rms[0]) + " N, window at 0.5 s " +
                str::sci(m.env.rms[m.riseWindow]) + " N (" + std::to_string(m.riseDb) +
                " dB above the first window), maximum " + str::sci(m.env.rms[m.peakWindow]) +
                " N at t = " + std::to_string(m.env.centres[m.peakWindow]) + " s, " +
                std::to_string(m.startDb) +
                " dB below the struck string 1 first-window RMS (passive first 2 ms)");
    return m;
}

template <class RigT>
void orc19Case(const char* label) {
    const Orc19Metrics m = measure<RigT>(label);
    const Orc19Data& d = m.data;

    // Check 1: the passive string's force is exactly zero until the bridge has moved.
    REQUIRE(m.firstMove < d.w.size());
    bool zeroUntilMove = true;
    for (std::size_t n = 0; n <= m.firstMove; ++n) {
        zeroUntilMove = zeroUntilMove && (d.f0[n] == 0.0);
    }
    CHECK(zeroUntilMove);
    CHECK(d.f0[m.firstMove + 1] != 0.0);

    // Check 3: the struck strings' forces start immediately and lead the passive string.
    CHECK(m.firstStruck1 <= kImmediateSamples);
    CHECK(m.firstStruck2 <= kImmediateSamples);
    CHECK(m.firstStruck1 < m.firstMove + 1);

    // Check 2 (growth): the passive force starts at (numerically) zero and its envelope grows, then
    // peaks after 0.1 s.
    CHECK(m.startDb >= kStartDb);
    CHECK(m.riseDb >= kRiseDb);
    CHECK(m.env.centres[m.peakWindow] > kPeakAfter);

    // Regression against the first accepted run.
    if (const std::string writePath = envString("PME_ORC19_WRITE_GOLDEN"); !writePath.empty()) {
        writeGolden(writePath, m.env);
    }
#ifdef PME_ORC19_GOLDEN
    std::ifstream in(PME_ORC19_GOLDEN);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    const std::vector<double> centres = readArray(text, "window_centres_s");
    const std::vector<double> golden = readArray(text, "rms_newtons");
    REQUIRE(centres.size() == m.env.rms.size());
    REQUIRE(golden.size() == m.env.rms.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < golden.size(); ++i) {
        CHECK(std::fabs(centres[i] - m.env.centres[i]) < 1.0e-6);
        const double dev = std::fabs(db(m.env.rms[i] / golden[i]));
        worst = std::max(worst, dev);
        CHECK(dev <= kGoldenToleranceDb);
    }
    str::report(std::string("ORC-19 ") + label + ": worst window deviation from the golden " +
                "envelope " + std::to_string(worst) + " dB (tolerance 0.5)");
#endif
}

/// SHOULD of the brief (reported, not gating): the passive string's rise within the first 100 ms
/// (window 50 to 100 ms over the first window) grows monotonically with Z0 |Y| over 0.005, 0.01
/// and 0.02.
template <class RigT>
void orc19Sweep(const char* label) {
    std::array<double, 3> rise{};
    const std::array<double, 3> targets{0.005, 0.01, 0.02};
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Orc19Data d = runOrc19<RigT>(targets[i]);
        const Envelope e = envelope(d.f0);
        rise[i] = db(e.rms[1] / e.rms[0]);
    }
    const bool monotone = rise[0] <= rise[1] && rise[1] <= rise[2];
    str::report(std::string("ORC-19 ") + label +
                ": rise of the 50-100 ms window over the first window at Z0|Y| = 0.005, 0.01, "
                "0.02: " +
                std::to_string(rise[0]) + ", " + std::to_string(rise[1]) + ", " +
                std::to_string(rise[2]) + " dB; monotone: " + (monotone ? "yes" : "no") +
                " (SHOULD, not gating)");
    CHECK(std::isfinite(rise[0] + rise[1] + rise[2]));
}

}  // namespace

TEST_CASE(
    "ORC-19 una corda aftersound passive string bridge force trichord s = 1 (SPEC scratch "
    "scheme)") {
    orc19Case<ham::ScratchRig<float>>("SPEC scratch scheme");
}

TEST_CASE(
    "ORC-19 una corda aftersound rise within 100 ms versus bridge admittance reported (SPEC "
    "scratch scheme)") {
    orc19Sweep<ham::ScratchRig<float>>("SPEC scratch scheme");
}

#ifdef PME_ORACLE_HAMMER_A5
TEST_CASE(
    "ORC-19 una corda aftersound passive string bridge force trichord s = 1 (A5 StruckNote)") {
    orc19Case<ham::A5Rig<float>>("A5 StruckNote");
}

TEST_CASE(
    "ORC-19 una corda aftersound rise within 100 ms versus bridge admittance reported (A5 "
    "StruckNote)") {
    orc19Sweep<ham::A5Rig<float>>("A5 StruckNote");
}
#endif
