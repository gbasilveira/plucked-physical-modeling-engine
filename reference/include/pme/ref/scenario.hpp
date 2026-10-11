// SPDX-License-Identifier: MIT
// Scenario description of the reference solver (P0.5-R7) and its JSON reader. The format is
// documented in reference/README.md and described by reference/scenario.schema.json
// (schema_version 1). The structures reuse the engine's description structs at interfaces-v2.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pme/barriers.hpp"
#include "pme/body.hpp"
#include "pme/bridge.hpp"
#include "pme/exciters.hpp"
#include "pme/string.hpp"
#include "pme/types.hpp"

namespace pme::ref {

// String initial state: the closed-form triangle of SPEC-PLK "Initial displacement" released at
// rest. The displacement is split into the polarisations by `angle` (v: cos, h: sin).
struct InitialDisplacementSpec {
    bool enabled = false;
    double amplitude = 0.0;  // A (m)
    double position = 0.0;   // x_p (m)
    double angle = 0.0;      // theta_p (rad)
};

// SPEC-GEO switches. Tension modulation and longitudinal waves are supported for a single string
// with fixed ends, an initial displacement and no hammer, barrier, bridge or plate.
struct GeometricSpec {
    bool tension_modulation = false;
    bool longitudinal = false;
    double longitudinal_b1 = 0.0;  // 1/s
    double longitudinal_b3 = 0.0;  // s
    // Optional initial longitudinal state: a triangle of height `amplitude` (m) with its apex at
    // `position` (m), released at rest. Used to isolate the free longitudinal modes.
    bool has_longitudinal_initial = false;
    double longitudinal_initial_amplitude = 0.0;
    double longitudinal_initial_position = 0.0;
};

struct StringSpec {
    std::string name;
    StringParams params;
    int intervals = 100;  // N: grid intervals along the string
    int bridge = -1;      // index into Scenario::bridges; -1 = fixed end at x = L
    InitialDisplacementSpec initial;
    GeometricSpec geometric;
};

// Hammer (SPEC-HAM): lumped mass with the felt law K [delta]_+^p and the optional Hunt-Crossley
// term R = K mu [delta]_+^p. It strikes the listed strings (vertical polarisation) over a raised
// cosine window of width `width` centred at `strike_x`; with `wall` it strikes a fixed felt
// contact instead (SPEC-HAM "Oracle").
struct HammerSpec {
    double mass = 0.0;      // m_H (kg)
    double K = 0.0;         // felt stiffness (N/m^p)
    double p = 2.5;         // felt exponent
    double mu = 0.0;        // Hunt-Crossley mu (s/m)
    double width = 0.0;     // window width b (m); 0 = point contact
    double strike_x = 0.0;  // x_c (m) on the string
    double velocity = 0.0;  // v_H (m/s) towards the string
    double gap = 0.0;       // initial gap (m); 0 selects v_H dt (contact starts on the next step)
    std::vector<int> strings;  // indices into Scenario::strings
    bool wall = false;
};

enum class BridgeModelKind { MassSpringDashpot, Conductance };

// Bridge point (SPEC-BRG). `desc` holds the position (body frame), theta_b and the horizontal
// conductance G_h; the parallel axis is modelled by `kind`.
struct BridgeSpec {
    BridgePointDesc desc;
    BridgeModelKind kind = BridgeModelKind::MassSpringDashpot;
    double mass = 0.0;         // m_b (kg)
    double stiffness = 0.0;    // k_b (N/m)
    double damping = 0.0;      // r_b (N s/m)
    double conductance = 0.0;  // G (m/(N s)); kind Conductance
};

struct PlateSpec {
    PlateParams params;
    int nx = 40;  // intervals along x
    int ny = 30;  // intervals along y
    // Rayleigh loss of the plate, sigma(omega) = b1 + b3 omega^2. When both are zero and
    // params.lossFactor > 0, b3 is chosen so that sigma = eta omega / 2 at loss_reference_hz.
    double loss_b1 = 0.0;
    double loss_b3 = 0.0;
    double loss_reference_hz = 0.0;
    // Optional initial state: a displacement spike at (x, y) spread bilinearly over the four
    // nearest nodes, released at rest.
    bool has_initial = false;
    double initial_x = 0.0;
    double initial_y = 0.0;
    double initial_amplitude = 0.0;
};

struct BarrierSpec {
    int string = 0;
    BarrierParams params;
};

enum class ObservationKind {
    StringDisplacement,
    StringVelocity,
    BridgeForce,         // component along the bridge's parallel axis (SPEC-BRG)
    BridgeForcePerp,     // component along the perpendicular axis
    BridgeDisplacement,  // parallel axis
    BridgeVelocity,
    HammerForce,        // total force of hammer `index` on its strings or wall
    HammerPenetration,  // penetration of the hammer's first contact
    HammerPosition,
    BarrierForce,  // total force of barrier `index`
    PlateDisplacement,
    LongitudinalBridgeForce,   // F_L of SPEC-GEO
    LongitudinalDisplacement,  // longitudinal displacement xi at position x
};

struct ObservationSpec {
    std::string name;
    ObservationKind kind = ObservationKind::StringDisplacement;
    int index = 0;         // string, bridge, hammer or barrier index
    double x = 0.0;        // position on the string (m)
    int polarisation = 0;  // 0 = vertical, 1 = horizontal
    Point point;           // plate observation point (m)
};

struct Scenario {
    int schema_version = 1;
    std::string name;
    double fs = 48000.0;    // nominal sample rate (Hz)
    int oversampling = 1;   // simulation rate = fs * oversampling
    double duration = 1.0;  // s
    double theta = 0.25;    // theta of the implicit scheme (Bilbao 2009, section 7.2.2)
    std::vector<StringSpec> strings;
    std::vector<HammerSpec> hammers;
    std::vector<BridgeSpec> bridges;
    std::optional<PlateSpec> plate;
    std::vector<BarrierSpec> barriers;
    std::vector<ObservationSpec> observations;
    int feature_partials = 12;  // partials reported in the golden features
};

// Parses a scenario from JSON text. Throws RefError naming the offending field for unknown keys,
// wrong types, schema_version != 1 and values the reference does not model.
Scenario parse_scenario(std::string_view json_text);
Scenario read_scenario_file(const std::string& path);

// Checks a parsed scenario for consistency and for features the reference does not model.
// Throws RefError naming the field. Called by parse_scenario and by the simulation.
void validate_scenario(const Scenario& scenario);

}  // namespace pme::ref
